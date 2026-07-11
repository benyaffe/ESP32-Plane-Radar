// @vitest-environment happy-dom
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { makeTapDiscriminator } from "./input";

// The tap discriminator is the two-gesture state machine that turns raw
// clicks into "single" (adjust screen) vs "double" (advance screen ring).
// Guards the 250-ms window logic — a regression here breaks EVERY
// interaction on the site.

beforeEach(() => {
  vi.useFakeTimers();
});

afterEach(() => {
  vi.useRealTimers();
});

describe("makeTapDiscriminator", () => {
  it("fires 'single' after the window elapses on a lone tap", () => {
    const seen: string[] = [];
    const disc = makeTapDiscriminator(t => seen.push(t), 250);
    disc.tap();
    expect(seen).toEqual([]);              // still pending
    vi.advanceTimersByTime(249);
    expect(seen).toEqual([]);
    vi.advanceTimersByTime(1);
    expect(seen).toEqual(["single"]);
  });

  it("fires 'double' immediately on the second tap within the window", () => {
    const seen: string[] = [];
    const disc = makeTapDiscriminator(t => seen.push(t), 250);
    disc.tap();
    disc.tap();
    expect(seen).toEqual(["double"]);      // no timer wait
    // No 'single' should follow after the window elapses.
    vi.advanceTimersByTime(500);
    expect(seen).toEqual(["double"]);
  });

  it("fires two 'single's when the gap exceeds the window", () => {
    const seen: string[] = [];
    const disc = makeTapDiscriminator(t => seen.push(t), 250);
    disc.tap();
    vi.advanceTimersByTime(300);
    disc.tap();
    vi.advanceTimersByTime(300);
    expect(seen).toEqual(["single", "single"]);
  });

  it("treats a third rapid tap as the start of a new count", () => {
    const seen: string[] = [];
    const disc = makeTapDiscriminator(t => seen.push(t), 250);
    disc.tap();
    disc.tap();               // 'double' fires, count resets to 0
    disc.tap();               // this is a fresh 'single' pending
    expect(seen).toEqual(["double"]);
    vi.advanceTimersByTime(250);
    expect(seen).toEqual(["double", "single"]);
  });

  it("respects a custom windowMs", () => {
    const seen: string[] = [];
    const disc = makeTapDiscriminator(t => seen.push(t), 100);
    disc.tap();
    vi.advanceTimersByTime(99);
    expect(seen).toEqual([]);
    vi.advanceTimersByTime(1);
    expect(seen).toEqual(["single"]);
  });
});
