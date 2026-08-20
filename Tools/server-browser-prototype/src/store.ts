import { useSyncExternalStore } from 'react'
import { backend } from './backend'
import type { ConnectError, ConnectStage } from './backend'
import { publishConnectionUiEvent, subscribeConnectionUiEvents } from './connectionUiEvents'
import { DEFAULT_FILTERS, type BrowserFilters } from './search'
import type { ServerAddress, ServerItem, ServerSource } from './types'
import { serverKey } from './types'

/**
 * État applicatif du navigateur, indépendant de React (module singleton +
 * useSyncExternalStore). Les composants ne parlent qu'au store ; le store ne
 * parle qu'au backend.
 */

export interface ConnectFlow {
  phase: 'idle' | 'password' | 'connecting' | 'failed'
  address: ServerAddress | null
  serverName?: string
  stage?: ConnectStage
  statusMessage?: string
  error?: ConnectError
}

export interface NetworkStats {
  playersOnline: number
  peakPlayers: number
  serverCount: number
}

export interface BrowserState {
  source: ServerSource
  /** Serveurs de la source courante, indexés par id, ordre d'arrivée. */
  servers: ReadonlyMap<string, ServerItem>
  refreshing: boolean
  progress: { scanned: number; total: number } | null
  query: string
  filters: BrowserFilters
  selectedId: string | null
  connect: ConnectFlow
  stats: NetworkStats
  notice: string | null
}

let state: BrowserState = {
  source: 'internet',
  servers: new Map(),
  refreshing: false,
  progress: null,
  query: '',
  filters: { ...DEFAULT_FILTERS },
  selectedId: null,
  connect: { phase: 'idle', address: null },
  stats: { playersOnline: 0, peakPlayers: 0, serverCount: 0 },
  notice: null,
}

const subscribers = new Set<() => void>()

function setState(patch: Partial<BrowserState>): void {
  state = { ...state, ...patch }
  for (const fn of subscribers) fn()
}

export function useBrowserState(): BrowserState {
  return useSyncExternalStore(
    (fn) => {
      subscribers.add(fn)
      return () => subscribers.delete(fn)
    },
    () => state,
  )
}

backend.subscribe((event) => {
  if (event.type.startsWith('connect-')) publishConnectionUiEvent(event as Extract<typeof event, { type: `connect-${string}` }>)

  switch (event.type) {
    case 'servers-reset':
      if (event.source === state.source) {
        setState({ servers: new Map(), selectedId: null })
      }
      break
    case 'server-updated': {
      if (event.source !== state.source && state.source !== 'favourites') break
      if (!state.servers.has(event.server.id) && event.source !== state.source) break
      const servers = new Map(state.servers)
      if (servers.has(event.server.id) || event.source === state.source) {
        servers.set(event.server.id, {
          ...event.server,
          isFavourite: event.server.isFavourite,
        })
        setState({
          servers,
          selectedId: state.selectedId ?? event.server.id,
          notice: null,
        })
      }
      break
    }
    case 'refresh-progress':
      if (event.source === state.source) {
        setState({ progress: { scanned: event.scanned, total: event.total } })
        void refreshStats()
      }
      break
    case 'refresh-finished':
      if (event.source === state.source) setState({ refreshing: false, progress: null })
      void refreshStats()
      break
    case 'refresh-failed':
      if (event.source === state.source) setState({ refreshing: false, progress: null, notice: event.message })
      break
    case 'connect-started':
      setState({
        connect: { phase: 'connecting', address: event.address, serverName: event.serverName, stage: 'contacting' },
      })
      break
    case 'connect-progress':
      if (state.connect.phase === 'connecting') {
        setState({ connect: { ...state.connect, stage: event.stage, statusMessage: event.message } })
      }
      break
    case 'connect-failed': {
      const needsPassword = event.error.code === 'bad-password' || event.error.code === 'password-required'
      setState({
        connect: {
          phase: needsPassword ? 'password' : 'failed',
          address: event.error.address.ip !== '' ? event.error.address : state.connect.address,
          serverName: state.connect.serverName,
          // password-required n'est pas une erreur : la modale s'ouvre vierge
          error: event.error.code === 'password-required' ? undefined : event.error,
        },
      })
      break
    }
    case 'connect-succeeded':
      // Keep the accepted state long enough to be painted in the simulator.
      // In-client, the mod normally replaces the menu before this timer ends.
      setState({ connect: { ...state.connect, phase: 'connecting', address: event.address, stage: 'joining' } })
      setTimeout(() => {
        if (state.connect.address?.ip === event.address.ip && state.connect.address.port === event.address.port && state.connect.stage === 'joining') {
          setState({ connect: { phase: 'idle', address: null } })
        }
      }, 900)
      break
    case 'favourites-changed':
      if (state.source === 'favourites') {
        setState({ servers: new Map(event.servers.map((s) => [s.id, s])) })
      }
      break
    case 'history-changed':
      if (state.source === 'recent') {
        setState({ servers: new Map(event.servers.map((s) => [s.id, s])) })
      }
      break
  }
})

