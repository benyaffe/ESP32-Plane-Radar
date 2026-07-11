import { afterEach, describe, expect, it, vi } from "vitest";
import { loadIndexData } from "./data";

// loadIndexData is the entrypoint the app uses to bootstrap the airport
// typeahead. Guards that (a) the fetch URL uses the caller-supplied
// basePath, (b) a valid response parses, (c) an HTTP failure throws a
// diagnostic error.

afterEach(() => {
  vi.unstubAllGlobals();
});

const CANNED = [
  ["KSFO", "SFO", "San Francisco", "SFO Intl", 37.6188, -122.375],
  ["KOAK", "OAK", "Oakland", "OAK Intl", 37.7213, -122.2214],
];

describe("loadIndexData", () => {
  it("uses the provided basePath in the fetch URL", async () => {
    const fetchMock = vi.fn((_input: RequestInfo | URL) =>
      Promise.resolve(new Response(JSON.stringify(CANNED), { status: 200 })),
    );
    vi.stubGlobal("fetch", fetchMock);
    await loadIndexData("data");
    const url = String(fetchMock.mock.calls[0]?.[0] ?? "");
    expect(url).toContain("data/airport_index.json");
  });

  it("threads a custom basePath through", async () => {
    const fetchMock = vi.fn((_input: RequestInfo | URL) =>
      Promise.resolve(new Response(JSON.stringify(CANNED), { status: 200 })),
    );
    vi.stubGlobal("fetch", fetchMock);
    await loadIndexData("public/data");
    const url = String(fetchMock.mock.calls[0]?.[0] ?? "");
    expect(url).toContain("public/data/airport_index.json");
  });

  it("returns { airportIndex } populated from the response body", async () => {
    vi.stubGlobal("fetch", vi.fn(() =>
      Promise.resolve(new Response(JSON.stringify(CANNED), { status: 200 })),
    ));
    const result = await loadIndexData("data");
    expect(result.airportIndex.length).toBe(2);
    expect(result.airportIndex[0][0]).toBe("KSFO");
    expect(result.airportIndex[1][1]).toBe("OAK");
  });

  it("throws a diagnostic error on non-OK responses", async () => {
    vi.stubGlobal("fetch", vi.fn(() =>
      Promise.resolve(new Response("", { status: 404 })),
    ));
    await expect(loadIndexData("data")).rejects.toThrow(/airport_index\.json.*404/);
  });
});
