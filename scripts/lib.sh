# Sourced by every script. The ONLY thing that reaches the console.
#
# tools/psplink/pspsh.exe is a tripwire that refuses; the real binary is in
# tools/psplink/.raw/ and only psp_cmd() runs it. That is deliberate: the
# transport carries one request at a time, and two of ours in it at once
# desyncs the bridge or leaves the device waiting on a response nobody will
# send. It then answers nothing and needs a power-cycle. Twice.

here="$(cd "$(dirname "$0")" && pwd)"
root="$(dirname "$here")"
raw="$root/tools/psplink/.raw"
pspsh="$raw/pspsh.exe"
usbhost="$raw/usbhostfs_pc.exe"

# host0: on the device IS this directory. Everything the console writes and
# everything a script pulls off it lands here, so the checkout stays clean and
# this can be emptied whenever.
link="$root/run"
mkdir -p "$link"

run_log="$link/pocketfin-run.log"
shell_log="/tmp/pocketfin-shell.txt"
bridge_log="/tmp/usbhostfs.log"
lock_dir="/tmp/pocketfin-device.lock"

# Every line carries seconds since the script started. A step that costs a
# minute is otherwise indistinguishable from one that hung, which is how a
# 3 s suite got watched for 147 s without anyone being able to say where it
# went.
_t0=$(date +%s)
say() { printf '[%3ss] %s\n' "$(( $(date +%s) - _t0 ))" "$*"; }
die() { say ""; say "  $*"; exit 1; }

# ---- one host process in the transport at a time ----
#
# mkdir is atomic wherever this runs; test-then-touch is not.

_held=0

device_lock() {
    [ "$_held" = 0 ] || { _held=$(( _held + 1 )); return 0; }
    _n=0
    while ! mkdir "$lock_dir" 2>/dev/null; do
        _n=$(( _n + 1 ))
        _who="$(cat "$lock_dir/who" 2>/dev/null)"

        # A DEAD OWNER IS NOT SOMETHING TO WAIT FOR. An aborted script leaves
        # the directory behind and every command after it then blocked for two
        # minutes before breaking it -- which is indistinguishable from a
        # console that has stopped answering, and was read as one.
        _pid="${_who##* }"
        case "$_pid" in
            ''|*[!0-9]*) ;;
            *) if ! kill -0 "$_pid" 2>/dev/null; then
                   say "  the device lock was left by a dead ${_who%% *} -- taking it"
                   rm -rf "$lock_dir"
                   continue
               fi ;;
        esac
        # A killed script leaves the directory. Two minutes is far longer
        # than any exchange here takes.
        if [ "$_n" -gt 120 ]; then
            say "  breaking a stale device lock held by ${_who:-someone}"
            rm -rf "$lock_dir"
            continue
        fi
        [ "$_n" = 1 ] && say "  waiting for the device lock (${_who:-someone})"
        sleep 1
    done
    printf '%s pid %s\n' "$(basename "$0")" "$$" > "$lock_dir/who" 2>/dev/null
    _held=1
    trap 'rm -rf "$lock_dir"' EXIT INT TERM
}

device_unlock() {
    _held=$(( _held - 1 ))
    [ "$_held" -le 0 ] && { _held=0; rm -rf "$lock_dir"; }
    return 0
}

