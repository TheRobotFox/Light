#include <ddcutil_types.h>
#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>

#include <ddcutil_c_api.h>
#include <ddcutil_status_codes.h>
#include <ddcutil_macros.h>

#include "List/List.h"

#define BRIGHTNESS 0x10

typedef struct {
    DDCA_Display_Ref ref;
    DDCA_Display_Handle handle;
    DDCA_Non_Table_Vcp_Value brightness;
} Display;

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

bool cmp_ref(const void *e, const void *t)
{
    const Display *a=e, *b=t;
    return a->ref==b->ref;
}

void sync_Displays(LIST(Display) list, bool dirty)
{
    // get all refs
    DDCA_Display_Ref *refs = NULL;
    ddca_get_display_refs(false, &refs);
    if(!refs){
        printf("[ERROR] Could not obtain Display referneces!\n");
        exit(-1);
    }

    while(*refs){
        // check valid ref and or new Ref
        Display *present = List_finde(list, cmp_ref, refs);
        if(ddca_validate_display_ref(*refs, false) != DDCRC_OK){
            printf("[WARINING] Got invalid Display Reference.\n");

            // cleanup present
            if(present){
                ddca_close_display(present->handle);
                List_rme(list, present);
            }
            goto next;
        }
        if(present && !dirty) goto next;

        // get handle
        if(!present){

            // get handle
            DDCA_Display_Handle h;

            if(ddca_open_display2(*refs, false, &h) != DDCRC_OK){
                printf("[WARNING] Could not get Display Handle.\n");
                goto next;
            }

            // has Brightness attribute
            if(!display_check_feature(h, BRIGHTNESS)){
                ddca_close_display(h);
                goto next;
            }
            present = List_push(list, NULL);
            present->ref = *refs;
            present->handle = h;
        }

        if(ddca_get_non_table_vcp_value(present->handle, BRIGHTNESS, &present->brightness) != DDCRC_OK){
            printf("[WARNING] Failed to Read Display! (%p;%p)\n", present->ref, present->handle);
            ddca_close_display(present->handle);
            List_rme(list, present);
            goto next;
        }

        next:
        refs++;
    }
}

void Display_List_free(LIST(Display) list)
{
    LIST_LOOP(Display, list, e){
        ddca_close_display(e->handle);
    }
    LIST_free(list);
}


static void init_daemon(void)
{
    // Permissions
    /* if(setgid(100) || setuid(1000)){ */
    /*     printf("[WARNING] Failed to lower Permissions!\n"); */
    /* } */

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
    nice(10);
}

static int run = 1;


pthread_mutex_t mut = PTHREAD_MUTEX_INITIALIZER;
pthread_t sync_thread;

char *path = "/tmp/brighnessctlr";
static void stop(int signo)
{
    remove(path);
    pthread_cancel(sync_thread);
    run = 0;
}

static void init_pipe(char *path)
{
    if(mkfifo(path, S_IRWXO | S_IRWXU | S_IRWXG) == EEXIST){
        printf("[WARNING] lightd terminated inapproprately!\n");
    }
}
typedef struct {
    LIST(Display) displays;
    bool dirty;
} Ctx;

void* sync_thread_function(void *_ctx)
{
    Ctx *ctx = _ctx;
    while(true) {
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        sleep(5);
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
        pthread_mutex_lock(&mut);

        sync_Displays(ctx->displays, ctx->dirty);
        ctx->dirty=true;
        pthread_mutex_unlock(&mut);
    }
    return NULL;
}

int main(void)
{
    // init
    init_daemon();

    char *path = "/tmp/brighnessctlr";
    init_pipe(path);

    ddca_init2("", DDCA_SYSLOG_ERROR, DDCA_INIT_OPTIONS_NONE, NULL);

    LIST(Display) list = LIST_create(Display);
    sync_Displays(list, true);

    Ctx ctx = {list, false};

    pthread_create(&sync_thread, NULL, sync_thread_function, &ctx);

    signal(SIGINT, stop);
    signal(SIGABRT, stop);

    int pipe;
    int8_t delta;
    start:
    pipe = open(path, O_RDONLY);

    while(run){
        int res = read(pipe, &delta, 1);
        if(res == EINTR) break;
        if(res != 1) goto start;

        pthread_mutex_lock(&mut);

        LIST_LOOP(Display, ctx.displays, d){
            DDCA_Non_Table_Vcp_Value val = d->brightness;
            int new = val.sl + delta;
            if(new>val.ml) new=val.ml;
            if(new<0) new=0;
            if(new!=val.sl) ddca_set_non_table_vcp_value(d->handle, BRIGHTNESS, 0, new);
            d->brightness.sl = new;
        }
        ctx.dirty = false;
        pthread_mutex_unlock(&mut);

    }

    remove(path);
    pthread_join(sync_thread, NULL);
    Display_List_free(ctx.displays);
    return 0;
}
