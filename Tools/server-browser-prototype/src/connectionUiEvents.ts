import type { BackendEvent } from './backend/BrowserBackend'

type BackendConnectionEvent = Extract<BackendEvent, { type: `connect-${string}` }>

export type ConnectionUiEvent = BackendConnectionEvent | { type: 'connect-dismissed' }
export type ConnectionUiListener = (event: ConnectionUiEvent) => void

const listeners = new Set<ConnectionUiListener>()

/**
 * Keeps the always-mounted connection modal synchronized with the lazily
 * loaded server browser without importing or starting its backend at launch.
 */
export function publishConnectionUiEvent(event: ConnectionUiEvent): void {
  for (const listener of listeners) listener(event)
}

export function subscribeConnectionUiEvents(listener: ConnectionUiListener): () => void {
  listeners.add(listener)
  return () => listeners.delete(listener)
}
