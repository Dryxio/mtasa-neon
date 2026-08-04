/**
 * Modèle de données du navigateur de serveurs Neon.
 *
 * Reste aligné sur `CServerListItem` (Client/core/ServerBrowser/CServerList.h)
 * pour que le futur bridge C++ ↔ CEF sérialise un serveur en JSON sans
 * adaptation côté UI. Les champs de présentation riches (langues, pays,
 * liens et description) viennent du registre officiel Neon ; ASE reste la
 * source de vérité pour l'état dynamique du serveur.
 */

export const ServerSource = {
  Internet: 'internet',
  Lan: 'lan',
  Favourites: 'favourites',
  Recent: 'recent',
} as const

export type ServerSource = (typeof ServerSource)[keyof typeof ServerSource]

export const ALL_SOURCES: readonly ServerSource[] = [
  ServerSource.Internet,
  ServerSource.Favourites,
  ServerSource.Recent,
]

export const SOURCE_LABELS: Record<ServerSource, string> = {
  internet: 'Neon servers',
  lan: 'Local',
  favourites: 'Favourites',
  recent: 'Recent',
}

/** Progression du scan ASE d'un serveur, agrégée pour l'UI. */
export type ServerScanState = 'queued' | 'querying' | 'online' | 'offline'

export interface ServerPlayer {
  name: string
  score?: number
  ping?: number
}

export type LinkKind =
  | 'website'
  | 'discord'
  | 'instagram'
  | 'x'
  | 'facebook'
  | 'vk'
  | 'youtube'
  | 'tiktok'

export interface ServerLink {
  kind: LinkKind
  label: string
  url: string
}

export interface ServerItem {
  /** Clé stable "ip:gamePort" — identique à la clé du client natif. */
  id: string
  ip: string
  gamePort: number
  httpPort?: number

  name: string
  /** Ligne d'accroche affichée sous le nom dans la liste. */
  tagline: string
  /** Description longue du panneau de détails. */
  description: string
  gameMode: string
  map: string
  version: string
  isCompatible: boolean

  players: number
  maxPlayers: number
  ping?: number

  passworded: boolean
  requiresSerial: boolean
  isStatusVerified: boolean

  scanState: ServerScanState
  playerList: ServerPlayer[]

  isFavourite: boolean
  lastPlayedAt?: number

  /** Code pays ISO 3166-1 alpha-2 annoncé par le serveur. */
  country?: string
  /** Plusieurs régions d'accueil possibles pour un serveur Neon distribué. */
  countries?: string[]
  /** Langues parlées, noms affichables ("French", "English"…). */
  languages: string[]
  tags: string[]
  links: ServerLink[]
  /** Couleur de marque pour l'icône/bannière générée. */
  accent?: string
}

export interface ServerAddress {
  ip: string
  port: number
}

export const DEFAULT_GAME_PORT = 22003

/** "ip", "ip:port" ou "mtasa://ip:port" — mêmes formes que CConnectManager. */
export function parseServerAddress(raw: string): ServerAddress | null {
  let text = raw.trim()
  if (text === '') return null
  const scheme = /^mtasa:\/\//i
  if (scheme.test(text)) text = text.replace(scheme, '')
  text = text.replace(/\/+$/, '')

  const match = /^(\d{1,3}(?:\.\d{1,3}){3})(?::(\d{1,5}))?$/.exec(text)
  if (!match || !match[1]) return null
  const port = match[2] ? Number(match[2]) : DEFAULT_GAME_PORT
  if (port < 1 || port > 65535) return null
  return { ip: match[1], port }
}

export function serverKey(ip: string, port: number): string {
  return `${ip}:${port}`
}

/** 12348 → "12.348" (séparateur point, comme la maquette). */
export function formatThousands(n: number): string {
  return n.toString().replace(/\B(?=(\d{3})+(?!\d))/g, '.')
}

/** 13421 → "13.4 K" */
export function formatCompact(n: number): string {
  if (n < 1000) return String(n)
  return `${(n / 1000).toFixed(1).replace('.', ',').replace(',0', '')} K`.replace(',', '.')
}
