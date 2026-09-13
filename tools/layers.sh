#!/bin/sh
# The dependency direction, checked rather than intended.
#
#   tools/layers.sh          refuse any upward edge; 0 when clean
#   tools/layers.sh --graph  print every cross-layer edge and its count
#
# Both builds run this at parse/configure time, so a violation fails before
# anything compiles.
set -u

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root" || exit 2

# THE ORDER, bottom up. A file may include only from its own layer or one
# earlier in this list.
#
# port is the seam HEADERS and depends on nothing.
ORDER="port base io jelly model view page app shell"

# tools/ is orthogonal: it may read anything, and only the shell and the tools
# themselves may include it. The application never sees the harness.
TOOLS_MAY_BE_USED_BY="shell tools"

# A port implementation (psp/gfx.c, desktop/audio.c, ...) is a membership
# list, same shape as TOOLS_MAY_BE_USED_BY above, not a rank: it may reach the
# seam it implements and the layers genuinely below it, never model, view,
# page, app, shell or tools.
PORT_MAY_INCLUDE="port base io"

# AS A TARGET, port/psp and port/desktop rank with the seam (0) -- the shell
# must be able to include port/desktop/window.h.
rank() {
    want=$1
    case "$2:$want" in
        target:port/psp|target:port/desktop) echo 0; return 0;;
        *:tools) echo 99; return 0;;
    esac
    i=0
    for l in $ORDER; do
        [ "$l" = "$want" ] && { echo $i; return 0; }
        i=$((i + 1))
    done
    echo -1
}

# The layer a path belongs to: the directory under src/, except that the two
# port implementations rank apart from the seam they implement.
layer_of() {
    d=$(echo "$1" | cut -d/ -f2)
    if [ "$d" = "port" ]; then
        sub=$(echo "$1" | cut -d/ -f3)
        case "$sub" in psp|desktop) d="port/$sub";; esac
    fi
    echo "$d"
}

edges() {
    for f in $(git ls-files 'src/*.c' 'src/*.h' 'src/**/*.c' 'src/**/*.h' | sort -u); do
        from=$(layer_of "$f")
        grep -o '^[[:space:]]*#[[:space:]]*include[[:space:]]*"[a-zA-Z_/0-9]*\.h"' "$f" 2>/dev/null |
        sed 's|.*"\(.*\)"|\1|' |
        while read -r inc; do
            to=$(layer_of "src/$inc")
            [ "$from" = "$to" ] && continue
            echo "$from $to $f $inc"
        done
    done
}

if [ "${1:-}" = "--graph" ]; then
    edges | awk '{print $1" -> "$2}' | sort | uniq -c | sort -rn
    exit 0
fi

edges | while read -r from to file inc; do
    # An include with no '/' is the compiler's own-directory search (a local
    # header, or a vendor one like stb_image.h that FetchContent places on
    # the include path rather than in src/) -- not a layer edge, and nothing
    # layer_of could classify against ORDER anyway.
    case "$inc" in
        */*) ;;
        *) continue;;
    esac

    if [ "$from" = "port/psp" ] || [ "$from" = "port/desktop" ]; then
        case " $PORT_MAY_INCLUDE " in
            *" $to "*) continue;;
        esac
        why="a port implementation may reach only port, base and io"
    elif [ "$to" = "tools" ]; then
        case " $TOOLS_MAY_BE_USED_BY " in
            *" $from "*) continue;;
        esac
        why="the application may not see the harness"
    else
        rf=$(rank "$from" source)
        rt=$(rank "$to" target)
        if [ "$rt" -eq -1 ]; then
            why="the checker cannot classify \"$inc\""
        elif [ "$rt" -le "$rf" ]; then
            continue
        else
            why="$from may not reach up into $to"
        fi
    fi

    printf '  %s\n      includes "%s" -- %s\n' "$file" "$inc" "$why"
done > "$root/build/layers.txt" 2>/dev/null

if [ -s "$root/build/layers.txt" ]; then
    echo ""
    echo "  THE LAYERS ARE:  $ORDER"
    echo "  and a file may include only from its own or an earlier one."
    echo ""
    cat "$root/build/layers.txt"
    echo ""
    exit 1
fi

# R4 -- no test seam in a shipping file. `#ifdef POCKETFIN_CHECKS` may only
# appear in base/, which holds the one deliberate exception, and in tests/.
# Anywhere else, a fake is compiled into a file the console ships, and the
# shape it is hiding in is a branch nobody sees from the outside. The fix is
# always the same: the module takes an interface and the fake is a second
# implementation under tests/.
SEAMS="$root/tools/seams.allow"

git ls-files 'src/*.c' 'src/*.h' 'src/**/*.c' 'src/**/*.h' |
while read -r f; do
    grep -q 'POCKETFIN_CHECKS' "$f" 2>/dev/null || continue
    [ "$(layer_of "$f")" = "base" ] && continue
    grep -qxF "$f" "$SEAMS" 2>/dev/null && continue
    printf '  %s\n      has a POCKETFIN_CHECKS branch in a file the console ships\n' "$f"
done > "$root/build/seams.txt" 2>/dev/null

if [ -s "$root/build/seams.txt" ]; then
    echo ""
    echo "  A CHECK SEAM IN A SHIPPING FILE (rule R4)."
    echo ""
    cat "$root/build/seams.txt"
    echo "  Take an interface and put the fake under tests/, or list the file"
    echo "  in tools/seams.allow with the stage that removes it."
    echo ""
    exit 1
fi

[ "${1:-}" = "-v" ] && echo "layers: clean"
exit 0
