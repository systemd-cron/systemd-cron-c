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
#include <ctype.h>
#include <bsd/md5.h>

#ifndef USER_CRONTABS
#define USER_CRONTABS "/var/spool/cron/crontabs"
#endif

#ifndef UNITDIR
#define UNITDIR "/lib/systemd/system"
#endif

static const char *arg_dest = "/tmp";
bool debug = false;

const char *daysofweek[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
const char *isdow = "0123456";

void syslog(int level, char *message, char *message2) {
    FILE *out;

    out = fopen("/dev/kmsg", "w");
    if (out == NULL || debug)
        out = stderr;
    else
        fprintf(out, "<%d> ", level);

    fprintf(out, "systemd-crontab-generator[%d]: %s", getpid(), message);

    if (message2 != NULL)
        fprintf(out, "%s", message2);

    fprintf(out, "\n");
    fflush(out);

    if (out != stderr)
        fclose(out);
}

static int parse_dow(char *dow, char *dows){
        char c[2] = {"\0\0"};

        dows[0] = '\0';

        for(unsigned s = 0;s < strlen(dow);s++) {
                c[0] = dow[s];
                if (strchr(isdow, c[0]))
                        strncpy(dows + strlen(dows), daysofweek[atoi(&c[0])], 128 - strlen(dows));
                else if (c[0] == '*')
                        continue;
                else {
                        int l = strlen(dows);
                        if (l < 128) {
                                dows[l] = c[0];
                                dows[l+1] = '\0';
                        }
                }
        }

        int l = strlen(dows);
        if (l) {
                dows[l] = ' ';
                dows[l+1] = '\0';
        }

        return 0;
}

struct text_dict
{
    char *key;
    char *val;
    struct text_dict *next;
};
typedef struct text_dict env;

struct int_dict
{
    char *key;
    int val;
    struct int_dict *next;
};
typedef struct int_dict sequence;

void compress_blanks(char *string) {
    int count = 0;

    for (int i = 0; string[i]; i++)
        if (!(string[i] == ' ' && string[i+1] == ' '))
            string[count++] = string[i];

    string[count] = '\0';
}

static int parse_crontab(const char *dirname, const char *filename, char *usertab) {
        char *fullname;
        asprintf(&fullname, "%s/%s", dirname, filename);

        FILE *fp = NULL;
        char line[1024];
        char *p;

        char frequency[11], m[25], h[25], dom[25], mon[25], dow[25], user[65];
        char dows[128];
        char *schedule;
        bool persistent = false;

        int remainder = 0;
        int remainder2 = 0;
        char *command;

        /* fake regexp */
        char *pos_equal;
        char *pos_blank;

        env *head = NULL;
        env *curr = NULL;

        /* out */
        sequence *seq_head = NULL;
        sequence *seq_curr = NULL;
        FILE *outp = NULL;
        char *unit = NULL;
        char *outf = NULL;

        fp = fopen(fullname, "r");
        if (!fp) {
            syslog(3, "cannot read ", fullname);
            free(fullname);
            return -errno;
        }

        while (fgets(line, sizeof(line), fp)) {
                p = strchr(line, '\n');
                p[0] = '\0';
                compress_blanks(line);
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

                          // lstrip
                          while (value[0] == '"' || value[0] == '\''){
                              value++;
                          }

                          // rstrip
                          for(int i=strlen(value)-1; i>0; i--) {
                              if (value[i] == '"' || value[i] == '\'')
                                  value[i] = '\0';
                              else
                                  break;
                          }

                          if(strcmp("PERSISTENT", line) == 0) {
                              for (int i=0; value[i]; i++)
                                   value[i] = tolower((unsigned char)value[i]);
                              persistent = (!strcmp(value,"true") ||
                                            !strcmp(value,"yes") ||
                                            !strcmp(value,"1"));
                              continue;
                          }

                          curr = head;
                          bool found = false;
                          while(curr) {
                              if (strcmp(curr->key, line) == 0) {
                                  curr->val = (char *)realloc(curr->val, strlen(value)+1);
                                  strcpy(curr->val, value);
                                  found = true;
                                  break;
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
                command = &line[remainder+remainder2];

                parse_dow(dow, &dows[0]);
                asprintf(&schedule, "%s*-%s-%s %s:%s:00", dows, mon, dom, h, m);
                if (persistent) {
                    unsigned char digest[16];
                    MD5_CTX context;
                    MD5Init(&context);
                    MD5Update(&context, (unsigned char *)schedule, strlen(schedule)+1);
                    MD5Update(&context, (unsigned char *)command, strlen(command));
                    MD5Final(digest, &context);
                    char md5[33];
                    for(int i = 0; i < 16; ++i)
                        sprintf(&md5[i*2], "%02x", (unsigned int)digest[i]);
                    asprintf(&unit, "cron-%s-%s-%s", filename, user, md5);
                } else {
                    seq_curr = seq_head;
                    bool found = false;
                    while(seq_curr) {
                        if (strcmp(seq_curr->key, user) == 0) {
                            seq_curr->val++;
                            found = true;
                            break;
                        }
                        seq_curr = seq_curr->next;
                    }
                    if (!found) {
                        seq_curr = (sequence *)malloc(sizeof(sequence));
                        seq_curr->key = (char *)malloc(strlen(user)+1);
                        strcpy(seq_curr->key, user);
                        seq_curr->val = 0;
                        seq_curr->next = seq_head;
                        seq_head = seq_curr;
                    }
                    asprintf(&unit, "cron-%s-%s-%d", filename, user, seq_curr->val);
                }

                asprintf(&outf, "%s/%s.timer", arg_dest, unit);
                outp = fopen(outf, "w");
                fputs("[Unit]\n", outp);
                fprintf(outp, "Description=[Timer] \"%s\"\n", line);
                fputs("Documentation=man:systemd-crontab-generator(8)\n", outp);
                fputs("PartOf=cron.target\n", outp);
                fputs("RefuseManualStart=true\n", outp);
                fputs("RefuseManualStop=true\n", outp);
                fprintf(outp, "SourcePath=%s\n\n", fullname);

                fputs("[Timer]\n", outp);
                fprintf(outp, "Unit=%s.service\n", unit);
                fprintf(outp, "OnCalendar=%s\n", schedule);
                if (persistent)
                    fputs("Persistent=true\n", outp);
                fclose(outp);

                free(outf);
                asprintf(&outf, "%s/%s.service", arg_dest, unit);
                outp = fopen(outf, "w");
                fputs("[Unit]\n", outp);
                fprintf(outp, "Description=[Cron] \"%s\"\n", line);
                fputs("Documentation=man:systemd-crontab-generator(8)\n", outp);
                fputs("RefuseManualStart=true\n", outp);
                fputs("RefuseManualStop=true\n", outp);
                fprintf(outp, "SourcePath=%s\n", fullname);
                if (usertab || strcmp(user, "root")) {
                    fputs("Requires=systemd-user-sessions.service\n", outp);
                    fprintf(outp, "RequiresMountsFor=/home/%s\n", user);
                    // XXX: home = pwd.getpwnam(user).pw_dir
                }
                fputs("\n", outp);

                fputs("[Service]\n", outp);
                fputs("Type=oneshot\n", outp);
                fputs("IgnoreSIGPIPE=false\n", outp);
                struct stat sb;
                if (stat(command, &sb) != -1)
                    fprintf(outp, "ExecStart=%s\n", command);
                else
                    fprintf(outp, "ExecStart=/bin/sh -c \"%s\"\n", command);

                if (head) {
                    fputs("Environment=", outp);
                    curr = head;
                    while(curr) {
                        if (strlen(curr->val) == 0)
                            {}
                        else if (strchr(curr->val, ' '))
                            fprintf(outp, "%s=\"%s\" ", curr->key, curr->val);
                        else
                            fprintf(outp, "%s=%s ", curr->key, curr->val);
                        curr = curr->next;
                    }
                    fputs("\n", outp);
                }

                if (strcmp(user, "root")) {
                    fprintf(outp, "User=%s\n", user);
                }

                fclose(outp);

                free(schedule);
                free(unit);
                free(outf);
        }
        free(fullname);
        fclose(fp);

        curr = head;
        while(curr) {
                free(curr->key);
                free(curr->val);
                head = curr->next;
                free(curr);
                curr = head;
        }

        seq_curr = seq_head;
        while(seq_curr) {
                free(seq_curr->key);
                seq_head = seq_curr->next;
                free(seq_curr);
                seq_curr = seq_head;
        }

        return 0;
}

int parse_dir(bool system, const char *dirname) {
	DIR *dirp;
	struct dirent *dent;

	dirp = opendir(dirname);
	if (dirp == NULL) {
		return 0;
	}

	while ((dent = readdir(dirp))) {
                if (dent->d_name[0] == '.') // '.', '..', '.placeholder'
                    continue;
                if (system) {
                    if (strstr(dent->d_name, ".dpkg-") != NULL) {
                        syslog(5, "ignoring /etc/cron.d/", dent->d_name);
                        continue;
                    }
                    struct stat sb;
                    char *sys_unit;
                    char *etc_unit;
                    asprintf(&sys_unit, "%s/%s.timer", UNITDIR, dent->d_name);
                    asprintf(&etc_unit, "/etc/systemd/system/%s.timer", dent->d_name);
                    bool native = (stat(sys_unit, &sb) != -1) || (stat(etc_unit, &sb) != -1);
                    free(sys_unit);
                    free(etc_unit);
                    if (native) {
                        syslog(5, "ignoring because native timer is present: /etc/cron.d/", dent->d_name);
                        continue;
                    }
                    parse_crontab(dirname, dent->d_name, NULL);
                } else
                    parse_crontab(dirname, dent->d_name, dent->d_name);
	}
        closedir(dirp);
        return 0;
}

int main(int argc, char *argv[]) {
        if (argc > 1)
                arg_dest = argv[1];
        else
                debug = true;

        umask(0022);
        parse_crontab("/etc", "crontab", NULL);
        parse_dir(true, "/etc/cron.d");
        parse_dir(false, USER_CRONTABS);

        return 0;
}
