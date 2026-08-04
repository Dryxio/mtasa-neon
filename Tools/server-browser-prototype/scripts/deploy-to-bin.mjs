/**
 * Copie le bundle construit (dist/) vers le dossier que le client Neon sert
 * via le scheme local CEF : <repo>/Bin/mta/cef/serverbrowser/.
 *
 * Usage : npm run build:client   (build + copie)
 * Le client lit ce dossier via CServerBrowserWeb::GetUiRootPath()
 * (CalcMTASAPath("MTA/cef/serverbrowser")).
 */
import { cpSync, existsSync, mkdirSync, rmSync } from 'node:fs'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const root = dirname(dirname(fileURLToPath(import.meta.url)))
const dist = join(root, 'dist')
const target = resolve(root, '..', '..', 'Bin', 'mta', 'cef', 'serverbrowser')

if (!existsSync(dist)) {
  console.error('dist/ introuvable — lancer "npm run build" d\'abord.')
  process.exit(1)
}

rmSync(target, { recursive: true, force: true })
mkdirSync(target, { recursive: true })
cpSync(dist, target, { recursive: true })
console.log(`Bundle copié vers ${target}`)
