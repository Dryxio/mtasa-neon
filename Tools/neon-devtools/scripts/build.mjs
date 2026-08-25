import { cp, mkdir, rm } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'

const root = fileURLToPath(new URL('..', import.meta.url))
const source = `${root}/src`
const output = `${root}/dist`

await rm(output, { recursive: true, force: true })
await mkdir(output, { recursive: true })
await cp(source, output, { recursive: true })
