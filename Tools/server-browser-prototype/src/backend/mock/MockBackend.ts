import type { ServerAddress, ServerItem, ServerSource } from '../../types'
import { serverKey } from '../../types'
import type { BackendEvent, BackendListener, BrowserBackend, Unsubscribe } from '../BrowserBackend'
import { buildInternetList, buildLanList } from './data'

const FAVOURITES_KEY = 'neon.browser.favourites'
const HISTORY_KEY = 'neon.browser.history'

/** Mot de passe accepté par les serveurs verrouillés du mock. */
export const DEMO_PASSWORD = 'neon'

/**
 * Backend simulé : reproduit le comportement du scan ASE natif (réponses par
 * vagues, latence, échecs) et persiste favoris/historique en localStorage —
 * à migrer vers coreconfig.xml via le bridge CEF.
 */
export class MockBackend implements BrowserBackend {
  private listeners = new Set<BackendListener>()
  private internet: Map<string, ServerItem>
  private lan: Map<string, ServerItem>
  private favourites: Set<string>
  private history: Map<string, number>
  private refreshTimer: ReturnType<typeof setInterval> | null = null
  private connectTimer: ReturnType<typeof setTimeout> | null = null
  private connectProgressTimer: ReturnType<typeof setTimeout> | null = null
  private connectAttempts = new Map<string, number>()

  constructor() {
    this.internet = new Map(buildInternetList().map((s) => [s.id, s]))
    this.lan = new Map(buildLanList().map((s) => [s.id, s]))
    this.favourites = new Set(readJson<string[]>(FAVOURITES_KEY) ?? [])
    this.history = new Map(readJson<[string, number][]>(HISTORY_KEY) ?? [])
    for (const id of this.favourites) {
      const server = this.internet.get(id)
      if (server) server.isFavourite = true
    }
  }

  subscribe(listener: BackendListener): Unsubscribe {
    this.listeners.add(listener)
    return () => this.listeners.delete(listener)
  }

  private emit(event: BackendEvent): void {
    for (const listener of this.listeners) listener(event)
  }

  private listFor(source: ServerSource): ServerItem[] {
    switch (source) {
      case 'internet':
        return [...this.internet.values()]
      case 'lan':
        return [...this.lan.values()]
      case 'favourites':
        return [...this.favourites].flatMap((id) => this.internet.get(id) ?? [])
      case 'recent':
        return [...this.history.entries()]
          .sort((a, b) => b[1] - a[1])
          .flatMap(([id, at]) => {
            const server = this.internet.get(id)
            return server ? [{ ...server, lastPlayedAt: at }] : []
          })
    }
  }

  getServers(source: ServerSource): Promise<ServerItem[]> {
    return Promise.resolve(this.listFor(source).map((s) => ({ ...s })))
  }

  refresh(source: ServerSource): Promise<void> {
    if (this.refreshTimer) clearInterval(this.refreshTimer)
    const servers = this.listFor(source)
    for (const server of servers) server.scanState = 'queued'

    // Vagues de réponses ASE : ~25 serveurs toutes les 40 ms, dans un ordre
    // pseudo-aléatoire stable, ~4 % de timeouts.
    const order = [...servers].sort((a, b) => (hash(a.id) % 997) - (hash(b.id) % 997))
    let cursor = 0
    this.refreshTimer = setInterval(() => {
      const wave = order.slice(cursor, cursor + 25)
      cursor += 25
      for (const server of wave) {
        const offline = hash(server.id) % 100 < 4
        server.scanState = offline ? 'offline' : 'online'
        if (offline) server.ping = undefined
        this.emit({ type: 'server-updated', source, server: { ...server } })
      }
      this.emit({
        type: 'refresh-progress',
        source,
        scanned: Math.min(cursor, order.length),
        total: order.length,
      })
      if (cursor >= order.length) {
        if (this.refreshTimer) clearInterval(this.refreshTimer)
        this.refreshTimer = null
        this.emit({ type: 'refresh-finished', source })
      }
    }, 40)
    return Promise.resolve()
  }

