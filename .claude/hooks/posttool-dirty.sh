#!/usr/bin/env bash
# PostToolUse(Edit|Write|MultiEdit): mark the tree dirty. Cleared by ./verify.
# Cheap on purpose — running the full oracle on every edit is the wrong tempo;
# the Stop gate is where dirtiness gets cashed out.
#
# Only writes INSIDE the repo dirty it. A PR body or scratch script under the
# session scratchpad after the final verify used to trip the stop gate and
# cost one oracle run for nothing — every implementer stream in HYPERSAW paid
# it, 2026-09-17..19 (their B158; kit 2.6.3). Relative paths are assumed
# inside; an unreadable path marks dirty: a false positive costs one verify,
# a false negative would let an unverified edit finish.
root="${CLAUDE_PROJECT_DIR:-$(pwd)}"
path="$(python3 -c 'import json,sys
try:
    d=json.load(sys.stdin); print(d.get("tool_input",{}).get("file_path","") or "")
except Exception:
    print("")' 2>/dev/null)"
if [ -n "$path" ]; then
  case "$path" in
    "$root"/*|"$root") ;;
    /*) exit 0 ;;
    *) ;;
  esac
fi
mkdir -p .harness
date -u +%FT%TZ > .harness/dirty
exit 0
