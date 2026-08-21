import type { ServerAddress, ServerItem, ServerLink, ServerSource } from '../../types'
import type { BackendEvent, BackendListener, BrowserBackend, ConnectErrorCode, ConnectStage, Unsubscribe } from '../BrowserBackend'

/**
 * Backend réel : parle au bridge C++ de CServerBrowserWeb (Client/core).
 *
 * JS → C++ : window.mta.triggerEvent('sb:<commande>', ...args-chaînes)
 * C++ → JS : le natif appelle window.__neonSB.emit([...événements JSON])
 *
 * Le protocole est documenté dans Tools/server-browser-prototype/BRIDGE.md.
 */

interface MtaBridge {
  triggerEvent(name: string, ...args: string[]): void
}

declare global {
  interface Window {
    mta?: MtaBridge
    __neonSB?: { emit(events: NativeEvent[]): void }
  }
}

/** Événements poussés par le C++ (voir CServerBrowserWeb::QueueEvent). */
type NativeEvent =
  | { type: 'init'; version: string; source: ServerSource }
  | { type: 'list-reset'; source: ServerSource }
  | { type: 'server'; source: ServerSource; server: NativeServer }
  | { type: 'progress'; source: ServerSource; scanned: number; total: number }
  | { type: 'refresh-finished'; source: ServerSource }
  | { type: 'registry-error'; message: string }
  | { type: 'favourites'; keys: string[] }
  | { type: 'connect-password-required'; host: string; port: number; name?: string }
  | { type: 'connect-started'; host: string; port: number; name?: string }
  | { type: 'connect-progress'; stage: ConnectStage; message?: string }
  | { type: 'connect-failed'; code: string; message: string }
  | { type: 'connect-succeeded' }
  | { type: 'connect-dismissed' }
  | { type: 'clipboard-result'; requestId: string; success: boolean }

interface NativeServer {
  id: string
  serverId?: string
  ip: string
  port: number
  httpPort: number
  name: string
  tagline?: string
  description?: string
  gameMode: string
  map: string
  version: string
  players: number
  maxPlayers: number
  /** -1 quand le serveur n'a pas (encore) répondu. */
  ping: number
  passworded: boolean
  serials: boolean
  verified: boolean
  state: 'queued' | 'online' | 'offline'
  favourite: boolean
  featured: boolean
  playerList: string[]
  countries?: string[]
  languages?: string[]
  links?: ServerLink[]
  accent?: string
  logoUrl?: string
  bannerUrl?: string
}

export function isCefEnvironment(): boolean {
  return typeof window !== 'undefined' && typeof window.mta?.triggerEvent === 'function'
}

export class CefBackend implements BrowserBackend {
  private listeners = new Set<BackendListener>()
  private servers = new Map<ServerSource, Map<string, ServerItem>>()
  private favouriteKeys = new Set<string>()
  private clientVersion = ''
  private currentSource: ServerSource = 'internet'
  private peakPlayers = 0
  private activeConnection: ServerAddress | null = null
  private nextClipboardRequest = 0
  private clipboardRequests = new Map<
    string,
    { resolve: (success: boolean) => void; timeout: ReturnType<typeof setTimeout> }
  >()

  constructor() {
    window.__neonSB = { emit: (events) => events.forEach((e) => this.onNativeEvent(e)) }
    this.send('sb:ready')
  }

  private send(name: string, ...args: string[]): void {
    window.mta?.triggerEvent(name, ...args)
  }

  private emit(event: BackendEvent): void {
    for (const listener of this.listeners) listener(event)
  }

  private cacheFor(source: ServerSource): Map<string, ServerItem> {
    let cache = this.servers.get(source)
    if (!cache) {
      cache = new Map()
      this.servers.set(source, cache)
    }
    return cache
  }

  private toServerItem(native: NativeServer): ServerItem {
    return {
      id: native.id,
      ip: native.ip,
      gamePort: native.port,
      httpPort: native.httpPort || undefined,
      name: native.name,
      tagline: native.tagline || [native.gameMode, native.map].filter(Boolean).join(' — '),
      description: native.description || [native.gameMode, native.map].filter(Boolean).join(' on ') || native.name,
      gameMode: native.gameMode,
      map: native.map,
      version: native.version,
      isCompatible: this.clientVersion === '' || native.version === this.clientVersion,
      players: native.players,
      maxPlayers: native.maxPlayers,
      ping: native.ping >= 0 ? native.ping : undefined,
      passworded: native.passworded,
      requiresSerial: native.serials,
      isStatusVerified: native.verified,
      scanState: native.state,
      playerList: native.playerList.map((name) => ({ name })),
      isFavourite: native.favourite || this.favouriteKeys.has(native.id),
      isFeatured: native.featured,
      countries: native.countries ?? [],
      languages: native.languages ?? [],
      tags: [],
      links: native.links ?? [],
      accent: native.accent,
      logoUrl: native.logoUrl || undefined,
      bannerUrl: native.bannerUrl || undefined,
    }
  }

