#!/usr/bin/env bash
# Runtime smoke for the SDL emulator: boot, hold for a few seconds while
# a frame gets drawn, confirm the process didn't crash, tear down.
#
# What this catches that the build-only smoke misses: init-time panics
# from LovyanGFX / SDL / TileStore that only surface at runtime — e.g.,
# a bad kFallbackTile[] byte, a missing embed_file, a font that fails to
# load, an assert-fail in the boot-splash sequencer.
#
# Local dev only (needs a display). CI uses `pio run -e native` as the
# compile-side counterpart.
#
# Usage:
#   scripts/dev/smoke-native.sh              # 3-second smoke
#   scripts/dev/smoke-native.sh 10           # 10-second smoke
#
# Exit codes:
#   0 = booted and rendered at least one frame
#   1 = process died or no frame captured
#   2 = binary missing (run pio run -e native first)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="$REPO_ROOT/.pio/build/native/program"
PPM=/tmp/plane-radar-screenshot.ppm
DURATION="${1:-3}"

if [[ ! -x "$BIN" ]]; then
  echo "smoke-native: $BIN not built. Run: pio run -e native" >&2
  exit 2
fi

rm -f "$PPM"
"$BIN" >/tmp/plane-radar-smoke.log 2>&1 &
PID=$!
trap 'kill "$PID" 2>/dev/null || true; wait "$PID" 2>/dev/null || true' EXIT

# Give the emulator a chance to init + dump its first frame (auto-shot
# fires every ~200 ms; give it a generous window).
for _ in $(seq 1 20); do
  if [[ -s "$PPM" ]]; then
    break
  fi
  # Fail fast if it crashed at boot.
  if ! kill -0 "$PID" 2>/dev/null; then
    echo "smoke-native: emulator died during init. Log:" >&2
    tail -40 /tmp/plane-radar-smoke.log >&2
    exit 1
  fi
  sleep 0.2
done

if [[ ! -s "$PPM" ]]; then
  echo "smoke-native: no frame captured after 4s" >&2
  exit 1
fi

# Let it run a bit longer to catch mid-loop crashes.
sleep "$DURATION"

if ! kill -0 "$PID" 2>/dev/null; then
  echo "smoke-native: emulator died mid-loop after first frame. Log:" >&2
  tail -40 /tmp/plane-radar-smoke.log >&2
  exit 1
fi

echo "smoke-native: booted, drew ${DURATION}s worth of frames, exiting cleanly"
