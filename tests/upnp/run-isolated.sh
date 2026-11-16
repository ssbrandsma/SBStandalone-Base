#!/bin/sh
set -eu
test "$(readlink /proc/self/ns/net)" != "$(readlink /proc/1/ns/net)" || {
 echo 'Refusing to change routes outside a private network namespace' >&2; exit 2;
}
ip link set lo up
ip link add upnpfixture0 type dummy
ip address add 192.168.77.2/24 dev upnpfixture0
ip link set upnpfixture0 up multicast on
ip route add default dev upnpfixture0
export RES_OPTIONS=attempts:1
exec "$@"
