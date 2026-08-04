import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// https://vite.dev/config/
export default defineConfig({
  plugins: [react()],
  // Chemins relatifs : dans le client, la page est servie depuis
  // http://mta/local/index.html — les assets doivent se résoudre à côté.
  base: './',
})
