import type { ServerItem } from './types'

/**
 * Analyse de la barre de recherche : texte libre, `langs:` pour les langues et
 * `@pseudo` pour retrouver un joueur. Les tags restent des métadonnées backend
 * mais ne participent plus à l'interface volontairement épurée.
 */
export interface ParsedQuery {
  text: string[]
  langs: string[]
  users: string[]
}

export function parseQuery(raw: string): ParsedQuery {
  const parsed: ParsedQuery = { text: [], langs: [], users: [] }
  for (const token of raw.trim().toLowerCase().split(/\s+/)) {
    if (token === '') continue
    if (token.startsWith('langs:')) {
      parsed.langs.push(...token.slice(6).split(',').filter(Boolean))
    } else if (token.startsWith('@') && token.length > 1) {
      parsed.users.push(token.slice(1))
    } else {
      parsed.text.push(token)
    }
  }
  return parsed
}

export interface BrowserFilters {
  hideFull: boolean
  hideEmpty: boolean
  hideLocked: boolean
  hideIncompatible: boolean
  hideOffline: boolean
}

export const DEFAULT_FILTERS: BrowserFilters = {
  hideFull: false,
  hideEmpty: false,
  hideLocked: false,
  hideIncompatible: false,
  hideOffline: false,
}

function matchesQuery(server: ServerItem, query: ParsedQuery): boolean {
  for (const term of query.text) {
    const haystack = `${server.name} ${server.tagline} ${server.description} ${server.gameMode} ${server.ip}`.toLowerCase()
    if (!haystack.includes(term)) return false
  }
  for (const lang of query.langs) {
    if (!server.languages.some((l) => l.toLowerCase().includes(lang))) return false
  }
  for (const user of query.users) {
    if (!server.playerList.some((p) => p.name.toLowerCase().includes(user))) return false
  }
  return true
}

/**
 * Filtre + tri de la liste visible. Tri par défaut du client natif conservé :
 * joueurs décroissants, serveurs hors ligne relégués en fin de liste.
 */
export function visibleServers(
  servers: readonly ServerItem[],
  rawQuery: string,
  filters: BrowserFilters,
): ServerItem[] {
  const query = parseQuery(rawQuery)
  const result = servers.filter((server) => {
    if (filters.hideFull && server.players >= server.maxPlayers) return false
    if (filters.hideEmpty && server.players === 0) return false
    if (filters.hideLocked && server.passworded) return false
    if (filters.hideIncompatible && !server.isCompatible) return false
    if (filters.hideOffline && server.scanState === 'offline') return false
    return matchesQuery(server, query)
  })
  result.sort((a, b) => {
    if (a.isFeatured !== b.isFeatured) return a.isFeatured ? -1 : 1
    const aOffline = a.scanState === 'offline' ? 1 : 0
    const bOffline = b.scanState === 'offline' ? 1 : 0
    if (aOffline !== bOffline) return aOffline - bOffline
    if (a.players !== b.players) return b.players - a.players
    return a.name.localeCompare(b.name)
  })
  return result
}