subscribeConnectionUiEvents((event) => {
  if (event.type === 'connect-dismissed') setState({ connect: { phase: 'idle', address: null } })
})

const refreshedSources = new Set<ServerSource>()

async function refreshStats(): Promise<void> {
  const stats = await backend.getNetworkStats()
  setState({ stats })
}

export const actions = {
  async init(): Promise<void> {
    await refreshStats()
    await actions.setSource('internet')
  },

  async setSource(source: ServerSource): Promise<void> {
    const servers = await backend.getServers(source)
    const needsScan = source === 'internet' && !refreshedSources.has(source)
    setState({
      source,
      servers: new Map(servers.map((s) => [s.id, s])),
      selectedId: servers[0]?.id ?? null,
      progress: null,
      refreshing: needsScan,
      notice: null,
    })
    if (needsScan) {
      refreshedSources.add(source)
      await backend.refresh(source)
    }
  },

  async refresh(): Promise<void> {
    setState({ refreshing: true, notice: null })
    await backend.refresh(state.source)
  },

  setQuery(query: string): void {
    setState({ query })
  },

  setFilters(filters: BrowserFilters): void {
    setState({ filters })
  },

  select(id: string | null): void {
    setState({ selectedId: id })
  },

  /** Déplace la sélection dans la liste visible fournie par l'appelant. */
  moveSelection(visibleIds: readonly string[], delta: number): void {
    if (visibleIds.length === 0) return
    const index = state.selectedId ? visibleIds.indexOf(state.selectedId) : -1
    const next = Math.max(0, Math.min(visibleIds.length - 1, index + delta))
    setState({ selectedId: visibleIds[next] ?? null })
  },

  connectTo(server: ServerItem): void {
    const address: ServerAddress = { ip: server.ip, port: server.gamePort }
    // Let the backend publish password-required like every other connection
    // state so the always-mounted modal remains the single UI owner.
    void backend.connect(address)
  },

  connectToAddress(address: ServerAddress): void {
    void backend.connect(address)
  },

  submitPassword(password: string): void {
    if (state.connect.address) void backend.connect(state.connect.address, password)
  },

  retryConnect(): void {
    if (state.connect.address) void backend.connect(state.connect.address)
  },

  dismissConnect(): void {
    backend.cancelConnect()
    publishConnectionUiEvent({ type: 'connect-dismissed' })
  },

  toggleFavourite(server: ServerItem): void {
    const address: ServerAddress = { ip: server.ip, port: server.gamePort }
    const servers = new Map(state.servers)
    const id = serverKey(server.ip, server.gamePort)
    const current = servers.get(id)
    if (current) servers.set(id, { ...current, isFavourite: !server.isFavourite })
    setState({ servers })
    if (server.isFavourite) void backend.removeFavourite(address)
    else void backend.addFavourite(address)
  },

  /**
   * Ouvre un lien via le bridge natif quand il existe. Retourne true si le
   * lien a été pris en charge (l'ancre ne doit alors pas naviguer).
   */
  openExternal(url: string): boolean {
    if (backend.openExternal) {
      backend.openExternal(url)
      return true
    }
    return false
  },

  copyServerLink(server: ServerItem): Promise<boolean> {
    return backend.copyServerLink({ ip: server.ip, port: server.gamePort })
  },

  /** Ferme le navigateur (retour au menu côté natif). */
  closeBrowser(): void {
    if (backend.close) backend.close()
    else window.location.hash = ''
  },
}
