// Tap discriminator — same shape as the firmware's BootTap state machine
// (400 ms window; count taps, dispatch on quiet).

export type Tap = "single" | "double" | "triple";

export function makeTapDiscriminator(onTap: (t: Tap) => void, windowMs = 400) {
  let count = 0;
  let timer: number | null = null;

  function flush() {
    if (count === 0) return;
    const n = count;
    count = 0;
    timer = null;
    if (n === 1) onTap("single");
    else if (n === 2) onTap("double");
    else onTap("triple");  // 3+
  }

  function tap() {
    count += 1;
    if (count >= 3) {
      // Triple fires immediately, no reason to wait.
      if (timer !== null) {
        clearTimeout(timer);
        timer = null;
      }
      const n = count;
      count = 0;
      onTap(n === 2 ? "double" : "triple");
      return;
    }
    if (timer !== null) clearTimeout(timer);
    timer = window.setTimeout(flush, windowMs);
  }

  return { tap };
}