  private onNativeEvent(event: NativeEvent): void {
    switch (event.type) {
      case 'init':
        this.clientVersion = event.version
        this.currentSource = event.source
        break
      case 'list-reset':
        this.cacheFor(event.source).clear()
        this.emit({ type: 'servers-reset', source: event.source })
        break
      case 'server': {
        const item = this.toServerItem(event.server)
        this.cacheFor(event.source).set(item.id, item)
        this.emit({ type: 'server-updated', source: event.source, server: item })
        break
      }
      case 'progress':
        this.emit({ type: 'refresh-progress', source: event.source, scanned: event.scanned, total: event.total })
        break
      case 'refresh-finished':
        this.emit({ type: 'refresh-finished', source: event.source })
        break
      case 'registry-error':
        this.emit({ type: 'refresh-failed', source: 'internet', message: event.message })
        break
      case 'favourites': {
        this.favouriteKeys = new Set(event.keys)
        for (const cache of this.servers.values()) {
          for (const [id, item] of cache) {
            const isFavourite = this.favouriteKeys.has(id)
            if (item.isFavourite !== isFavourite) {
              const updated = { ...item, isFavourite }
              cache.set(id, updated)
              this.emit({ type: 'server-updated', source: this.currentSource, server: updated })
            }
          }
        }
        break
      }
      case 'connect-password-required':
        this.activeConnection = { ip: event.host, port: event.port }
        this.emit({
          type: 'connect-failed',
          error: {
            address: { ip: event.host, port: event.port },
            code: 'password-required',
            message: '',
          },
        })
        break
      case 'connect-started':
        this.activeConnection = { ip: event.host, port: event.port }
        this.emit({ type: 'connect-started', address: this.activeConnection, serverName: event.name })
        break
      case 'connect-progress':
        this.emit({ type: 'connect-progress', stage: event.stage, message: event.message })
        break
      case 'connect-failed':
        this.emit({
          type: 'connect-failed',
          error: {
            address: this.activeConnection ?? { ip: '', port: 0 },
            code: normalizeConnectErrorCode(event.code),
            message: event.message,
          },
        })
        break
      case 'connect-succeeded':
        if (this.activeConnection) this.emit({ type: 'connect-succeeded', address: this.activeConnection })
        this.activeConnection = null
        break
      case 'connect-dismissed':
        // A native owner (currently the full-server queue) takes over here.
        // Clear the shared web state so reopening CEF cannot resurrect a
        // stale connecting modal or trap keyboard input behind it.
        this.activeConnection = null
        this.emit({ type: 'connect-dismissed' })
        break
      case 'clipboard-result': {
        const request = this.clipboardRequests.get(event.requestId)
        if (!request) break
        clearTimeout(request.timeout)
        this.clipboardRequests.delete(event.requestId)
        request.resolve(event.success)
        break
      }
    }
  }

  subscribe(listener: BackendListener): Unsubscribe {
    this.listeners.add(listener)
    return () => this.listeners.delete(listener)
  }

  resume(): void {
    this.send('sb:resume')
  }

  suspend(): void {
    this.send('sb:suspend')
  }

  getServers(source: ServerSource): Promise<ServerItem[]> {
    this.currentSource = source
    this.send('sb:setSource', source)
    return Promise.resolve([...this.cacheFor(source).values()])
  }

  refresh(_source: ServerSource): Promise<void> {
    this.send('sb:refresh')
    return Promise.resolve()
  }

  connect(address: ServerAddress, password?: string): Promise<void> {
    this.activeConnection = address
    this.send('sb:connect', address.ip, String(address.port), password ?? '')
    return Promise.resolve()
  }

  cancelConnect(): void {
    this.send('sb:cancelConnect')
    this.activeConnection = null
  }

  addFavourite(address: ServerAddress): Promise<void> {
    this.send('sb:favourite', address.ip, String(address.port), '1')
    return Promise.resolve()
  }

  removeFavourite(address: ServerAddress): Promise<void> {
    this.send('sb:favourite', address.ip, String(address.port), '0')
    return Promise.resolve()
  }

  getHistory(): Promise<ServerItem[]> {
    return Promise.resolve([...this.cacheFor('recent').values()])
  }

  getNetworkStats(): Promise<{ playersOnline: number; peakPlayers: number; serverCount: number }> {
    const internet = this.cacheFor('internet')
    let playersOnline = 0
    for (const server of internet.values()) {
      if (server.scanState !== 'offline') playersOnline += server.players
    }
    this.peakPlayers = Math.max(this.peakPlayers, playersOnline)
    return Promise.resolve({ playersOnline, peakPlayers: this.peakPlayers, serverCount: internet.size })
  }

  copyServerLink(address: ServerAddress): Promise<boolean> {
    const requestId = `copy-${++this.nextClipboardRequest}`
    return new Promise((resolve) => {
      const timeout = setTimeout(() => {
        this.clipboardRequests.delete(requestId)
        resolve(false)
      }, 3000)
      this.clipboardRequests.set(requestId, { resolve, timeout })
      this.send('sb:copyServerLink', requestId, address.ip, String(address.port))
    })
  }

  openExternal(url: string): void {
    this.send('sb:openExternal', url)
  }

  close(): void {
    for (const request of this.clipboardRequests.values()) {
      clearTimeout(request.timeout)
      request.resolve(false)
    }
    this.clipboardRequests.clear()
    this.send('sb:close')
  }
}

const CONNECT_ERROR_CODES = new Set<ConnectErrorCode>([
  'timeout',
  'refused',
  'bad-password',
  'password-required',
  'server-full',
  'version-mismatch',
  'banned',
  'disconnected',
  'identity-required',
  'identity-failed',
  'bad-response',
  'mod-unavailable',
  'invalid-nick',
  'connection-start-failed',
  'unknown',
])

function normalizeConnectErrorCode(code: string): ConnectErrorCode {
  return CONNECT_ERROR_CODES.has(code as ConnectErrorCode) ? (code as ConnectErrorCode) : 'unknown'
}
