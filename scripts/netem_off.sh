#!/bin/sh
IF=${1:-wlan0}; tc qdisc del dev "$IF" root 2>/dev/null; tc qdisc del dev "$IF" ingress 2>/dev/null; tc qdisc del dev ifb0 root 2>/dev/null; echo "netem OFF on $IF"
