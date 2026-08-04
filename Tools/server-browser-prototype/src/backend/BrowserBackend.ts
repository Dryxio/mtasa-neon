import type { ServerAddress, ServerItem, ServerSource } from '../types'

/**
 * Contrat entre l'UI et la source de données serveur.
 *
 * Implémenté aujourd'hui par MockBackend ; lors de l'intégration CEF, une
 * implémentation CefBackend le réalisera par-dessus le bridge C++
 * (postMessage / cefQuery) sans toucher aux composants.
 *
 * Le flux est événementiel : le C++ pousse les serveurs au fil du scan ASE
 * (un serveur peut répondre plusieurs fois : master list puis query directe),
 * donc l'UI s'abonne plutôt que d'attendre une réponse unique.
 */

export interface ConnectError {
  address: ServerAddress
  /**
   * `password-required` : le serveur est protégé et aucun mot de passe n'a
   * été fourni — l'UI ouvre sa modale sans afficher d'erreur.
   */
  code: 'timeout' | 'refused' | 'bad-password' | 'password-required' | 'server-full' | 'version-mismatch' | 'unknown'
  message: string
}

export type BackendEvent =
  | { type: 'servers-reset'; source: ServerSource }
  | { type: 'server-updated'; source: ServerSource; server: ServerItem }
  | { type: 'refresh-progress'; source: ServerSource; scanned: number; total: number }
  | { type: 'refresh-finished'; source: ServerSource }
  | { type: 'refresh-failed'; source: ServerSource; message: string }
  | { type: 'connect-started'; address: ServerAddress; serverName?: string }
  | { type: 'connect-failed'; error: ConnectError }
  | { type: 'connect-succeeded'; address: ServerAddress }
  | { type: 'favourites-changed'; servers: ServerItem[] }
  | { type: 'history-changed'; servers: ServerItem[] }

export type BackendListener = (event: BackendEvent) => void
export type Unsubscribe = () => void

export interface BrowserBackend {
  subscribe(listener: BackendListener): Unsubscribe

  /** Snapshot initial d'une source (cache disque côté natif). */
  getServers(source: ServerSource): Promise<ServerItem[]>

  /** (Re)scanne une source ; la progression arrive via les événements. */
  refresh(source: ServerSource): Promise<void>

  /**
   * Tente la connexion. Si le serveur est protégé, `password` doit être
   * fourni — sinon le backend émet connect-failed { code: 'bad-password' }.
   */
  connect(address: ServerAddress, password?: string): Promise<void>
  cancelConnect(): void

  addFavourite(address: ServerAddress): Promise<void>
  removeFavourite(address: ServerAddress): Promise<void>

  getHistory(): Promise<ServerItem[]>

  /** Statistiques réseau globales (master server). */
  getNetworkStats(): Promise<{ playersOnline: number; peakPlayers: number; serverCount: number }>

  /** Ouvre un lien dans le navigateur système (CEF uniquement). */
  openExternal?(url: string): void

  /** Ferme le navigateur de serveurs (retour au menu — CEF uniquement). */
  close?(): void
}
