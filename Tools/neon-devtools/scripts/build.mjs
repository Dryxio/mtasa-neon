import { cp, mkdir, rm } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'

const source = fileURLToPath(new URL('../src/', import.meta.url))
// The client serves this directory directly as MTA/cef/devtools. Keeping the
// generated document in Shared/data makes the regular data install, CI
// artifact composition, NSIS installer and auto-updater share one runtime
// source instead of relying on a developer-only copy into Bin.
const output = fileURLToPath(new URL('../../../Shared/data/MTA San Andreas/MTA/cef/devtools/', import.meta.url))

await rm(output, { recursive: true, force: true })
await mkdir(output, { recursive: true })
await cp(source, output, { recursive: true })
