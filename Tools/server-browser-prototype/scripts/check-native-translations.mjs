import { readFile } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'

const prototypeRoot = fileURLToPath(new URL('..', import.meta.url))
const reactSource = await readFile(`${prototypeRoot}/src/i18n.tsx`, 'utf8')
const nativeSource = await readFile(
  `${prototypeRoot}/../../Client/core/ServerBrowser/CServerBrowserWeb.cpp`,
  'utf8',
)

const reactKeys = [...reactSource.matchAll(/^  '([^']+)':/gm)].map((match) => match[1])
const nativeKeys = [...nativeSource.matchAll(/\{\s*"([^"]+)"\s*,\s*_td\(/g)].map(
  (match) => match[1],
)

const missingNative = reactKeys.filter((key) => !nativeKeys.includes(key))
const missingReact = nativeKeys.filter((key) => !reactKeys.includes(key))
const duplicates = (keys) => keys.filter((key, index) => keys.indexOf(key) !== index)

if (
  missingNative.length > 0 ||
  missingReact.length > 0 ||
  duplicates(reactKeys).length > 0 ||
  duplicates(nativeKeys).length > 0
) {
  console.error('React/native translation catalogues are out of sync.')
  if (missingNative.length > 0) console.error('Missing native keys:', missingNative.join(', '))
  if (missingReact.length > 0) console.error('Missing React keys:', missingReact.join(', '))
  if (duplicates(reactKeys).length > 0) console.error('Duplicate React keys:', duplicates(reactKeys).join(', '))
  if (duplicates(nativeKeys).length > 0) console.error('Duplicate native keys:', duplicates(nativeKeys).join(', '))
  process.exitCode = 1
}
