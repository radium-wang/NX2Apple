#include <fcntl.h>
#include <linux/input.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

int main(void) {
    int fd=open("/dev/input/event8", O_WRONLY);
    if (fd<0) return 1;
    char line[16];
    while (fgets(line, sizeof line, stdin)) {
        struct timespec t;
        struct input_event axis={.type=EV_ABS,.code=ABS_HAT0Y,.value=line[0]=='d'?1:0};
        struct input_event syn={.type=EV_SYN,.code=SYN_REPORT};
        if (line[0]!='p' && write(fd,&axis,sizeof axis)!=sizeof axis) return 2;
        clock_gettime(CLOCK_MONOTONIC,&t);
        if (line[0]!='p' && write(fd,&syn,sizeof syn)!=sizeof syn) return 2;
        printf("%lld\n",(long long)t.tv_sec*1000000000+t.tv_nsec);
        fflush(stdout);
    }
    close(fd);
}
