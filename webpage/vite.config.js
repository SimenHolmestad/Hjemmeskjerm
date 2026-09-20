import { defineConfig } from "vite";
import netatmo from "./server/netatmo.js";

// Datanova sender ingen CORS-headere, så kallet må gå via dev-/preview-serveren.
const toyenbadetProxy = {
  "/api/toyenbadet": {
    target: "https://web.datanova.com",
    changeOrigin: true,
    rewrite: () =>
      "/OslobadeneERPTicketMonitor?handler=VisitorData&shopNo=12",
  },
};

export default defineConfig({
  plugins: [netatmo()],
  server: { proxy: toyenbadetProxy },
  preview: { proxy: toyenbadetProxy },
});
