// Netlify Function — proxy for aviationweather.gov's METAR bbox endpoint.
//
// aviationweather.gov doesn't send CORS headers, so the browser can't
// call it directly. This function fetches on behalf of the site, and
// we add Access-Control-Allow-Origin so the response reaches the
// browser. Same v1 handler pattern as adsb.mjs; wired at /api/metar
// via a [[redirects]] rule in netlify.toml.

export const handler = async (event) => {
  const p = event.queryStringParameters ?? {};
  const bbox = (p.bbox ?? "").trim();
  // "lat_min,lon_min,lat_max,lon_max" — light validation to reject junk
  // before it ever reaches upstream.
  const parts = bbox.split(",");
  if (parts.length !== 4 || !parts.every((s) => isFinite(parseFloat(s)))) {
    return json({ error: "bbox=lat_min,lon_min,lat_max,lon_max required" }, 400);
  }
  const [latMin, lonMin, latMax, lonMax] = parts.map(parseFloat);
  if (latMax - latMin > 20 || lonMax - lonMin > 40) {
    return json({ error: "bbox too large" }, 400);
  }

  const upstream =
    `https://aviationweather.gov/api/data/metar?bbox=${bbox}&format=json`;

  const ctrl = new AbortController();
  const t = setTimeout(() => ctrl.abort(), 8000);
  try {
    const r = await fetch(upstream, {
      headers: {
        "User-Agent": "plane-radar-web (github.com/benyaffe/ESP32-Plane-Radar)",
        Accept: "application/json",
      },
      signal: ctrl.signal,
    });
    if (!r.ok) return json({ error: `upstream ${r.status}` }, 502);
    const body = await r.text();
    return {
      statusCode: 200,
      headers: {
        "Content-Type": "application/json",
        "Access-Control-Allow-Origin": "*",
        // aviationweather refreshes on the cycle (~5 min); mirror that
        // so the CDN eats most repeat hits.
        "Cache-Control": "public, max-age=300",
      },
      body,
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
