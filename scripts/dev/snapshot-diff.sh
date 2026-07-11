#!/usr/bin/env bash
# Compare a fresh SDL emulator screenshot against a stored golden.
#
# What this catches: pixel-level regressions in the LovyanGFX render path
# — a font swap, a bezel radius drift, a color constant typo that
# survives type-check but produces the wrong pixels on the desk toy.
#
# Not run in CI (needs a display for SDL). Local development harness
# only. When a golden intentionally shifts (theme change, layout tweak),
# `--update` overwrites it in-place.
#
# Usage:
#   scripts/dev/snapshot-diff.sh                 # diff against test/goldens/radar_default.ppm
#   scripts/dev/snapshot-diff.sh <golden.ppm>    # diff against a different golden
#   scripts/dev/snapshot-diff.sh --update        # accept current output as the new golden
#
# Exit codes:
#   0 = pixel-identical
#   1 = differs (writes /tmp/plane-radar-diff.ppm)
#   2 = missing golden or emulator binary
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
GOLDENS_DIR="$REPO_ROOT/test/goldens"
DEFAULT_GOLDEN="$GOLDENS_DIR/radar_default.ppm"
FRESH="/tmp/plane-radar-screenshot.ppm"
DIFF_OUT="/tmp/plane-radar-diff.ppm"

if [[ "${1:-}" == "--update" ]]; then
  mkdir -p "$GOLDENS_DIR"
  "$REPO_ROOT/scripts/dev/snap.sh" "$FRESH.png" >/dev/null
  cp "$FRESH" "$DEFAULT_GOLDEN"
  echo "Updated $DEFAULT_GOLDEN ($(stat -f %z "$DEFAULT_GOLDEN") bytes)" >&2
  exit 0
fi

GOLDEN="${1:-$DEFAULT_GOLDEN}"
if [[ ! -f "$GOLDEN" ]]; then
  echo "snapshot-diff: golden $GOLDEN not found. Run with --update to bootstrap." >&2
  exit 2
fi

"$REPO_ROOT/scripts/dev/snap.sh" "$FRESH.png" >/dev/null

if cmp -s "$GOLDEN" "$FRESH"; then
  echo "snapshot-diff: pixel-identical to $GOLDEN"
  exit 0
fi

# Not identical — build a red-mask diff so the diverged region is visible.
# ImageMagick's `compare` if available, else just report byte-count delta.
if command -v compare >/dev/null; then
  compare -metric AE -fuzz 3% "$GOLDEN" "$FRESH" "$DIFF_OUT" 2>&1 | \
    xargs -I{} echo "snapshot-diff: {} pixels differ; see $DIFF_OUT"
else
  gsize=$(stat -f %z "$GOLDEN")
  fsize=$(stat -f %z "$FRESH")
  echo "snapshot-diff: bytes differ ($gsize vs $fsize). Install ImageMagick for a pixel-diff." >&2
fi
exit 1
