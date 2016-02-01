#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    int delay, remainder;
    if (argc != 2 || !sscanf(argv[1],"%d %n", &delay, &remainder) || remainder != strlen(argv[1])) {
        fprintf(stderr, "Usage: boot-delay <minutes>\n");
        exit(1);
    }

    int uptime;
    FILE *fp;
    fp = fopen("/proc/uptime", "r");
    fscanf(fp, "%d.", &uptime);
    fclose(fp);

    if(delay > uptime)
        sleep(delay - uptime);
}
