// import { defineConfig } from 'vite'
// import react from '@vitejs/plugin-react'

// // https://vite.dev/config/
// export default defineConfig({
//   plugins: [react()],
//   server: {
//     proxy: {
//       '/api': 'http://localhost:3000',
//     },
//   },
// })

import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// laptop hosting
// Expose the Vite dev server on the hotspot/LAN so the UI can be opened
// from the host laptop or another device on the same local network.
export default defineConfig({
  plugins: [react()],
  server: {
    host: '0.0.0.0',
    port: 5173,
    strictPort: true,
    proxy: {
      '/api': 'http://localhost:3000',
    },
  },
})