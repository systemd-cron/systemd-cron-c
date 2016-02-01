/***
  Copyright 2013 Shawn Landden:
                 - original implementation in C posted on
                   systemd mailing list
                 - parse_dow() re-used here almost as-is
            2014 Konstantin Stepanov,
                 Dwayne Bent,
                 Daniel Schaal,
                 Alexandre Detiste
                 - polished Python version packaged for
                   Arch, Debian, Gentoo...

                 The project is licensed under MIT.

            2015 Alexandre Detiste - maintenance
            2015 Konstantin Stepanov - rewrite in Rust
            2016 Alexandre Detiste - _this_ C rewrite
                 that re-implements the logic in the Python version
                 with some extra ideas from the Rust version
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
#include <pwd.h>
#include <bsd/md5.h>

#ifndef USER_CRONTABS
#define USER_CRONTABS "/var/spool/cron/crontabs"
#endif
// or "/var/spool/cron"

#ifndef PREFIX
#define PREFIX ""
#endif
// or "/usr"

// do not re-run @reboot jobs
// when switching from/to Vixie-Cron
#define REBOOT_FILE "/run/crond.reboot"

static const char *arg_dest = "/tmp";
bool debug = false;

const char *daysofweek[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
const char *isdow = "0123456";

void log_msg(int level, char *message, char *message2) {
    FILE *out;

    if (debug)
        out = stderr;
    else if ((out = fopen("/dev/kmsg", "w")))
        fprintf(out, "<%d> ", level);
    else
        out = stderr;

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

struct text_dict* text_dict_set(struct text_dict *head, char *key, char *value) {
    struct text_dict *curr = NULL;
    bool found = false;

    curr = head;
    while(curr) {
        if (strcmp(curr->key, key) == 0) {
            curr->val = (char *)realloc(curr->val, strlen(value)+1);
            strcpy(curr->val, value);
            found = true;
            break;
        }
        curr = curr->next;
    }

    if (!found) {
        curr = (env *)malloc(sizeof(env));
        curr->key = (char *)malloc(strlen(key)+1);
        strcpy(curr->key, key);
        curr->val = (char *)malloc(strlen(value)+1);
        strcpy(curr->val, value);
        curr->next = head;
        head = curr;
    }
    return head;
}

struct int_dict
{
    char *key;
    int val;
    struct int_dict *next;
};
typedef struct int_dict sequence;

void compress_blanks(char *string) {
    for (int i = 0; string[i]; i++)
        if (string[i] == '\t')
            string[i] = ' ';

    int count = 0;

    for (int i = 0; string[i]; i++)
        if (!(string[i] == ' ' && string[i+1] == ' '))
            string[count++] = string[i];

    string[count] = '\0';
}


bool str_to_bool(char *string) {
    for (int i=0; string[i]; i++)
        string[i] = tolower((unsigned char)string[i]);
    return !strcmp(string,"true") ||
           !strcmp(string,"yes") ||
           !strcmp(string,"1");
}

static int parse_crontab(const char *dirname, const char *filename, char *usertab, bool anacrontab) {
        char *fullname;
        asprintf(&fullname, "%s/%s", dirname, filename);

        FILE *fp = NULL;
        char line[1024];
        char shell[20] = "/bin/sh";
        char *p;

        char frequency[11], m[25], h[25], dom[25], mon[25], dow[25], user[65];
        char dows[128];
        char *schedule;
        bool persistent = anacrontab;
        bool batch = false;
        bool reboot = false;
        int delay = 0;
        char jobid[25];

        char *command;
        int skipped = 0;

        /* fake regexp */
        char *pos_equal;
        char *pos_blank;

        env *head = NULL;
        env *curr = NULL;

        /* out */
        char *timers_dir = NULL;
        sequence *seq_head = NULL;
        sequence *seq_curr = NULL;
        FILE *outp = NULL;
        char *unit = NULL;
        char *outf = NULL;

        fp = fopen(fullname, "r");
        if (!fp) {
            log_msg(3, "cannot read ", fullname);
            free(fullname);
            return -errno;
        }

        asprintf(&timers_dir, "%s/cron.target.wants", arg_dest);

        while (fgets(line, sizeof(line), fp)) {
                p = strchr(line, '\n');
                p[0] = '\0';
                compress_blanks(line);
                schedule = NULL;
                reboot = false;
                switch(line[0]) {
                    case '\0':
                      continue;
                    case '#':
                      continue;
                    case '@':
                      sscanf(line, "%10s %n", frequency, &skipped);
                      command = line + skipped;
                      if(!strcmp(frequency,"@minutely") ||
                         !strcmp(frequency,"@hourly") ||
                         !strcmp(frequency,"@daily") ||
                         !strcmp(frequency,"@weekly") ||
                         !strcmp(frequency,"@monthly") ||
                         !strcmp(frequency,"@quarterly") ||
                         !strcmp(frequency,"@semiannually") ||
                         !strcmp(frequency,"@yearly")) {
                             schedule = strdup(&frequency[1]);
                      } else if (!strcmp(frequency,"@midnight")) {
                             schedule = strdup("daily");
                      } else if (!strcmp(frequency, "@biannually") ||
                                 !strcmp(frequency, "@bi-annually") ||
                                 !strcmp(frequency, "@semi-annually")) {
                             schedule = strdup("semiannually");
                      } else if (!strcmp(frequency,"@anually") ||
                                 !strcmp(frequency,"@annually")) {
                             schedule = strdup("yearly");
                      } else if (!strcmp(frequency,"@reboot")) {
                             struct stat sb;
                             if (stat(REBOOT_FILE, &sb) != -1)
                                 continue;
                             schedule = strdup(&frequency[1]);
                             reboot = true;
                      } else {
                             log_msg(3, "garbled time: ", line);
                             continue;
                      }
                      if(anacrontab) {
                             sscanf(command, "%4d %24s %n", &delay, jobid, &skipped);
                             command += skipped;
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

                          if(strcmp("DELAY", line) == 0) {
                              if(!sscanf(value, "%d", &delay)) {
                                  log_msg(4, "cannot read DELAY: ", value);
                                  delay = 0;
                              }
                              continue;
                          }

                          if(strcmp("PERSISTENT", line) == 0) {
                              persistent = str_to_bool(value);
                              continue;
                          }

                          if(strcmp("BATCH", line) == 0) {
                              batch = str_to_bool(value);
                              continue;
                          }

                          if(strcmp("SHELL", line) == 0) {
                              if(strlen(value) > (sizeof(shell)-1)) {
                                  log_msg(3, "bad SHELL, ingnoring: ", value);
                                  continue;
                              }
                              strncpy(shell, value, sizeof(shell)-1);
                          }

                          head = text_dict_set(head, line, value);

                          continue;
                      }
                      if(anacrontab) {
                          int days;
                          sscanf(line, "%4d %4d %24s %n", &days, &delay, jobid, &skipped);
                          command = line + skipped;
                          switch(days) {
                              case(1):
                                schedule = strdup("daily");
                                break;
                              case(7):
                                schedule = strdup("weekly");
                                break;
                              case(30):
                                schedule = strdup("monthly");
                                break;
                              case(31):
                                schedule = strdup("monthly");
                                break;
                              default:
                                log_msg(3, "unsupported anacrontab", line);
                                continue;
                          }
                      } else {
                          sscanf(line, "%24s %24s %24s %24s %24s %n", m, h, dom, mon, dow, &skipped);
                          command = line + skipped;
                      }
                }
                if (usertab == NULL) {
                    sscanf(command, "%64s %n", user, &skipped);
                    command += skipped;
                } else
                    strcpy(user, usertab);

                if (schedule == NULL) {
                    parse_dow(dow, &dows[0]);
                    void expand_range(char* var) {
                        if(!strchr(var, '-'))
                            return;
                        char tmp[25];
                        strncpy(tmp, var, 24);
                        int start, end;
                        sscanf(strtok(tmp, "-"), "%d", &start);
                        sscanf(strtok(NULL, "-"), "%d", &end);
                        sprintf(var, "%d", start);
                        for(int i=start+1; i <= end; i++)
                             sprintf(var + strlen(var),",%d", i);
                    }
                    expand_range(mon);
                    expand_range(dom);
                    expand_range(h);
                    expand_range(m);
                    asprintf(&schedule, "%s*-%s-%s %s:%s:00", dows, mon, dom, h, m);
                } else if (delay) {
                    char *delayed_schedule = NULL;
                    if (!strcmp(schedule, "hourly"))
                        asprintf(&delayed_schedule, "*-*-* *:%d:0", delay);
                    else if (!strcmp(schedule, "daily"))
                        asprintf(&delayed_schedule, "*-*-* 0:%d:0", delay);
                    else if (!strcmp(schedule, "weekly"))
                        asprintf(&delayed_schedule, "Mon *-*-* 0:%d:0", delay);
                    else if (!strcmp(schedule, "monthly"))
                        asprintf(&delayed_schedule, "*-*-1 0:%d:0", delay);
                    else if (!strcmp(schedule, "quarterly"))
                        asprintf(&delayed_schedule, "*-1,4,7,10-1 0:%d:0", delay);
                    else if (!strcmp(schedule, "semiannually"))
                        asprintf(&delayed_schedule, "*-1,7-1 0:%d:0", delay);
                    else if (!strcmp(schedule, "yearly"))
                        asprintf(&delayed_schedule, "*-1-1 0:%d:0", delay);
                    if(delayed_schedule) {
                        free(schedule);
                        schedule = delayed_schedule;
                    }
                }

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
                    if (anacrontab) {
                        int count = 0;
                        for (int i = 0; jobid[i]; i++)
                            if (('a' <= jobid[i] && jobid[i] <= 'z') ||
                                ('A' <= jobid[i] && jobid[i] <= 'Z') ||
                                ('0' <= jobid[i] && jobid[i] <= '9'))
                                jobid[count++] = jobid[i];
                        jobid[count] = '\0';
                        asprintf(&unit, "cron-%s-%s-%s", jobid, user, md5);
                    } else
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
                if(outp == NULL) {
                    log_msg(3, "Couldn't create output, aborting: ", outf);
                    exit(1);
                }
                fputs("[Unit]\n", outp);
                fprintf(outp, "Description=[Timer] \"%s\"\n", line);
                fputs("Documentation=man:systemd-crontab-generator(8)\n", outp);
                fputs("PartOf=cron.target\n", outp);
                fputs("RefuseManualStart=true\n", outp);
                fputs("RefuseManualStop=true\n", outp);
                fprintf(outp, "SourcePath=%s\n\n", fullname);

                fputs("[Timer]\n", outp);
                fprintf(outp, "Unit=%s.service\n", unit);
                if(reboot)
                    fputs("OnBootSec=1m\n", outp);
                else
                    fprintf(outp, "OnCalendar=%s\n", schedule);
                if (persistent)
                    fputs("Persistent=true\n", outp);
                fclose(outp);

                mkdir(timers_dir, S_IRUSR | S_IWUSR | S_IXUSR);
                char *link;
                asprintf(&link, "%s/%s.timer", timers_dir, unit);
                symlink(outf, link);
                free(link);

                free(outf);
                asprintf(&outf, "%s/%s.service", arg_dest, unit);
                outp = fopen(outf, "w");
                fputs("[Unit]\n", outp);
                fprintf(outp, "Description=[Cron] \"%s\"\n", line);
                fputs("Documentation=man:systemd-crontab-generator(8)\n", outp);
                fputs("RefuseManualStart=true\n", outp);
                fputs("RefuseManualStop=true\n", outp);
                fprintf(outp, "SourcePath=%s\n", fullname);
                if ((usertab && !anacrontab) || strcmp(user, "root")) {
                    fputs("Requires=systemd-user-sessions.service\n", outp);
                    struct passwd *pwd;
                    pwd = getpwnam(user);
                    fprintf(outp, "RequiresMountsFor=%s\n", pwd->pw_dir);
                }
                fputs("\n", outp);

                fputs("[Service]\n", outp);
                fputs("Type=oneshot\n", outp);
                fputs("IgnoreSIGPIPE=false\n", outp);
                if (!reboot && delay)
                    fprintf(outp, "ExecStartPre=-" PREFIX "/lib/systemd-cron/boot_delay %d\n", delay);
                struct stat sb;
                if (stat(command, &sb) != -1)
                    fprintf(outp, "ExecStart=%s\n", command);
                else {
                    char* script;
                    asprintf(&script, "%s/%s.sh", arg_dest, unit);
                    fprintf(outp, "ExecStart=%s %s\n", shell, script);
                    FILE *sh;
                    sh = fopen(script, "w");
                    fprintf(sh, "%s\n", command);
                    fclose(sh);
                    free(script);
                }

                if (head) {
                    fputs("Environment=", outp);
                    curr = head;
                    while(curr) {
                        if (strlen(curr->val) == 0)
                            {}
                        else if (strchr(curr->val, ' '))
                            fprintf(outp, "\"%s=%s\" ", curr->key, curr->val);
                        else
                            fprintf(outp, "%s=%s ", curr->key, curr->val);
                        curr = curr->next;
                    }
                    fputs("\n", outp);
                }

                if (strcmp(user, "root")) {
                    fprintf(outp, "User=%s\n", user);
                }
                if (batch) {
                    fputs("CPUSchedulingPolicy=idle\n", outp);
                    fputs("IOSchedulingClass=idle\n", outp);
                }

                fclose(outp);

                free(schedule);
                free(unit);
                free(outf);
        }
        free(fullname);
        free(timers_dir);
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
                        log_msg(5, "ignoring /etc/cron.d/", dent->d_name);
                        continue;
                    }
                    struct stat sb;
                    char *sys_unit;
                    char *etc_unit;
                    asprintf(&sys_unit, PREFIX "/lib/systemd/system/%s.timer", dent->d_name);
                    asprintf(&etc_unit, "/etc/systemd/system/%s.timer", dent->d_name);
                    bool native = (stat(sys_unit, &sb) != -1) || (stat(etc_unit, &sb) != -1);
                    free(sys_unit);
                    free(etc_unit);
                    if (native) {
                        log_msg(5, "ignoring because native timer is present: /etc/cron.d/", dent->d_name);
                        continue;
                    }
                    parse_crontab(dirname, dent->d_name, NULL, false);
                } else
                    parse_crontab(dirname, dent->d_name, dent->d_name, false);
	}
        closedir(dirp);
        return 0;
}

