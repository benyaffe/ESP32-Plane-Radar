// Netlify Function — proxy for Nominatim (OpenStreetMap) geocoding.
//
// Nominatim's usage policy requires a real User-Agent header and asks
// callers to cache aggressively. Running through this proxy lets us
// set the header once server-side and let the Netlify CDN cache
// per-query, so N users typing the same address collapse to one
// upstream fetch. Wired at /api/geocode via a [[redirects]] rule in
// netlify.toml.

export const handler = async (event) => {
  const p = event.queryStringParameters ?? {};
  const q = (p.q ?? "").trim();
  if (q.length < 3 || q.length > 200) {
    return json({ error: "q required (3-200 chars)" }, 400);
  }
  const params = new URLSearchParams({
    q,
    format: "json",
    limit: "5",
    "accept-language": "en",
    addressdetails: "0",
    dedupe: "1",
  });
  const upstream = `https://nominatim.openstreetmap.org/search?${params}`;

  const ctrl = new AbortController();
  const t = setTimeout(() => ctrl.abort(), 5000);
  try {
    const r = await fetch(upstream, {
      headers: {
        // Required by Nominatim ToS — must identify the app.
        "User-Agent": "plane-radar-web (github.com/benyaffe/ESP32-Plane-Radar)",
        Accept: "application/json",
      },
      signal: ctrl.signal,
    });
    if (!r.ok) return json({ error: `upstream ${r.status}` }, 502);
    const rows = await r.json();
    // Trim to only the fields the client needs.
    const trimmed = Array.isArray(rows)
      ? rows.map((row) => ({
          display_name: String(row.display_name ?? ""),
          lat: Number(row.lat),
          lon: Number(row.lon),
        })).filter((row) => isFinite(row.lat) && isFinite(row.lon))
      : [];
    return {
      statusCode: 200,
      headers: {
        "Content-Type": "application/json",
        "Access-Control-Allow-Origin": "*",
        // CDN caches per-query for a day, revalidating for a week —
        // ordinary address lookups don't move.
        "Cache-Control": "public, max-age=86400, stale-while-revalidate=604800",
      },
      body: JSON.stringify(trimmed),
    };
  } catch (err) {
    return json({ error: `fetch failed: ${String(err)}` }, 502);
  } finally {
    clearTimeout(t);
  }
};

function json(payload, statusCode = 200) {
  return {
    statusCode,
    headers: {
      "Content-Type": "application/json",
      "Access-Control-Allow-Origin": "*",
    },
    body: JSON.stringify(payload),
  };
}
