#!/bin/sh
# Packages the mod into dist/more-stratagem-slots-<version>.zip with HellForge's CLI, which builds it first
# (a mod that does not compile is not packed).
#
#   ./package.sh [HellForge folder]
#
# The HellForge folder is the installation (the one with app/ and mods/ in it). Without the argument it is taken
# from the HELLFORGE environment variable. Works on Linux and in Git Bash on Windows.
set -eu

mod=more-stratagem-slots
root=$(cd "$(dirname "$0")/.." && pwd) # the repository's root: this script is in scripts/
hf=${1:-${HELLFORGE:-}}
if [ -z "$hf" ]; then
    echo "Give your HellForge folder: ./package.sh ~/HellForge  (or set the HELLFORGE environment variable)" >&2
    exit 1
fi

cli=
for candidate in "$hf/app/cli/HellForge.Cli.exe" "$hf/app/cli-linux/HellForge.Cli"; do
    if [ -f "$candidate" ]; then cli=$candidate; break; fi
done
if [ -z "$cli" ]; then
    echo "No HellForge CLI under $hf/app" >&2
    exit 1
fi

# The CLI builds a mod only under an installation, so the mod is copied into its temp folder for the build.
work="$hf/temp/package-$mod"
rm -rf "$work"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work" "$root/dist"
cp -R "$root/$mod" "$work/$mod"

"$cli" package-mod --mod "$work/$mod" --out "$root/dist"
