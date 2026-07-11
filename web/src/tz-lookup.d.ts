// Ambient module declaration for the untyped `tz-lookup` package.
// Runtime is CommonJS with a single default export: (lat, lon) => IANA tz.
declare module "tz-lookup" {
  export default function tzlookup(lat: number, lon: number): string;
}
