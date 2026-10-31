#!/bin/sh
# Read-only; stream over SSH with sh -s. No probe writes or service changes.
for f in /proc/cpuinfo /proc/version /etc/squeezeos.version /proc/meminfo /proc/mounts /proc/net/dev /proc/net/tcp /proc/net/tcp6 /proc/net/udp /proc/net/udp6; do
 echo "=== $f"; cat "$f" 2>/dev/null || true
done
for d in /mnt/storage /mnt/sbdata /media /media/sda1 /media/mmcblk0p1; do
 if test -d "$d"; then
  echo "directory=$d writable_by_permissions=$(test -w "$d" && echo yes || echo no)"
 fi
done
netstat -lntu 2>/dev/null || true
ps
for d in /proc/[0-9]*; do
 cmd=$(tr '\000' ' ' < "$d/cmdline" 2>/dev/null) || continue
 case "$cmd" in *squeezecenter*|*slimserver*|*tinysc*|*sbbase*|*sbproxy*|*sbwebserver*)
  echo "$d $cmd"; ls -l "$d/fd" 2>/dev/null || true ;;
 esac
done
for f in /sys/class/graphics/fb*/virtual_size /sys/class/graphics/fb*/bits_per_pixel; do
 echo "$f"; cat "$f" 2>/dev/null || true
done
ls /etc/init.d/*squeeze* /etc/init.d/*slim* /etc/init.d/*tiny* 2>/dev/null || true
