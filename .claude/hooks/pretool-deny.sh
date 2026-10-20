#!/usr/bin/env bash
# PreToolUse(Bash): second line of defense behind settings.json deny rules.
# Reads hook JSON on stdin; exit 2 blocks the tool call and feeds stderr to Claude.
set -uo pipefail

INPUT=$(cat)
CMD=$(printf '%s' "$INPUT" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("tool_input",{}).get("command",""))' 2>/dev/null || echo "")

BLOCKLIST='rm -rf /|rm -rf ~|git push --force|git push -f|git reset --hard|git clean -fd|chmod -R 777|curl .*\| *(ba)?sh|wget .*\| *(ba)?sh|> */dev/sd'

if printf '%s' "$CMD" | grep -qE "$BLOCKLIST"; then
  echo "BLOCKED by harness: command matches destructive pattern. If genuinely needed, ask the human to run it." >&2
  exit 2
fi
exit 0
