#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

char *path = "/tmp/brighnessctlr";

int main(int argc, char **argv)
{
    if(argc != 2){
        printf("Usage: %s [delta%%]\n", argv[0]);
        return 1;
    }

    int8_t delta = strtol(argv[1], NULL, 10);
    if(!delta) return 0;
    printf("%d\n", delta);

    int fd = open(path, O_WRONLY);
    write(fd, &delta, 1);
    close(fd);
    return 0;
}
