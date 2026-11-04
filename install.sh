#!/bin/sh
# Installs the Tacqor toolchain and standard library.
#   sh install.sh              -> /usr/local (if writable) or ~/.local
#   sh install.sh --prefix DIR
#   sh install.sh --uninstall
set -eu
here=$(cd "$(dirname "$0")" && pwd)
prefix=""
uninstall=no
while [ $# -gt 0 ]; do
    case "$1" in
        --prefix) prefix="$2"; shift 2;;
        --uninstall) uninstall=yes; shift;;
        *) echo "usage: sh install.sh [--prefix DIR] [--uninstall]"; exit 2;;
    esac
done
if [ -z "$prefix" ]; then
    if [ -w /usr/local/bin ] 2>/dev/null; then prefix=/usr/local; else prefix="$HOME/.local"; fi
fi
if [ "$uninstall" = yes ]; then
    rm -f "$prefix/bin/tacqor"
    rm -rf "$prefix/lib/tacqor"
    echo "removed $prefix/bin/tacqor and $prefix/lib/tacqor"
    exit 0
fi
binary="$here/linux-x64/tacqor"
if [ ! -x "$binary" ]; then
    if [ -x "$here/tacqor" ]; then binary="$here/tacqor"; else echo "no tacqor binary found; run make first"; exit 1; fi
fi
mkdir -p "$prefix/bin" "$prefix/lib/tacqor"
cp "$binary" "$prefix/bin/tacqor"
chmod +x "$prefix/bin/tacqor"
rm -rf "$prefix/lib/tacqor/std"
cp -R "$here/std" "$prefix/lib/tacqor/std"
echo "installed $prefix/bin/tacqor (std library in $prefix/lib/tacqor/std)"
case ":$PATH:" in
    *":$prefix/bin:"*) ;;
    *) echo "add $prefix/bin to your PATH, e.g.:  export PATH=\"$prefix/bin:\$PATH\"";;
esac
"$prefix/bin/tacqor" --version
