#!/bin/sh
# Peanuts Native: one-time setup that lets the launcher hold the CPU and GPU at their highest clocks while a game runs, and put them back afterwards,
# without asking for a password each time. Installs /etc/peanuts-native/pn-clocks (on | off) and a sudoers rule that allows only that helper, for you.
#   sudo sh install-max-clocks.sh            install (Peanuts Native → Settings → Max clocks runs this for you)
#   sudo sh install-max-clocks.sh --remove   remove both
set -e
[ "$(id -u)" = 0 ] || { echo "run it with sudo: sudo sh $0"; exit 1; }
[ ! -f /run/.containerenv ] || { echo "run it on the host, not inside a toolbox or distrobox (type exit first)"; exit 1; }
helper=/etc/peanuts-native/pn-clocks rule=/etc/sudoers.d/peanuts-native-clocks
if [ "$1" = --remove ]; then
    [ -x "$helper" ] && "$helper" off || true
    rm -f "$helper" "$rule"; echo "removed $helper and $rule"; exit 0
fi
user="${PN_USER:-${SUDO_USER:-${PKEXEC_UID:+$(id -nu "$PKEXEC_UID")}}}"
[ -n "$user" ] && [ "$user" != root ] || { echo "run it with sudo (or pkexec) from your own account"; exit 1; }
mkdir -p /etc/peanuts-native   # /etc: /usr and /usr/local are read-only on ArmadaOS and other atomic systems
cat > "$helper" <<'H'
#!/bin/sh
# Peanuts Native: CPU and GPU clocks for a game session.
#   on   every CPU cluster on the performance governor at its highest frequency, the GPU (devfreq) at its highest; what was set before is kept in /run/peanuts-native-clocks
#   off  what was set before, back
st=/run/peanuts-native-clocks
gpus() { for d in /sys/class/devfreq/*; do [ -d "$d" ] || continue; case "$(basename "$d") $(cat "$d/name" 2>/dev/null)" in *gpu*|*kgsl*|*3d00000*|*adreno*|*mali*|*panfrost*) echo "$d";; esac; done; }
case "$1" in
on)
    if [ ! -f "$st" ]; then
        : > "$st"
        for p in /sys/devices/system/cpu/cpufreq/policy*; do echo "cpu $p $(cat "$p/scaling_governor") $(cat "$p/scaling_min_freq")" >> "$st"; done
        for d in $(gpus); do echo "gpu $d $(cat "$d/governor") $(cat "$d/min_freq")" >> "$st"; done
    fi
    for p in /sys/devices/system/cpu/cpufreq/policy*; do
        echo performance > "$p/scaling_governor" 2>/dev/null || true
        cat "$p/cpuinfo_max_freq" > "$p/scaling_min_freq" 2>/dev/null || true
    done
    for d in $(gpus); do
        if grep -qw performance "$d/available_governors" 2>/dev/null; then echo performance > "$d/governor" 2>/dev/null || true; fi
        cat "$d/max_freq" > "$d/min_freq" 2>/dev/null || true
    done ;;
off)
    [ -f "$st" ] || exit 0
    while read -r kind path gov minf; do
        case "$kind" in
        cpu) echo "$minf" > "$path/scaling_min_freq" 2>/dev/null || true; echo "$gov" > "$path/scaling_governor" 2>/dev/null || true ;;
        gpu) echo "$minf" > "$path/min_freq" 2>/dev/null || true; echo "$gov" > "$path/governor" 2>/dev/null || true ;;
        esac
    done < "$st"
    rm -f "$st" ;;
status) [ -f "$st" ] && echo on || echo off ;;
*) echo "usage: pn-clocks on|off|status"; exit 2 ;;
esac
H
chown root:root "$helper"; chmod 755 "$helper"
printf '%s ALL=(root) NOPASSWD: %s on, %s off, %s status\n' "$user" "$helper" "$helper" "$helper" > "$rule.tmp"
chmod 440 "$rule.tmp"
if visudo -cf "$rule.tmp" >/dev/null; then mv "$rule.tmp" "$rule"; else rm -f "$rule.tmp" "$helper"; echo "the sudoers rule did not check out: nothing installed"; exit 1; fi
echo "Max clocks are set up for $user: Peanuts Native turns them on while a game runs."