# ---- the one way to reach the console ----
#
# Prints what came back. 1 when nothing did, and it says WHICH failure --
# a desynced bridge and a wedged console look identical and only one of them
# needs a power-cycle.
psp_cmd() {
    _cmd="$1"
    _bound="${2:-15}"

    # A transfer while the application is running puts two users in the
    # transport: the app writing its log, and this. That is freeze number one,
    # exactly, and refusing it is the whole reason this function exists.
    case "$_cmd" in
        # kill terminates threads wherever they are, including inside a
        # usbhostfs operation -- which leaves the device waiting on a response
        # nobody will send, and the console then needs a power-cycle. It has
        # done exactly that. HOME is the safe stop: the exit callback sets the
        # quit flag, the loop leaves, and the card log is flushed on the way
        # out. That works even when the link is down, which is when kill is
        # most dangerous and most tempting.
        kill\ *)
            if [ "${POCKETFIN_FORCE_KILL:-}" != "1" ]; then
                say "  REFUSED: \"$_cmd\"."
                say "  Press HOME on the console -- it exits cleanly and"
                say "  flushes the card log, even with the link down."
                say "  scripts/psp.sh stop asks first and only then forces."
                return 2
            fi
            ;;
        cp\ *|copy\ *)
            # THREADS, NOT RESIDENCE. The hazard is the app writing its log
            # down the same cable, which needs its writer alive -- and a
            # module that has exited stays resident with no threads at all,
            # which is exactly when its card log is the thing worth reading.
            if [ -n "$(_app_threads_unlocked)" ]; then
                say "  REFUSED: \"$_cmd\" while the application is running."
                say "  It writes its log down the same cable. Stop it first:"
                say "      scripts/psp.sh stop"
                return 2
            fi
            ;;
    esac

    device_lock
    # grep -c prints 0 AND exits 1 when it finds nothing, so "|| echo 0"
    # gives two lines and every comparison after it is a syntax error.
    _before="$(grep -c "invalid magic" "$bridge_log" 2>/dev/null)"
    _out="$(timeout "$_bound" "$pspsh" -e "$_cmd" 2>&1)"
    _after="$(grep -c "invalid magic" "$bridge_log" 2>/dev/null)"
    _before="${_before:-0}"
    _after="${_after:-0}"
    device_unlock

    printf '%s' "$_out"

    # A FRESH probe, every time it matters. Nothing here may report on the
    # device from an earlier reading: saying "it answers" from a status taken
    # minutes ago is how a wedged console was called healthy.
    case "$_cmd" in
        kill\ *|reset*|poweroff*) _recheck=1 ;;
        *) [ -z "$_out" ] && _recheck=1 || _recheck=0 ;;
    esac
    if [ "$_recheck" = 1 ]; then
        if ! printf '%s' "$(timeout 8 "$pspsh" -e "ls" 2>/dev/null)" |
                grep -q "Listing directory"; then
            say ""
            say "  ***  THE DEVICE HAS STOPPED ANSWERING  ***"
            say "  after: $_cmd"
            say "  The bridge may be desynced or the console wedged."
            say "  A wedged console needs a power cycle, which is yours to call."
            return 3
        fi
    fi

    if [ "$_after" -gt "$_before" ]; then
        say ""
        say "  THE BRIDGE DESYNCED on \"$_cmd\". The console is probably fine."
        say "  scripts/psp.sh restarts it."
        return 1
    fi
    [ -n "$_out" ] || return 1
    return 0
}

# Used by the refusal above, so it cannot recurse into psp_cmd.
_resident_unlocked() {
    device_lock
    _m="$(timeout 15 "$pspsh" -e "modlist" 2>/dev/null |
          awk '/Name: pocketfin/ { print $2 }')"
    device_unlock
    printf '%s' "$_m"
}

_app_threads_unlocked() {
    device_lock
    _t="$(timeout 15 "$pspsh" -e "thlist" 2>/dev/null |
          grep -cE "Name: (link|logwriter|update)[[:space:]]*$")"
    device_unlock
    [ "${_t:-0}" -gt 0 ] && printf '%s' "$_t"
}

resident()       { _resident_unlocked; }
device_answers() {
    case "$(psp_cmd "ls" 8)" in *"Listing directory"*) return 0 ;; esac
    return 1
}

bridge_running() {
    tasklist //FI "IMAGENAME eq usbhostfs_pc.exe" 2>/dev/null |
        grep -q usbhostfs_pc
}

start_bridge() {
    say "  bridge: starting usbhostfs"
    ( cd "$link" && "$usbhost" >"$bridge_log" 2>&1 & )
    timeout 15 tail -n +1 -f "$bridge_log" 2>/dev/null |
        grep -q -m1 "Connected to device"
}

# ---- the remote channel ----
#
# The application watches $link/cmd.txt and answers into ack.txt and
# state.txt. It runs over the SAME cable as the log, which is why psp_cmd
# refuses transfers while a module is resident.

