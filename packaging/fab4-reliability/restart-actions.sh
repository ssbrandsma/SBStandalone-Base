#!/bin/sh
# Fixed, qualified Fab4 actions. Only the detached native coordinator invokes
# these phases. No USB writes, Music runtime, new ports or watchdog feeders.
set -eu
umask 077
root=/usr/share/jive/applets/StandaloneBase
run=/var/run/standalonebase
backup=/mnt/storage/standalonebase/wp4a2b-backup
helper=$root/sb-maintenance-helper
control=$root/sb-tinysc-control
case "${2:-}:${3:-}" in *[!0-9:]*|:*) exit 64;; esac
case "${1:-}" in stop|start|recover) "$helper" maintenance-active >/dev/null;; esac
inactive()
{
    status=$(wget -q -O - http://127.0.0.1:9000/api/nowplaying)
    printf '%s\n' "$status" | grep -F '"active":false' >/dev/null
    printf '%s\n' "$status" | grep -F '"state":"stopped"' >/dev/null
}
tiny_absent()
{
    state=$($control detect)
    test "$state" = 'count=0 uncertain=0'
    test -f "$run/tinysc.latch"
}
ui_stack()
{
    nonce=$(cat "$run/restart.challenge")
    "$helper" verify-ui "$2" "$3" "$nonce" >/dev/null
    stack=$("$helper" verify-stack "$2" "$3")
    tiny_absent
}
case "$1" in
 preflight)
    test -f /mnt/storage/standalonebase/tinysc-arbitration.enabled
    sh "$backup/check-installed.sh"
    sh "$backup/check-recovery.sh"
    "$helper" verify-stack "$2" "$3" >/dev/null
    inactive
    tiny_absent
    ;;
 stop)
    /etc/init.d/squeezeplay maintenance-stop "$2" "$3"
    ;;
 start)
    # The coordinator forks Jive itself so exit/signal evidence is retained.
    test -f "$run/restart.old"
    ;;
 health)
    ui_stack "$@"
    state=$(wget -q -O - http://127.0.0.1:9000/api/tinysc/lifecycle)
    base_pid=$(printf '%s\n' "$stack" | sed -n 's/.*"base_pid":\([0-9][0-9]*\).*/\1/p')
    base_start=$(printf '%s\n' "$stack" | sed -n 's/.*"base_start":"\([0-9][0-9]*\)".*/\1/p')
    test -n "$base_pid" && test -n "$base_start"
    printf '%s\n' "$state" | grep -F '"actor":"sbbase"' >/dev/null
    printf '%s\n' "$state" | grep -F "\"pid\":$base_pid," >/dev/null
    printf '%s\n' "$state" | grep -F "\"start\":\"$base_start\"" >/dev/null
    printf '%s\n' "$state" | grep -F '"enabled":true' >/dev/null
    printf '%s\n' "$state" | grep -F '"operational":true' >/dev/null
    printf '%s\n' "$state" | grep -F '"fresh":true' >/dev/null
    printf '%s\n' "$state" | grep -F '"state":"STOPPED_VERIFIED"' >/dev/null
    printf '%s\n' "$state" | grep -F '"count":0' >/dev/null
    boot=$(cat /proc/sys/kernel/random/boot_id)
    printf '%s\n' "$state" | grep -F "\"boot\":\"$boot\"" >/dev/null
    ;;
 recover)
    if test -f "$run/restart.new"; then "$helper" quiesce-new
    else "$helper" recover-old; fi
    sh "$backup/recover-base.sh"
    ;;
 health-recovery)
    ui_stack "$@"
    inactive
    ;;
 *) exit 64;;
esac