  connect(address: ServerAddress, password?: string): Promise<void> {
    const id = serverKey(address.ip, address.port)
    const server = this.internet.get(id) ?? this.lan.get(id)
    if (server?.passworded && !password) {
      this.emit({
        type: 'connect-failed',
        error: { address, code: 'password-required', message: '' },
      })
      return Promise.resolve()
    }
    this.emit({ type: 'connect-started', address, serverName: server?.name })
    this.connectProgressTimer = setTimeout(() => {
      this.emit({ type: 'connect-progress', stage: 'authorizing' })
      this.connectProgressTimer = null
    }, 550)

    this.connectTimer = setTimeout(() => {
      this.connectTimer = null
      if (!server) {
        this.emit({
          type: 'connect-failed',
          error: { address, code: 'timeout', message: 'No response from the server.' },
        })
        return
      }
      if (server.passworded && password !== DEMO_PASSWORD) {
        this.emit({
          type: 'connect-failed',
          error: { address, code: 'bad-password', message: 'Incorrect server password.' },
        })
        return
      }
      const attempts = (this.connectAttempts.get(id) ?? 0) + 1
      this.connectAttempts.set(id, attempts)
      // Serveur plein : laisse entrer à la 3e tentative (file d'attente simulée).
      if (server.players >= server.maxPlayers && attempts < 3) {
        this.emit({
          type: 'connect-failed',
          error: { address, code: 'server-full', message: 'The server is full. Retrying may work.' },
        })
        return
      }
      this.history.set(id, Date.now())
      writeJson(HISTORY_KEY, [...this.history.entries()])
      this.emit({ type: 'history-changed', servers: this.listFor('recent') })
      this.emit({ type: 'connect-succeeded', address })
    }, 1100 + Math.random() * 600)
    return Promise.resolve()
  }

  cancelConnect(): void {
    if (this.connectTimer) clearTimeout(this.connectTimer)
    if (this.connectProgressTimer) clearTimeout(this.connectProgressTimer)
    this.connectTimer = null
    this.connectProgressTimer = null
  }

  addFavourite(address: ServerAddress): Promise<void> {
    return this.setFavourite(address, true)
  }

  removeFavourite(address: ServerAddress): Promise<void> {
    return this.setFavourite(address, false)
  }

  private setFavourite(address: ServerAddress, value: boolean): Promise<void> {
    const id = serverKey(address.ip, address.port)
    if (value) this.favourites.add(id)
    else this.favourites.delete(id)
    writeJson(FAVOURITES_KEY, [...this.favourites])
    const server = this.internet.get(id) ?? this.lan.get(id)
    if (server) {
      server.isFavourite = value
      this.emit({ type: 'server-updated', source: 'internet', server: { ...server } })
    }
    this.emit({ type: 'favourites-changed', servers: this.listFor('favourites') })
    return Promise.resolve()
  }

  getHistory(): Promise<ServerItem[]> {
    return Promise.resolve(this.listFor('recent'))
  }

  getNetworkStats(): Promise<{ playersOnline: number; peakPlayers: number; serverCount: number }> {
    let players = 0
    for (const server of this.internet.values()) players += server.players
    return Promise.resolve({
      playersOnline: players,
      peakPlayers: Math.round(players * 2.4),
      serverCount: this.internet.size,
    })
  }
}

function hash(text: string): number {
  let h = 2166136261
  for (let i = 0; i < text.length; i++) {
    h ^= text.charCodeAt(i)
    h = Math.imul(h, 16777619)
  }
  return h >>> 0
}

function readJson<T>(key: string): T | null {
  try {
    const raw = localStorage.getItem(key)
    return raw ? (JSON.parse(raw) as T) : null
  } catch {
    return null
  }
}

function writeJson(key: string, value: unknown): void {
  try {
    localStorage.setItem(key, JSON.stringify(value))
  } catch {
    // stockage indisponible : favoris non persistés, sans gravité pour le proto
  }
}
