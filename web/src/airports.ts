// Typeahead search + selection over the compact airport_index payload.

import type { AirportIndexRow } from "./data";
import { setCenter } from "./state";

// Case-insensitive substring match on ICAO, IATA, city, or name.
// Ranks exact prefix matches on ICAO/IATA first, then substring hits,
// then general name matches.
function scoreRow(row: AirportIndexRow, q: string): number {
  const [icao, iata, city, name] = row;
  const icaoL = icao.toLowerCase();
  const iataL = iata.toLowerCase();
  const cityL = city.toLowerCase();
  const nameL = name.toLowerCase();
  if (icaoL === q || iataL === q) return 1000;
  if (icaoL.startsWith(q) || iataL.startsWith(q)) return 500;
  if (cityL.startsWith(q) || nameL.startsWith(q)) return 200;
  if (icaoL.includes(q) || iataL.includes(q)) return 100;
  if (cityL.includes(q) || nameL.includes(q)) return 50;
  return 0;
}

export function search(index: AirportIndexRow[], query: string, limit = 8): AirportIndexRow[] {
  const q = query.trim().toLowerCase();
  if (q.length === 0) return [];
  const scored: [number, AirportIndexRow][] = [];
  for (const row of index) {
    const s = scoreRow(row, q);
    if (s > 0) scored.push([s, row]);
  }
  scored.sort((a, b) => b[0] - a[0]);
  return scored.slice(0, limit).map(([, row]) => row);
}

interface MountOpts {
  input: HTMLInputElement;
  results: HTMLElement;
  index: AirportIndexRow[];
  onSelected: () => void;
}

// Wire the typeahead: input events populate the results list, clicks
// select an airport (via setCenter), Enter picks the first match.
export function mountTypeahead(opts: MountOpts): void {
  const { input, results, index, onSelected } = opts;
  let focusIdx = -1;
  let current: AirportIndexRow[] = [];

  function render() {
    results.innerHTML = "";
    focusIdx = -1;
    for (let i = 0; i < current.length; i++) {
      const row = current[i];
      const [icao, iata, city, name] = row;
      const li = document.createElement("li");
      li.setAttribute("role", "option");
      li.innerHTML =
        `<span class="icao">${icao}${iata ? " · " + iata : ""}</span>` +
        `${city ? city + " — " : ""}${name}`;
      li.addEventListener("mousedown", (e) => {
        e.preventDefault();
        pick(row);
      });
      results.appendChild(li);
    }
    results.hidden = current.length === 0;
  }

  function pick(row: AirportIndexRow) {
    const [icao, iata, city, name, lat, lon] = row;
    void iata; void name; void city;
    setCenter(lat, lon, icao);
    input.value = icao;
    results.hidden = true;
    onSelected();
  }

  function selectAt(i: number) {
    const items = Array.from(results.querySelectorAll("li"));
    if (focusIdx >= 0 && items[focusIdx]) {
      items[focusIdx].removeAttribute("aria-selected");
    }
    focusIdx = Math.max(0, Math.min(items.length - 1, i));
    if (items[focusIdx]) {
      items[focusIdx].setAttribute("aria-selected", "true");
      items[focusIdx].scrollIntoView({ block: "nearest" });
    }
  }

  input.addEventListener("input", () => {
    current = search(index, input.value);
    render();
  });
  input.addEventListener("focus", () => {
    if (current.length > 0) results.hidden = false;
  });
  input.addEventListener("blur", () => {
    // Delay so a click can register.
    setTimeout(() => (results.hidden = true), 120);
  });
  input.addEventListener("keydown", (e) => {
    if (e.key === "ArrowDown") { e.preventDefault(); selectAt(focusIdx + 1); }
    else if (e.key === "ArrowUp") { e.preventDefault(); selectAt(focusIdx - 1); }
    else if (e.key === "Enter") {
      e.preventDefault();
      const pickIdx = focusIdx >= 0 ? focusIdx : 0;
      if (current[pickIdx]) pick(current[pickIdx]);
    } else if (e.key === "Escape") {
      results.hidden = true;
      input.blur();
    }
  });
}
