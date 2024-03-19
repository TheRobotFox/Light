#include <ddcutil_types.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <ddcutil_c_api.h>
#include <ddcutil_status_codes.h>
#include <ddcutil_macros.h>

#include "List/List.h"

#define BRIGHTNESS 0x10

IMPLEMENT_LIST(DDCA_Display_Handle)

bool display_check_feature(DDCA_Display_Handle h, DDCA_Vcp_Feature_Code feature)
{
    char *capability_str;
    if(ddca_get_capabilities_string(h, &capability_str) != DDCRC_OK){
        printf("[WARNING] Could not get Feature String\n");
        return false;
    }

    DDCA_Capabilities *capabilities;
    if(ddca_parse_capabilities_string(capability_str, &capabilities) != DDCRC_OK){
        printf("[WARNING] Could not get Capabilities\n");
        return false;
    }
    free(capability_str);

    DDCA_Feature_List features = ddca_feature_list_from_capabilities(capabilities);
    ddca_free_parsed_capabilities(capabilities);



    return ddca_feature_list_contains(features, feature);
}

LIST(DDCA_Display_Handle) get_compactible_dispaly_handles(DDCA_Vcp_Feature_Code feature)
{
    DDCA_Display_Ref *refs = NULL;
    ddca_get_display_refs(false, &refs);
    if(!refs){
        printf("[ERROR] Could not obtain Diplay referneces!\n");
        exit(-1);
    }

    LIST(DDCA_Dispay_Handle) list = LIST_create(DDCA_Display_Handle);

    while(*refs){
        if(ddca_validate_display_ref(*refs, false) != DDCRC_OK){
            printf("[WARINING] Got invalid Display Reference.\n");
            goto end;
        }

        DDCA_Display_Handle h;

        if(ddca_open_display2(*refs, false, &h) != DDCRC_OK){
            printf("[WARNING] Could not get Disply Handle.\n");
            goto end;
        }

        if(display_check_feature(h, feature))
            LIST_push(DDCA_Display_Handle)(list, h);

        end:
        refs++;
    }
    return list;
}

void Display_List_free(LIST(DDCA_Display_Handle) list)
{
    LIST_LOOP(DDCA_Display_Handle, list, e){
        ddca_close_display(*e);
    }
    LIST_free(list);
}

static void init_daemon(void)
{
    // Permissions
    if(setgid(100) || setuid(1000)){
        printf("[WARNING] Failed to lower Permissions!\n");
    }

    // Signals
    sigset_t set;
    if(sigemptyset(&set) || sigprocmask(SIG_SETMASK, &set, NULL)){
        printf("[WARNING] Failed to reset Singal Mask!\n");
    }

    // Root dir
    if(chdir("/") == -1){
        printf("[WARNING] Could not cd to root\n");
    }
    // priority
    /* nice(10); */
}

static volatile int run = 1;

static void stop(int signo)
{
    run = 0;
}

static void init_pipe(char *path)
{
    if(mkfifo(path, S_IRWXO | S_IRWXU | S_IRWXG) == EEXIST){
        printf("[WARNING] lightd terminated inapproprately!\n");
    }
}

int main(void)
{
    // init
    init_daemon();

    char *path = "/tmp/brighnessctlr";
    init_pipe(path);

    ddca_init2("", DDCA_SYSLOG_ERROR, DDCA_INIT_OPTIONS_NONE, NULL);

    // obtain display Handles
    // find dispalys with appropriate feature
    LIST(DDCA_Display_Handle) handles = get_compactible_dispaly_handles(BRIGHTNESS);

    int pipe = open(path, O_RDONLY);
    signal(SIGINT, stop);
    signal(SIGABRT, stop);

    int8_t delta;
    while(run){
        if(read(pipe, &delta, 1) != 1) continue;

        LIST_LOOP(DDCA_Display_Handle, handles, e){
            // get curret brightness
            DDCA_Non_Table_Vcp_Value val;
            if(ddca_get_non_table_vcp_value(*e, BRIGHTNESS, &val) != DDCRC_OK){
                printf("[WARNING] Failed to get current value!\n");
                continue;
            }
            int new = val.sl+delta;
            if(new>val.ml) new=val.ml;
            if(new<0) new=0;
            if(new!=val.sl) ddca_set_non_table_vcp_value(*e, BRIGHTNESS, 0, new);

        }

    }

    remove(path);
    Display_List_free(handles);
    return 0;
}
