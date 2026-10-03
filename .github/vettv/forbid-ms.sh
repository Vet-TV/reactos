#!/usr/bin/env bash
# No-Microsoft-files guard for VetTV Retro OS ISOs.
# Fails if any file in the ISO matches a known Microsoft redistributable name,
# UNLESS its sha256 equals a .dll that this same build's ninja graph produced
# (e.g. ReactOS's own Wine-derived d3dx9_*.dll). Usage: forbid-ms.sh <iso> <build-dir>
# Requires: 7z, sha256sum, and `ninja` on PATH (RosBE shell).
set -euo pipefail
ISO="$1"; BUILD="$2"
PATTERN='(msvcr[0-9]+|msvcp[0-9]+|vcruntime[0-9_]*|d3dx9_[0-9]+|mfc[0-9]+[a-z]*)\.dll$'
# Always forbidden, no hash allowlist: inpout drivers (licence audit pending) and any
# user-imported UserFiles path (IMP-5). Win16 names (KRNL386.EXE etc.) deliberately absent.
HARD='((^|/)inpout(32|x64)\.dll$|(^|/)UserFiles(/|$))'
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT

7z l -slt "$ISO" | sed -n 's/^Path = //p' > "$WORK/iso-paths.txt"
if grep -Ei "$HARD" "$WORK/iso-paths.txt" > "$WORK/hard.txt"; then
  sed 's/^/FORBID (always) /' "$WORK/hard.txt"; echo "::error::Always-forbidden path(s) in $(basename "$ISO")"; exit 1
fi
grep -Ei "$PATTERN" "$WORK/iso-paths.txt" > "$WORK/matched.txt" || true
N=$(wc -l < "$WORK/matched.txt")
echo "name-pattern matches in $(basename "$ISO"): $N"
[ "$N" -eq 0 ] && { echo "PASS: no forbidden names"; exit 0; }

# Hashes of every .dll the ninja graph in this build dir declares as an output.
( cd "$BUILD" && ninja -t targets all | sed -n 's/^\(.*\.dll\): .*/\1/p' | sort -u \
  | while read -r f; do if [ -f "$f" ]; then sha256sum "$f"; fi; done ) > "$WORK/built.sha256"
echo "ninja-built .dll outputs hashed: $(wc -l < "$WORK/built.sha256")"
cut -d' ' -f1 "$WORK/built.sha256" | sort -u > "$WORK/built-hashes.txt"

mkdir -p "$WORK/x"
7z x -y -o"$WORK/x" "$ISO" @"$WORK/matched.txt" >/dev/null
FAIL=0
while read -r p; do
  h=$(sha256sum "$WORK/x/$p" | cut -d' ' -f1)
  if grep -qx "$h" "$WORK/built-hashes.txt"; then
    src=$(grep -m1 "^$h " "$WORK/built.sha256" | cut -d' ' -f3-)
    echo "ALLOW  $p  $h  (built: $src)"
  else
    echo "FORBID $p  $h  (not a build output of this run)"; FAIL=1
  fi
done < "$WORK/matched.txt"
[ "$FAIL" -eq 0 ] && echo "PASS: every name match is a ReactOS build output" || { echo "::error::Forbidden file(s) in ISO"; exit 1; }
