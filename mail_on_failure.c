#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <string.h>

char* read_systemd_variable(const char *unit, const char *property) {
	int filedes[2];
	if (pipe(filedes) == -1) {
		perror("pipe");
		exit(1);
	}

	pid_t pid = fork();
	if (pid == -1) {
		perror("fork");
		exit(1);
	} else if (pid == 0) {
		while ((dup2(filedes[1], STDOUT_FILENO) == -1) && (errno == EINTR)) {}
		close(filedes[1]);
		close(filedes[0]);
		char *arg;
		asprintf(&arg, "--property=%s", property);
		execl("/usr/bin/systemctl", "systemctl", "show", unit, arg, NULL);
		perror("execl");
		_exit(1);
	}
	close(filedes[1]);
	char buffer[4096];
	memset(buffer, '\0', sizeof(buffer));
	read(filedes[0], buffer, sizeof(buffer));
	close(filedes[0]);
	wait(0);

	char *result;
	asprintf(&result, "%s", buffer + strlen(property) + 1);
	result[strlen(result)-1] = '\0';
	return result;
}

int main(int argc, char *argv[]) {
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <unit>\n", argv[0]);
		exit(1);
	}
        struct stat sb;
        if (stat("/usr/sbin/sendmail", &sb) == -1) {
		fprintf(stderr, "<3>can't send error mail for %s without a MTA\n", argv[1]);
		exit(1);
	}

	char *mailto = NULL;

	char *environment;
	environment = read_systemd_variable(argv[1], "Environment");
	printf("[%s]\n", environment);
	if (strlen(environment)) {
		// leak
		mailto = strstr(environment, "MAILTO=");
		if (mailto) {
			mailto += strlen("MAILTO=");

			char *p;
                        p = strchr(mailto, ' ');
			if (p)
			    p[0] = '\0';

			if (!strlen(mailto)) {
				free(environment);
				exit(0);
			}
		}
	}

	if (!mailto) {
		mailto = read_systemd_variable(argv[1], "User");
		if(!strlen(mailto)) {
			free(mailto);
			mailto = strdup("root");
		}
	}
	printf("Mailto: [%s]\n", mailto);

/*
hostname = os.uname()[1]

body = "From: root (systemd-cron)\n"
body += "To: " + mailto + "\n"
body += "Subject: [" + hostname + "] job " + job  + " failed\n"
body += "MIME-Version: 1.0\n"
body += "Content-Type: text/plain; charset=UTF-8\n"
body += "Content-Transfer-Encoding: 8bit\n"
body += "Auto-Submitted: auto-generated\n"
body += "\n"

try:
    systemctl = subprocess.check_output(['systemctl','status',job], universal_newlines=True)
except subprocess.CalledProcessError as e:
    if e.returncode != 3:
        raise
    else:
        body += e.output

p = Popen(['sendmail','-i','-B8BITMIME',mailto], stdin=PIPE)
p.communicate(bytes(body, 'UTF-8'))
*/

	free(environment);
}
