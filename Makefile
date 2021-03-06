# use 'make prefix=/usr' on UsrMerge'd distros
prefix ?=
CFLAGS ?= -g -Wall

all: systemd-crontab-generator boot_delay mail_on_failure remove_stale_stamps

%: %.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) $< -o $@

systemd-crontab-generator: systemd-crontab-generator.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) systemd-crontab-generator.c -l bsd -o systemd-crontab-generator

install:
	install -D -m 0755 systemd-crontab-generator  $(DESTDIR)$(prefix)/lib/systemd/system-generators/systemd-crontab-generator
	install -D -m 0755 boot_delay                 $(DESTDIR)$(prefix)/lib/systemd-cron/boot_delay
	install -D -m 0755 mail_on_failure            $(DESTDIR)$(prefix)/lib/systemd-cron/mail_on_failure
	install -D -m 0755 remove_stale_stamps        $(DESTDIR)$(prefix)/lib/systemd-cron/remove_stale_stamps

clean:
	rm -f systemd-crontab-generator boot_delay mail_on_failure remove_stale_stamps
