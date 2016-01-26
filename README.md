systemd-cron-c
==============

## What's it?

This is a compatibility layer for crontab-to-systemd timers framework. It works by parsing
crontab and anacrontab files from usual places like `/etc/crontab` and `/var/spool/cron`
and generating systemd timers and services. You can use `cron.target` as a single control
point for the generated units.

It's intented to be drop-in replacement for all cron implementations.

## More

I'm mostly interrested in learning C at a slow pace;
so users are better off using the supported [Python][]
version or the [Rust][] rewrite for now.

[Python]: https://github.com/systemd-cron/systemd-cron
[Rust]: https://github.com/systemd-cron/systemd-cron-next