int main(int argc, char *argv[]) {
        if (argc > 1)
                arg_dest = argv[1];
        else
                debug = true;

        struct stat sb;
        if (stat(arg_dest, &sb) == -1) {
            fprintf(stderr, "%s doesn't exist.\n", arg_dest);
            exit(1);
        }

        umask(0022);
        parse_crontab("/etc", "crontab", NULL, false);
        parse_crontab("/etc", "anacrontab", "root", true);
        parse_dir(true, "/etc/cron.d");

        if (stat(USER_CRONTABS, &sb) != -1) {
            // /var is available
            parse_dir(false, USER_CRONTABS);
            close(open(REBOOT_FILE, O_CREAT));
        } else {
            // schedule rerun
            char *unit;
            asprintf(&unit, "%s/cron-after-var.service", arg_dest);
            FILE *f;
            f = fopen(unit, "w");
            fputs("[Unit]\n", f);
            fputs("Description=Rerun systemd-crontab-generator because /var is a separate mount\n", f);
            fputs("After=cron.target\n", f);
            fputs("ConditionDirectoryNotEmpty=" USER_CRONTABS "\n", f);

            fputs("\n[Service]\n", f);
            fputs("Type=oneshot\n", f);
            fputs("ExecStart=/bin/sh -c '" PREFIX "/bin/systemctl daemon-reload ; "
                                           PREFIX "/bin/systemctl try-restart cron.target'\n", f);
            fclose(f);

            char *dir;
            asprintf(&dir, "%s/multi-user.target.wants", arg_dest);
            mkdir(dir, S_IRUSR | S_IWUSR | S_IXUSR);
            free(dir);

            char *link;
            asprintf(&link, "%s/multi-user.target.wants/cron-after-var.service", arg_dest);
            symlink(unit, link);
            free(link);
            free(unit);
        }

        return 0;
}
