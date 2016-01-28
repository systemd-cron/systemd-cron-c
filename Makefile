# use 'make prefix=/usr' on UsrMerge'd distros
prefix ?=
CFLAGS ?= -g -Wall

all: systemd-crontab-generator

systemd-crontab-generator: systemd-crontab-generator.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) systemd-crontab-generator.c -l bsd -o systemd-crontab-generator

install:
	install -D -m 0755 systemd-crontab-generator  $(DESTDIR)$(prefix)/lib/systemd/system-generators/systemd-crontab-generator

clean:
	rm -f systemd-crontab-generator
