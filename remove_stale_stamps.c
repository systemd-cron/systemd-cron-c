#define _GNU_SOURCE
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>

#define TIMERS_DIR "/var/lib/systemd/timers"

int main(int argc, char *argv[]) {
        DIR *dirp;
        struct dirent *dent;
        struct stat sb;
        // dent->d_name[256]
        char basename[170];
        char unit[200];

        if(chdir(TIMERS_DIR)) {
                return 0;
        }

        dirp = opendir(TIMERS_DIR);
        if (dirp == NULL) {
                return 0;
        }

        while ((dent = readdir(dirp))) {
                if (strncmp(dent->d_name, "stamp-cron-", strlen("stamp-cron-")))
                        continue;
                if (strcmp(dent->d_name + strlen(dent->d_name) - strlen(".timer"),".timer"))
                        continue;
		strncpy(basename, &dent->d_name[strlen("stamp-")], sizeof(basename)-1);

                snprintf(unit, sizeof(unit), "/usr/lib/systemd/system/%s", basename);
                if (stat(unit, &sb) != -1)
                        continue;

                snprintf(unit, sizeof(unit), "/run/systemd/generator/%s", basename);
                if (stat(unit, &sb) != -1)
                        continue;

                printf("Removing stale stamp /var/lib/systemd/timers/%s\n", dent->d_name);
                if(unlink(dent->d_name)) {
                     perror("failed");
                };
	}
        closedir(dirp);
}