# ASKED TWICE, BOUNDED SHORT. A watch takes whatever cmd.txt already held when
# it started as its baseline and does not deliver it, so a command written in
# the window around a load is swallowed by design and the second write is a
# line that has changed and cannot be. Eight seconds because the channel polls
# every few milliseconds when it works: a reply that has not come by then is
# not coming, and waiting longer only delays the diagnosis.
remote_ask() {
    _word="$1"; _arg="${2:-}"; _bound="${3:-8}"

    [ -f "$link/remote.on" ] || { say "  the channel is off -- nothing is running"; return 1; }

    _seq="$(( $(cat "$link/ack.txt" 2>/dev/null | tr -dc 0-9) + 1 ))"
    for _try in 1 2; do
        printf '%s %s %s\n' "$_seq" "$_word" "$_arg" > "$link/cmd.txt"
        if timeout "$_bound" sh -c '
                while [ "$(cat "'"$link"'/ack.txt" 2>/dev/null | tr -dc 0-9)" != "'"$_seq"'" ]; do
                    sleep 0.2
                done'; then
            return 0
        fi
        _seq=$(( _seq + 1 ))
    done

    say "  no ack for \"$_word $_arg\" in $(( _bound * 2 ))s, asked twice."
    say "  last log line: $(tail -1 "$run_log" 2>/dev/null | cut -c1-80)"
    say "  a heartbeat there means it is looping and the CHANNEL is the problem."
    return 1
}

remote_cmd() {
    remote_ask "$1" "${2:-}" || return 1
    cat "$link/state.txt" 2>/dev/null
}

# 391 kB in 8 kB pieces at one piece per 5 ms link pass is about 2.5 s on a
# healthy cable, so ten is already a failure.
take_shot() {
    _name="${1:-console}"
    _leaf="shot-$_name.ppm"
    _ppm="$link/$_leaf"
    _out="$link/shots/$_name.png"

    rm -f "$_ppm"
    remote_ask "shot" "$_leaf" 10 || return 1

    case "$(cat "$link/state.txt" 2>/dev/null)" in
        *FAILED*) say "  the console refused the capture"; return 1 ;;
    esac

    _want=$(( 15 + 480 * 272 * 3 ))
    _got="$(stat -c %s "$_ppm" 2>/dev/null || echo 0)"
    [ "$_got" = "$_want" ] || { say "  $_leaf is $_got bytes of $_want -- it arrived cut short"; return 1; }

    mkdir -p "$link/shots"
    python "$root/scripts/ppm2png.py" "$_ppm" "$_out" || return 1
    rm -f "$_ppm"
    say "  $_out"
}

# WHETHER THERE IS A CONSOLE AT ALL, from the bridge's own log, in no time.
#
# The bridge prints "waiting for device..." until one appears and "Connected
# to device" when it does, so the last of those two IS the answer. Asking the
# device instead costs a 15 s pspsh bound and then another 15 s restarting the
# bridge -- half a minute to discover a cable that is not plugged in.
bridge_connected() {
    case "$(grep -E "Connected to device|waiting for device" "$bridge_log" 2>/dev/null | tail -1)" in
        *"Connected to device"*) return 0 ;;
        *) return 1 ;;
    esac
}

# Silence is the bridge's symptom as well as the console's, and only one of
# the two is cheap to rule out.
require_device() {
    # THE CONSOLE IS THE AUTHORITY, AND THE LOG IS ONLY A HINT. A second
    # usbhostfs left running keeps writing "waiting for device" into the log
    # bridge_connected reads, while the instance actually holding the console
    # answers everything -- and a perfectly healthy PSP is then reported as
    # unplugged, through a reboot that was never needed.
    device_answers && return 0

    # FREE, AND FIRST. Nothing below is worth a second if there is no console.
    if ! bridge_connected; then
        say "  THE CONSOLE IS NOT CONNECTED -- the bridge is still waiting for it."
        say "  Plug it in and start PSPLINK, then run this again."
        exit 2
    fi
    device_answers && return 0
    say "  no answer -- restarting the bridge and asking once more"
    taskkill //F //IM usbhostfs_pc.exe >/dev/null 2>&1 || pkill -f usbhostfs_pc 2>/dev/null
    start_bridge
    device_answers && return 0
    say ""
    say "  THE PSP IS NOT ANSWERING. No bytes came back."
    say "  HOME -> exit on the console; if that does nothing, hold POWER up ~10 s"
    say "  and start PSPLINK again."
    exit 2
}
