# SDL emulator golden framebuffers

Reference PPM screenshots the SDL emulator should produce at boot for each
built-in view. Compared byte-for-byte by `scripts/dev/snapshot-diff.sh`
so pixel regressions in the LovyanGFX render path show up as a
red-diff PNG in `/tmp/plane-radar-diff.ppm`.

## When to regen

Run `scripts/dev/snapshot-diff.sh --update` whenever you intentionally
change the render — new theme colors, tweaked bezel, added label,
different font size, etc. Commit the resulting PPM so the next diff
runs against the new baseline.

## Why not in CI

The SDL emulator needs a display to init the panel. Even under
`SDL_VIDEODRIVER=dummy` the LovyanGFX SDL backend touches window
management APIs that Xvfb doesn't fully cover. Local pre-commit check
only until the native build grows a headless snapshot mode (see
plan follow-up P3.7).

## Adding new goldens

1. Boot the emulator in the state you want to lock (e.g., weather view
   with a specific METAR configuration).
2. Run `scripts/dev/snap.sh /tmp/plane-radar-screenshot.png`.
3. Copy `/tmp/plane-radar-screenshot.ppm` into this dir with a
   descriptive name (`weather_kSFO.ppm`, `cockpit.ppm`).
4. Update `snapshot-diff.sh` if you need a new named view.
