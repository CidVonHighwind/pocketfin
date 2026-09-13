#!/bin/sh
# The console, in one command. Everything device-side goes through here.
#
#   psp.sh status            what is true right now          (~3 s)
#   psp.sh stop              ask the module to leave         (~3 s)
#   psp.sh app               build, load, leave it running   (~40 s)
#   psp.sh run [group]       build, load, run the checks     (~60 s)
#   psp.sh speed             the link, with no media server in it (~40 s)
#   psp.sh cmd <words...>    one remote command              (~2 s)
#   psp.sh shot [name]       the panel, as a PNG             (~5 s)
#
# ---- WHY THIS FILE EXISTS AT ALL ----
#
# The version before it was correct and unusable: every operation blocked in
# silence for a minute or more, so a wedged console and a healthy one looked
# identical until the bound expired. An evening went into waiting out timeouts
# for failures that were already decided in the first three seconds.
#
# So: NOTHING BLOCKS SILENTLY. A load streams the device's own log as it
# arrives, and every wait ends on the first line that settles the question --
# success or failure -- rather than on a duration. Durations here are upper
# bounds meaning "this has failed", set just above what a healthy step costs,
# because a generous bound on a machine that hangs is a guaranteed wait.
set -u

. "$(cd "$(dirname "$0")" && pwd)/lib.sh"

# Measured, not guessed. A step that takes longer than this has failed, and
# failing at the bound must cost seconds rather than minutes.
BOUND_LOAD=25   # load to the first log line
BOUND_STOP=12   # ask to threads gone
BOUND_CHECKS=120 # link then clock alone measured 91 s against the real server
BOUND_CMD=8     # one remote command and its ack

# ---- the bridge ----

ensure_bridge() {
    if ! bridge_running; then start_bridge; return; fi
    # "invalid magic" is the bridge's framing lost: the console is fine and
    # answers nothing, which is indistinguishable from wedged.
    if grep -q "invalid magic" "$bridge_log" 2>/dev/null; then
        say "  bridge desynced -- restarting it"
        taskkill //F //IM usbhostfs_pc.exe >/dev/null 2>&1 || pkill -f usbhostfs_pc 2>/dev/null
        start_bridge
    fi
}

# ---- state ----

do_status() {
    _res="$(resident)"
    if [ -n "$_res" ]; then
        say "  module: resident ($_res)"
    else
        say "  module: none"
    fi

    _free="$(psp_cmd "meminfo" 10 2>/dev/null | awk '$1 == "2" { print $9 }')"
    case "${_free:-0}" in ''|*[!0-9]*) _free=0 ;; esac
    if [ "$_free" -ge 16777216 ]; then
        say "  memory: $_free bytes contiguous"
    else
        say "  memory: $_free bytes contiguous -- fragmented; a load may be refused"
    fi

    # WHETHER A CABLED SLEEP WOULD SURVIVE. Partition 11 is what carries the
    # module across a suspend with the cable attached, and every psplink reset
    # consumes it -- so without this line a standby test can be run, fail, and
    # be believed, when what actually happened is the module was wiped. That
    # cost an hour tonight and the check existed before this rewrite dropped
    # it.
    _p8="$(psp_cmd "meminfo" 10 2>/dev/null | awk '$1 == "8" { print $3 }')"
    case "$_p8" in
        0x8BB00000) say "  standby: partition 11 present -- a cabled sleep survives" ;;
        "")         say "  standby: could not read the partition table" ;;
        *)          say "  standby: partition 11 is GONE ($_p8) -- a cabled sleep WILL wipe"
                    say "           the module. Power-cycle first, or test from the XMB." ;;
    esac

    # GROWING OR NOT, which is the only question that separates a running
    # application from a resident one that has stopped.
    _a="$(grep -c '' "$run_log" 2>/dev/null)"
    sleep 2
    _b="$(grep -c '' "$run_log" 2>/dev/null)"
    if [ "${_b:-0}" -gt "${_a:-0}" ]; then
        say "  log: growing (${_a:-0} -> ${_b:-0} lines)"
    else
        say "  log: still at ${_b:-0} lines -- nothing is logging"
    fi
    [ -s "$run_log" ] && say "  last: $(tail -1 "$run_log" | cut -c1-100)"
    return 0
}

# ---- stopping ----
#
# ASK, then verify its threads are gone, then unload. Never force: a kill can
# land inside a usbhostfs operation and the console then needs a power cycle.
# A module that will not stop is REPORTED, because the fix for that is a thumb
# on the HOME button and no script can do it.

