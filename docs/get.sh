#!/bin/sh
# Installs the latest Tacqor release (Linux x64) from GitHub:
#   curl -fsSL https://tacqor.github.io/tacqor/get.sh | sh
#   curl -fsSL https://tacqor.github.io/tacqor/get.sh | sh -s -- --prefix ~/.local
set -eu
repo=tacqor/tacqor
tag=$(curl -fsSL "https://api.github.com/repos/$repo/releases/latest" | sed -n 's/.*"tag_name": *"\([^"]*\)".*/\1/p' | head -n 1)
[ -n "$tag" ] || { echo "could not determine the latest release of $repo"; exit 1; }
version=${tag#v}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
echo "downloading Tacqor $version"
curl -fsSL "https://github.com/$repo/releases/download/$tag/Tacqor_${version}_Standalone.zip" -o "$tmp/tacqor.zip"
(cd "$tmp" && unzip -q tacqor.zip)
sh "$tmp/Tacqor_$version/install.sh" "$@"
