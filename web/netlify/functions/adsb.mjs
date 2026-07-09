// Netlify Function (v1 handler API) — proxy for opendata.adsb.fi + airplanes.live.
//
// Sticks to the v1 `export const handler` signature because the v2
// `export default` API + `config.path` route registration wouldn't
// resolve on our site's runtime (function was in the deploy manifest
// but every URL returned 404). v1 is universally supported and works
// with the plain function URL `/.netlify/functions/adsb`; the site's
// [[redirects]] rule in netlify.toml aliases /api/adsb to it.

export const handler = async (event) => {
  const p = event.queryStringParameters ?? {};
  const lat = parseFloat(p.lat ?? "");
  const lon = parseFloat(p.lon ?? "");
  const nm = parseFloat(p.nm ?? "25");
  if (!isFinite(lat) || !isFinite(lon) || !isFinite(nm)) {
    return json({ error: "lat, lon, nm are required numeric query params" }, 400);
  }
  if (nm <= 0 || nm > 250) {
    return json({ error: "nm out of range" }, 400);
  }

  const upstreams = [
    `https://api.airplanes.live/v2/point/${lat.toFixed(4)}/${lon.toFixed(4)}/${nm.toFixed(1)}`,
    `https://opendata.adsb.fi/api/v3/lat/${lat.toFixed(4)}/lon/${lon.toFixed(4)}/dist/${nm.toFixed(1)}`,
  ];

  let body = null;
  let lastStatus = 0;
  for (const upstream of upstreams) {
    const ctrl = new AbortController();
    const t = setTimeout(() => ctrl.abort(), 4000);
    try {
      const r = await fetch(upstream, {
        headers: {
          "User-Agent": "plane-radar-web (github.com/benyaffe/ESP32-Plane-Radar)",
          Accept: "application/json",
        },
        signal: ctrl.signal,
      });
      if (r.ok) { body = await r.text(); break; }
      lastStatus = r.status;
    } catch {
      lastStatus = 599;
    } finally {
      clearTimeout(t);
    }
  }
  if (body === null) {
    return json({ error: `upstream ${lastStatus}` }, 502);
  }
  return {
    statusCode: 200,
    headers: {
      "Content-Type": "application/json",
      "Access-Control-Allow-Origin": "*",
      "Cache-Control": "no-store",
    },
    body,
  };
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