do_stop() {
    _uids="$(resident)"
    [ -n "$_uids" ] || return 0

    if [ -f "$link/remote.on" ]; then
        # One past what it has answered, like every other command. A fixed
        # number worked exactly once: the app takes a sequence only if it is
        # above the last, and every cmd after a stop counts on from there, so
        # the second stop of a session asked with a number already spent and
        # was dropped as a duplicate -- reported as "IT WILL NOT STOP" with a
        # perfectly healthy app on the other end.
        echo "$(( $(cat "$link/ack.txt" 2>/dev/null | tr -dc 0-9) + 1 )) quit" > "$link/cmd.txt"
        _end=$(( $(date +%s) + BOUND_STOP ))
        while [ "$(date +%s)" -lt "$_end" ]; do
            if ! psp_cmd thlist 10 2>/dev/null |
                 grep -qE "Name: (link|logwriter|update)[[:space:]]*$"; then
                rm -f "$link/cmd.txt"
                say "  it shut its threads down"
                for u in $_uids; do
                    POCKETFIN_FORCE_KILL=1 psp_cmd "kill $u" 10 >/dev/null 2>&1
                done
                [ -z "$(resident)" ] && return 0
                break
            fi
            sleep 1
        done
        rm -f "$link/cmd.txt"
    fi

    say ""
    say "  IT WILL NOT STOP. Press HOME on the console -- it exits cleanly and"
    say "  flushes the card log even with the link down. Forcing it from here"
    say "  can leave a thread inside a usbhostfs operation, and that is what"
    say "  needs a power cycle."
    return 1
}

# ---- loading ----
#
# THE FIRST LOAD AFTER A RESET WORKS; THE SECOND OFTEN DOES NOT.
#
# A module loaded without one behind it comes up with the CABLE dead:
# resident, its threads running, its checks executing -- and an empty log and
# no acks, which is indistinguishable from a hang. It was diagnosed as a
# heap-size problem twice before the pattern was seen, and it alternates:
# clean, silent, clean, silent.
#
# NOTHING HERE RESETS PSPLINK. A reset does clear the silence -- and consumes
# partition 11 doing it, which is what carries a module across a cabled sleep,
# so paying it routinely destroys standby testing; one reset also never came
# back at all. A silent load is REPORTED and HOME is the answer.
#
# What is cheap is restarting the bridge, a PC-side process, so every load
# starts on a fresh one. It has to happen BEFORE the load: a module already up
# holds file handles from the old bridge instance and restarting underneath it
# does not revive them.
fresh_bridge() {
    # AND THE SHELL WITH IT. load_module leaves a pspsh alive for an hour, so
    # a second load starts while the first one is still attached: the new
    # module comes up and its cable is silent. That is the "every second load
    # is dead" symptom -- it is this, not the console.
    taskkill //F //IM pspsh.exe >/dev/null 2>&1 || pkill -f pspsh 2>/dev/null
    taskkill //F //IM usbhostfs_pc.exe >/dev/null 2>&1 || pkill -f usbhostfs_pc 2>/dev/null
    start_bridge
    return 0
}

do_build() {
    _out="$(wsl -d Ubuntu -- bash -lc \
        "cd /root/jfpsp && export PATH=/usr/local/pspdev/bin:\$PATH \
         PSPDEV=/usr/local/pspdev && make BUILD_PRX=1" 2>&1)"
    [ $? -eq 0 ] || { printf '%s\n' "$_out" | tail -20; die "build failed"; }
    # ONE narrow exception: psp-fixup-imports says "stubs out of order"
    # whenever the network libraries are linked, in every ordering tried, and
    # the module loads and runs regardless.
    printf '%s\n' "$_out" | grep -iE "error|warning" \
        | grep -v "could not fixup imports, stubs out of order" > "$root/build/warn.txt"
    [ -s "$root/build/warn.txt" ] && { head -10 "$root/build/warn.txt"; die "build was not clean"; }
    cp "$root/build/psp/pocketfin.prx" "$link/pocketfin.prx" || die "could not stage the module"
    say "  built $(wc -c < "$link/pocketfin.prx" | tr -d ' ') bytes"
}

# Loads and returns. The pspsh session lives as long as $1 seconds; the module
# outlives it.
load_module() {
    # NEVER OVER A MODULE THAT IS STILL THERE. do_stop can fail -- a film
    # playing does not answer the quit within its bound -- and loading anyway
    # puts two modules in the air: the second never says READY, which then
    # looks like the dead-cable symptom and spends a psplink reset (and
    # partition 11 with it) on a problem that was self-inflicted. Twice.
    if [ -n "$(resident)" ]; then
        say ""
        say "  A MODULE IS STILL RESIDENT and would not stop."
        say "  Press HOME on the console -- it exits cleanly and flushes the"
        say "  card log -- then run this again."
        exit 1
    fi

    # NOR WITHOUT THE ROOM FOR IT. The module wants 14.5 MB of heap and the
    # cable wants 256 kB after that; loaded into less, it comes up resident
    # and RUNNING with a dead cable -- empty log, no acks -- which reads
    # exactly like a hung console and has been diagnosed as one twice.
    # Measured: 9.7 MB free is enough to load and not enough to talk, and
    # 14.9 MB is enough to load and not enough to CLAIM THE HEAP -- that one
    # comes up with the link threads and no application at all. A fresh boot
    # gives 25 MB and a clean stop 21.9, so 18 accepts every healthy state and
    # refuses the marginal ones.
    _free="$(psp_cmd "meminfo" 10 2>/dev/null | awk '$1 == "2" { print $9 }')"
    case "${_free:-0}" in ''|*[!0-9]*) _free=0 ;; esac
    if [ "$_free" -lt 18874368 ]; then
        say ""
        say "  ONLY $_free BYTES FREE -- a load here comes up silent."
        say "  Press HOME on the console, then psp.sh stop. If that does not"
        say "  give it back, the console needs its power switch."
        exit 1
    fi
    device_lock
    : > "$shell_log"
    : > "$run_log"
    # DETACHED FROM WHOEVER CALLED US, all three descriptors. This group
    # outlives the command by an hour on purpose, and inheriting the caller's
    # stdout means anything reading psp.sh's output -- a pipe, a script, an
    # editor -- waits an hour for an end-of-file after the work is done and
    # "it is running on the console" has already been printed.
    { ( echo "host0:/pocketfin.prx ${2:-}"; sleep "$1" ) |
        timeout "$(( $1 + 10 ))" "$pspsh"; } >> "$shell_log" 2>&1 </dev/null &
}

