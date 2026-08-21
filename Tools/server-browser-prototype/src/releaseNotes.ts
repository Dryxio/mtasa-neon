import releaseCatalog from './releaseNotes.json'

export type ReleaseTone = 'players' | 'creators' | 'servers'

export interface ReleaseSection {
  label: string
  tone: ReleaseTone
  items: readonly string[]
}

export interface NeonRelease {
  build: number
  date: string
  title: string
  summary: string
  highlights: readonly string[]
  sections: readonly ReleaseSection[]
}

interface ReleaseCatalog {
  schema: number
  releases: NeonRelease[]
}

const catalog = releaseCatalog as ReleaseCatalog

// The catalogue is also validated by the release workflow before any public
// build starts. Keeping the browser on the same file prevents the notification,
// history page and GitHub release notes from drifting apart.
export const NEON_RELEASES: readonly NeonRelease[] = catalog.releases
export const LATEST_NEON_RELEASE = NEON_RELEASES[0]!

export function findNeonRelease(build: number): NeonRelease | undefined {
  return NEON_RELEASES.find((release) => release.build === build)
}

export function formatReleaseVersion(release: NeonRelease): string {
  return `${release.date.replaceAll('-', '.')}.${release.build}`
}

export function formatReleaseDate(release: NeonRelease): string {
  return new Intl.DateTimeFormat('en-US', {
    day: 'numeric',
    month: 'long',
    year: 'numeric',
    timeZone: 'UTC',
  }).format(new Date(`${release.date}T00:00:00Z`))
}
