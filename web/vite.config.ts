import { defineConfig } from "vite";

// Static site — build outputs to web/dist/, which Cloudflare Pages serves.
// Pages Functions in web/functions/ are picked up automatically.
export default defineConfig({
  base: "./",
  build: {
    outDir: "dist",
    emptyOutDir: true,
    target: "es2022",
  },
  server: {
    port: 5173,
    strictPort: false,
  },
});