# WAITS ON A LINE, NOT A CLOCK. Returns as soon as $2 appears in the log, or 1
# at the bound -- and prints what did arrive either way, so a failure is read
# rather than guessed at.
wait_for() {
    _bound="$1"; _want="$2"
    _end=$(( $(date +%s) + _bound ))
    while [ "$(date +%s)" -lt "$_end" ]; do
        grep -qE "$_want" "$run_log" 2>/dev/null && return 0
        sleep 1
    done
    return 1
}

# The log lines matching $2 as they land, until POCKETFIN-DONE or $1 seconds.
# POLLED, NOT TAILED: `tail -f | grep | sed q` does not terminate -- sed quits
# on the marker, tail keeps running, and a finished run sat there until it was
# killed by hand.
follow() {
    _seen=0
    _end=$(( $(date +%s) + $1 ))
    while [ "$(date +%s)" -lt "$_end" ]; do
        _now="$(grep -c '' "$run_log" 2>/dev/null)"; _now="${_now:-0}"
        if [ "$_now" -gt "$_seen" ]; then
            sed -n "$(( _seen + 1 )),${_now}p" "$run_log" 2>/dev/null | grep -E "$2"
            _seen="$_now"
        fi
        grep -q "POCKETFIN-DONE" "$run_log" 2>/dev/null && break
        sleep 1
    done
}

do_app() {
    do_build
    fresh_bridge
    load_module 3600
    if wait_for "$BOUND_LOAD" "POCKETFIN-READY"; then
        say "  it is running on the console"
        return 0
    fi
    say "  no READY in ${BOUND_LOAD}s -- the module is loaded and the cable is silent."
    say "  $(tail -1 "$shell_log" 2>/dev/null | cut -c1-90)"
    say ""
    say "  Press HOME on the console, then run this again. Nothing here will"
    say "  reset psplink: that clears the silence and takes partition 11 with"
    say "  it, which is what a cabled sleep needs -- and one reset never came"
    say "  back at all."
    return 1
}

do_run() {
    do_build
    fresh_bridge
    load_module "$BOUND_CHECKS" "checks ${1:-}"
    if ! wait_for "$BOUND_LOAD" "POCKETFIN-READY"; then
        say "  no READY in ${BOUND_LOAD}s -- loaded, cable silent. Nothing to read."
        say "  Press HOME on the console and run this again."
        return 1
    fi
    # THE CHECKS AS THEY LAND, so a run that stalls is obvious at the moment it
    # stalls rather than at the bound.
    follow "$BOUND_CHECKS" "(ok|FAIL|SKIP) +[a-z]|POCKETFIN-DONE|stage:|clock:|cpu:"

    # LEFT ON THE PANEL. Stopping here blanks the screen the moment the run
    # ends, so the one place the verdict is visible without a cable goes dark
    # at exactly the wrong time -- and a black screen is indistinguishable
    # from a console that died. `psp.sh stop` when you have read it.
    say "  the verdict is on the panel; psp.sh stop when you are done with it"
}

# The link on its own, and the server as a separate question. The module's
# "speed" mode reads jellyfin.txt for a host, so pointing that at any machine
# serving /pocketfin-speed.bin measures the radio with no media server in it.
do_speed() {
    do_build
    fresh_bridge
    load_module 240 "speed"
    if ! wait_for "$BOUND_LOAD" "speed:"; then
        say "  no speed line in ${BOUND_LOAD}s -- loaded, cable silent."
        return 1
    fi
    follow 240 "speed:|POCKETFIN-DONE"
}

case "${1:-status}" in
    status) ensure_bridge; require_device; do_status ;;
    stop)   ensure_bridge; require_device; do_stop ;;
    app)    ensure_bridge; require_device; do_stop && do_app ;;
    run)    ensure_bridge; require_device; do_stop && do_run "${2:-}" ;;
    speed)  ensure_bridge; require_device; do_stop && do_speed ;;
    cmd)    shift; ensure_bridge; remote_cmd "$@" ;;
    shot)   shift; ensure_bridge; take_shot "${1:-console}" ;;
    *)      die "usage: psp.sh [status|stop|app|run <group>|speed|cmd <words>|shot <name>]" ;;
esac
