import { afterEach, describe, expect, it, vi } from "vitest";
import { handler } from "../functions/geocode.mjs";

function ev(qs = {}) {
  return { queryStringParameters: qs };
}

afterEach(() => {
  vi.unstubAllGlobals();
});

describe("geocode proxy — validation", () => {
  it("returns 400 when q is missing", async () => {
    const r = await handler(ev({}));
    expect(r.statusCode).toBe(400);
    expect(JSON.parse(r.body).error).toMatch(/q required/);
  });

  it("returns 400 when q is under 3 chars", async () => {
    const r = await handler(ev({ q: "ab" }));
    expect(r.statusCode).toBe(400);
  });

  it("returns 400 when q exceeds 200 chars", async () => {
    const r = await handler(ev({ q: "a".repeat(201) }));
    expect(r.statusCode).toBe(400);
  });
});

describe("geocode proxy — happy path", () => {
  it("proxies a Nominatim response, trimming to display_name/lat/lon", async () => {
    const upstream = [
      { display_name: "1600 Amphitheatre Pkwy, Mountain View, CA",
        lat: "37.4224764", lon: "-122.0842499", extra_field: "ignored" },
      { display_name: "Bad row (non-numeric lat)", lat: "oops", lon: "0" },
    ];
    vi.stubGlobal("fetch", vi.fn(() =>
      Promise.resolve(new Response(JSON.stringify(upstream), { status: 200 })),
    ));

    const r = await handler(ev({ q: "1600 amphitheatre" }));
    expect(r.statusCode).toBe(200);
    expect(r.headers["Access-Control-Allow-Origin"]).toBe("*");
    expect(r.headers["Cache-Control"]).toMatch(/max-age=86400/);
    const body = JSON.parse(r.body);
    expect(body).toHaveLength(1);
    expect(body[0].display_name).toContain("Amphitheatre");
    expect(body[0].lat).toBeCloseTo(37.4224764, 5);
    expect(body[0].lon).toBeCloseTo(-122.0842499, 5);
  });

  it("hits the Nominatim search endpoint with the query URL-encoded", async () => {
    const fetchMock = vi.fn(() =>
      Promise.resolve(new Response("[]", { status: 200 })),
    );
    vi.stubGlobal("fetch", fetchMock);
    await handler(ev({ q: "742 Evergreen Terrace" }));
    const url = String(fetchMock.mock.calls[0][0]);
    expect(url).toContain("nominatim.openstreetmap.org/search");
    expect(url).toContain("q=742+Evergreen+Terrace");
  });

  it("sends a User-Agent header identifying the app", async () => {
    const fetchMock = vi.fn(() =>
      Promise.resolve(new Response("[]", { status: 200 })),
    );
    vi.stubGlobal("fetch", fetchMock);
    await handler(ev({ q: "san francisco" }));
    const opts = fetchMock.mock.calls[0][1];
    expect(String(opts.headers["User-Agent"])).toMatch(/plane-radar-web/);
  });
});

describe("geocode proxy — failure paths", () => {
  it("returns 502 on upstream non-OK", async () => {
    vi.stubGlobal("fetch", vi.fn(() =>
      Promise.resolve(new Response("boom", { status: 503 })),
    ));
    const r = await handler(ev({ q: "san francisco" }));
    expect(r.statusCode).toBe(502);
    expect(JSON.parse(r.body).error).toMatch(/upstream 503/);
  });

  it("returns 502 on fetch throw (dns / network)", async () => {
    vi.stubGlobal("fetch", vi.fn(() => Promise.reject(new Error("dns"))));
    const r = await handler(ev({ q: "san francisco" }));
    expect(r.statusCode).toBe(502);
    expect(JSON.parse(r.body).error).toMatch(/fetch failed/);
  });
});
