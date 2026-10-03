#!/usr/bin/env bash
# M0-11: downstream diff size outside modules/vettv/. Usage: diff-size.sh <base> <head> [limit]
set -euo pipefail
BASE="${1:-upstream/master}"; HEAD="${2:-vettv/main}"; LIMIT="${3:-500}"
git diff --stat "$BASE...$HEAD" -- . ':(exclude)modules/vettv/**'
N=$(git diff --numstat "$BASE...$HEAD" -- . ':(exclude)modules/vettv/**' | awk '{a+=$1; d+=$2} END{print a+d+0}')
echo "downstream lines changed outside modules/vettv/: $N (limit $LIMIT)"
[ "$N" -le "$LIMIT" ]
