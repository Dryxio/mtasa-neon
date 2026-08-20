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
  /** Mise en avant éditoriale contrôlée par le registre Neon. */
  isFeatured: boolean
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
  /** Visuels contrôlés et mis en cache par le registre Neon. */
  logoUrl?: string
  bannerUrl?: string
}

export interface ServerAddress {
  /** Adresse IPv4 ou nom d'hôte accepté par le gestionnaire de connexion natif. */
  ip: string
  port: number
}

export const DEFAULT_GAME_PORT = 22003

/**
 * Adresse directe acceptée par l'omnibox. Les deux protocoles restent valides
 * en entrée, mais les liens partagés utilisent mtaneon:// pour ouvrir le bon
 * client lorsque MTA officiel et Neon sont installés sur la même machine.
 */
export function parseServerAddress(raw: string): ServerAddress | null {
  let text = raw.trim()
  if (text === '') return null
  const scheme = /^(?:mtasa|mtaneon):\/\//i
  if (scheme.test(text)) text = text.replace(scheme, '')
  text = text.replace(/\/+$/, '')

  const match = /^([^\s/:?#@]+)(?::(\d{1,5}))?$/.exec(text)
  if (!match || !match[1]) return null
  const host = match[1].toLowerCase()
  const looksLikeIpv4 = /^\d+(?:\.\d+){3}$/.test(host)
  if (looksLikeIpv4 ? !isValidIpv4(host) : !isValidHostname(host)) return null
  const port = match[2] ? Number(match[2]) : DEFAULT_GAME_PORT
  if (port < 1 || port > 65535) return null
  return { ip: host, port }
}

export function formatNeonServerLink(address: ServerAddress): string {
  return `mtaneon://${address.ip}:${address.port}`
}

function isValidIpv4(host: string): boolean {
  const parts = host.split('.')
  return parts.length === 4 && parts.every((part) => /^\d{1,3}$/.test(part) && Number(part) <= 255)
}

function isValidHostname(host: string): boolean {
  if (host.length > 253) return false
  if (host === 'localhost') return true
  if (!host.includes('.')) return false
  return host.split('.').every((label) =>
    label.length >= 1 &&
    label.length <= 63 &&
    /^[a-z0-9](?:[a-z0-9-]*[a-z0-9])?$/.test(label),
  )
}

export function serverKey(ip: string, port: number): string {
  return `${ip}:${port}`
}
