/*-*- Mode: C; c-basic-offset: 8; indent-tabs-mode: nil -*-*/

/***
  This file is part of systemd.

  Copyright 2013 Shawn Landden

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>

#ifndef USER_CRONTABS
#define USER_CRONTABS "/var/spool/cron/crontabs"
#endif

static const char *arg_dest = "/tmp";

/*
const char *daysofweek[] = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
const char *isdow = "0123456";

int write_string_file(const char *fn, const char *line) {
        FILE *f = NULL;
        f = fopen(fn, "we");
        if (!f)
	    return -errno;
        fputs(line, f);
	return 0;
}

static int parse_dow(char *dow, char *dows){
        char c[2] = {"\0\0"};

        dows[0] = '\0';

        for(unsigned s = 0;s < strlen(dow);s++) {
                c[0] = dow[s];
                if (strchr(isdow, c[0]))
                        strncpy(dows + strlen(dows), daysofweek[atoi(&c[0])], 128 - strlen(dows));
                else {
                        int l = strlen(dows);
                        if (l < 128) {
                                dows[l] = c[0];
                                dows[l+1] = '\0';
                        }
                }
        }

        return 0;
}

static int parse_crontab_entry(char *line, const char *context, char *usertab) {
        int r = 0;
        int commandloc = 0;
        char *timer = NULL, *timerfn = NULL, *service = NULL, *servicefn = NULL;
        char dows[128];
        char *p;

        r = parse_dow(dow, &dows[0]);
        if (r < 0)
                return r;

        asprintf(&timerfn, "%s/cron-%s.timer", arg_dest, context);
        asprintf(&timer,"# Automatically generated by systemd-cron-generator\n"
                "# Was: %s\n"
                "[Unit]\n"
                "Description=[Cron] %s\n"
                "PartOf=cron.target\n"
                "RefuseManualStart=true\n"
                "RefuseManualStop=true\n"
                "\n"
                "[Timer]\n"
                "OnCalendar=%s *-%s-%s %s:%s\n"
                "Unit=cron-%s.service", line, context, dows, mon, dom, h, m, context);

        asprintf(&servicefn, "%s/cron-%s.service", arg_dest, context);
        asprintf(&service,"# Automatically generated by systemd-cron-generator\n"
                "# Was: %s\n"
                "[Unit]\n"
                "Description=[Cron] %s\n"
                "RefuseManualStart=true\n"
                "RefuseManualStop=true\n"
                "\n"
                "[Service]\n"
                "Type=oneshot\n"
                "User=%s\n"
                "ExecStart=/bin/sh -c \"%s\"\n", line, context, user, command);
        if (!timer || !timerfn || !service || !servicefn)
		// OOM
                exit(1);

        r = write_string_file(timerfn, timer);
        if (r < 0)
                return r;

        r = write_string_file(servicefn, service);
        if (r < 0)
                return r;

        return r;
}
*/

struct dict
{
    char *key;
    char *val;
    struct dict *next;
};

typedef struct dict env;

static int parse_crontab(const char *filename, char *usertab) {
        FILE *fp = NULL;
        char line[1024];
        char *p;

        char frequency[11], m[25], h[25], dom[25], mon[25], dow[25], user[65], command[1024];
        int remainder = 0;
        int remainder2 = 0;

        /* fake regexp */
        char *pos_equal;
        char *pos_blank;

        env *head = NULL;
        env *curr = NULL;

        fp = fopen(filename, "r");
        if (!fp)
            return -errno;

        while (fgets(line, sizeof(line), fp)) {
                p = strchr(line, '\n');
                p[0] = '\0';
                switch(line[0]) {
                    case '\0':
                      continue;
                    case '#':
                      continue;
                    case '@':
                      sscanf(line, "%10s %n", frequency, &remainder);
                      strcpy(m, "*");
                      strcpy(h, "*");
                      strcpy(dom, "*");
                      strcpy(mon, "*");
                      strcpy(dow, "*");
                      switch(frequency[1]) {
                          case 'y': // yearly
                          case 'a': // annually
                            mon[0] = '1';
                          case 'm': // monthly
                            dom[0] = '1';
                          case 'w': // weekly
                            if (frequency[1] == 'w')
                                dow[0] = '1';
                          case 'd': // daily
                            h[0] = '0';
                          case 'h': // hourly
                            m[0] = '0';
                      }
                      break;
                    default:
                      pos_equal=strchr(line, '=');
                      pos_blank=strchr(line, ' ');
                      if ((pos_equal != NULL) &&
                          (pos_blank == NULL || pos_equal < pos_blank)) {
                          pos_equal[0]='\0';
                          char *value=pos_equal+1;
                          curr = head;
                          bool found = false;
                          while(curr) {
                              if (strcmp(curr->key, line) == 0) {
                                  curr->val = (char *)realloc(curr->val, strlen(value)+1);
                                  strcpy(curr->val, value);
                                  found = true;
                              }
                              curr = curr->next;
                          }
                          if (!found) {
                              curr = (env *)malloc(sizeof(env));
                              curr->key = (char *)malloc(strlen(line)+1);
                              strcpy(curr->key, line);
                              curr->val = (char *)malloc(strlen(value)+1);
                              strcpy(curr->val, value);
                              curr->next = head;
                              head = curr;
                          }
                          continue;
                      }
                      sscanf(line, "%24s %24s %24s %24s %24s %n", m, h, dom, mon, dow, &remainder);
                }
                if (usertab == NULL)
                    sscanf(&line[remainder], "%64s %n", user, &remainder2);
		else
                    strcpy(user, usertab);

                printf("MINUTE: %s\n", m);
                printf("HOUR: %s\n", h);
                printf("DAY: %s\n", dom);
                printf("MONTH: %s\n", mon);
                printf("DOW: %s\n", dow);
                printf("USER: %s\n", user);
                printf("COMMAND: %s\n", &line[remainder+remainder2]);
                curr = head;
                while(curr) {
                    printf("%s=\"%s\" ", curr->key, curr->val);
                    curr = curr->next;
                }
                printf("\n\n");
        }
        fclose(fp);

        curr = head;
        while(curr) {
                free(curr->key);
                free(curr->val);
                head = curr->next;
                free(curr);
                curr = head;
        }

        return 0;
}

int parse_dir(bool system, const char *dirname) {
	DIR *dirp;
	struct dirent *dent;
        char *fullname = NULL;

	dirp = opendir(dirname);
	if (dirp == NULL) {
		return 0;
	}

	while ((dent = readdir(dirp))) {
                if (dent->d_name[0] == '.') // '.', '..', '.placeholder'
                    continue;
                asprintf(&fullname,"%s/%s", dirname, dent->d_name);
                if (system)
                    parse_crontab(fullname, NULL);
                else
                    parse_crontab(fullname, dent->d_name);
	}
        closedir(dirp);
        free(fullname);
        return 0;
}

int main(int argc, char *argv[]) {
        if (argc > 1)
                arg_dest = argv[1];

        umask(0022);
        parse_crontab("/etc/crontab", NULL);
        parse_dir(true, "/etc/cron.d");
        parse_dir(false, USER_CRONTABS);

        return 0;
}
