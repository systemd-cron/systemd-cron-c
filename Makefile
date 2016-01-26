# use 'make prefix=/usr' on UsrMerge'd distros
prefix ?=
CFLAGS ?= -Wall

all: systemd-cron-generator

systemd-cron-generator: systemd-cron-generator.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) systemd-cron-generator.c -o systemd-cron-generator

install:
	install -D -m 0755 systemd-cron-generator  $(DESTDIR)$(prefix)/lib/systemd/system-generators/systemd-crontab-generator

clean:
	rm -f systemd-cron-generator
