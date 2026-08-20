import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// https://vite.dev/config/
export default defineConfig({
  plugins: [react()],
  // The browser demo imports the same GTA fonts as the packaged CEF bundle.
  // They live at the repository level, so the development server must expose
  // that tree instead of silently falling back to generic system fonts.
  server: {
    fs: {
      allow: ['../..'],
    },
  },
  // Chemins relatifs : dans le client, la page est servie depuis
  // http://mta/local/index.html — les assets doivent se résoudre à côté.
  base: './',
})
