#!/bin/sh
# Pi: shape a degraded link on interface $1 in BOTH directions (egress + ingress via ifb0). Needs root + tc.
# usage: sudo ./netem_on.sh wlan0 "delay 20ms 10ms loss 5%"      (other examples: "delay 5ms 2ms loss 10% 25%"  = bursty)
IF=${1:-wlan0}; SPEC=${2:-delay 20ms 10ms loss 5%}
modprobe ifb numifbs=1; ip link set ifb0 up
tc qdisc replace dev "$IF" root netem $SPEC
tc qdisc replace dev "$IF" handle ffff: ingress
tc filter replace dev "$IF" parent ffff: protocol ip u32 match u32 0 0 action mirred egress redirect dev ifb0
tc qdisc replace dev ifb0 root netem $SPEC
echo "netem ON on $IF (both directions): $SPEC"; tc qdisc show dev "$IF"; tc qdisc show dev ifb0
