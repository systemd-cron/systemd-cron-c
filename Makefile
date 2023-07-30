CFLAGS ?= -g -Wall

all: systemd-crontab-generator boot_delay mail_on_failure remove_stale_stamps

%: %.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) $< -o $@

systemd-crontab-generator: systemd-crontab-generator.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) systemd-crontab-generator.c -l md -o systemd-crontab-generator

install:
	install -D -m 0755 systemd-crontab-generator  $(DESTDIR)/usr/lib/systemd/system-generators/systemd-crontab-generator
	install -D -m 0755 boot_delay                 $(DESTDIR)/usr/libexec/systemd-cron/boot_delay
	install -D -m 0755 mail_on_failure            $(DESTDIR)/usr/libexec/systemd-cron/mail_on_failure
	install -D -m 0755 remove_stale_stamps        $(DESTDIR)/usr/libexec/systemd-cron/remove_stale_stamps

clean:
	rm -f systemd-crontab-generator boot_delay mail_on_failure remove_stale_stamps
