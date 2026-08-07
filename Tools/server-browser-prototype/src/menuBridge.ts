import { useCallback, useEffect, useState } from 'react'
import type { ConnectErrorCode, ConnectStage } from './backend/BrowserBackend'
import { normalizeTranslations, type TranslationMap } from './i18n'
import type { ConnectFlow } from './store'

export interface MenuLanguage {
  locale: string
  name: string
}

export interface MenuIdentity {
  authenticated: boolean
  signingIn: boolean
  displayName: string
  status: string
}

interface MenuState {
  inGame: boolean
  locale: string
  languages: MenuLanguage[]
  identity: MenuIdentity
  translations: TranslationMap
  connect: ConnectFlow
}

type NativeMenuEvent =
  | ({ type: 'init'; translations?: Record<string, string> } & Omit<MenuState, 'translations' | 'connect'>)
  | { type: 'context'; inGame: boolean }
  | { type: 'identity'; identity: MenuIdentity }
  | { type: 'connect-password-required'; host: string; port: number; name?: string }
  | { type: 'connect-started'; host: string; port: number; name?: string }
  | { type: 'connect-progress'; stage: ConnectStage; message?: string }
  | { type: 'connect-failed'; code: string; message: string }
  | { type: 'connect-succeeded' }

declare global {
  interface Window {
    __neonMenu?: { emit(events: NativeMenuEvent[]): void }
  }
}

const DEVELOPMENT_LANGUAGES: MenuLanguage[] = [
  { locale: 'en_US', name: 'English' },
  { locale: 'fr_FR', name: 'Français' },
  { locale: 'de_DE', name: 'Deutsch' },
  { locale: 'es_ES', name: 'Español' },
  { locale: 'pt_BR', name: 'Português (Brasil)' },
  { locale: 'pl_PL', name: 'Polski' },
  { locale: 'ru_RU', name: 'Русский' },
  { locale: 'tr_TR', name: 'Türkçe' },
  { locale: 'ar_SA', name: 'العربية' },
]

const INITIAL_STATE: MenuState = {
  inGame: false,
  locale: 'en_US',
  languages: DEVELOPMENT_LANGUAGES,
  identity: {
    authenticated: true,
    signingIn: false,
    displayName: 'Dryxio',
    status: 'Neon ID connected',
  },
  translations: {},
  connect: { phase: 'idle', address: null },
}

function normalizeConnectErrorCode(code: string): ConnectErrorCode {
  switch (code) {
    case 'timeout':
    case 'refused':
    case 'bad-password':
    case 'password-required':
    case 'server-full':
    case 'version-mismatch':
    case 'banned':
    case 'disconnected':
    case 'identity-required':
    case 'identity-failed':
    case 'bad-response':
    case 'mod-unavailable':
    case 'invalid-nick':
    case 'connection-start-failed':
      return code
    default:
      return 'unknown'
  }
}

function send(name: string, ...args: string[]): boolean {
  if (typeof window.mta?.triggerEvent !== 'function') return false
  window.mta.triggerEvent(name, ...args)
  return true
}

export function notifyMenuVisualReady(): void {
  send('menu:visualReady')
}

function enforceInGameMenuRoute(inGame: boolean): void {
  if (!inGame || window.location.hash !== '#/servers') return

  // The CEF document stays alive while playing, so its last route can still
  // be the server browser used to join. Reset that hidden route as soon as
  // native gameplay starts; the next Escape must reveal the pause menu, not
  // an obsolete browser screen that requires an extra Back action.
  window.history.replaceState(null, '', `${window.location.pathname}${window.location.search}`)
  window.dispatchEvent(new Event('hashchange'))
}

/**
 * Point de contact unique entre le Main Menu React et CMainMenu. Les valeurs
 * de développement gardent le prototype utilisable hors du client.
 */
export function useMenuBridge() {
  const [state, setState] = useState<MenuState>(INITIAL_STATE)

  useEffect(() => {
    window.__neonMenu = {
      emit(events) {
        for (const event of events) {
          if (event.type === 'init') {
            enforceInGameMenuRoute(event.inGame)
            setState((current) => ({ ...current, ...event, translations: normalizeTranslations(event.translations) }))
          }
          else if (event.type === 'context') {
            enforceInGameMenuRoute(event.inGame)
            setState((current) => ({ ...current, inGame: event.inGame }))
          }
          else if (event.type === 'identity') {
            setState((current) => ({ ...current, identity: event.identity }))
          }
          else if (event.type === 'connect-password-required') {
            setState((current) => ({
              ...current,
              connect: {
                phase: 'password',
                address: { ip: event.host, port: event.port },
                serverName: event.name,
              },
            }))
          }
          else if (event.type === 'connect-started') {
            setState((current) => ({
              ...current,
              connect: {
                phase: 'connecting',
                address: { ip: event.host, port: event.port },
                serverName: event.name,
                stage: 'contacting',
              },
            }))
          }
          else if (event.type === 'connect-progress') {
            setState((current) => ({
              ...current,
              connect:
                current.connect.phase === 'connecting'
                  ? { ...current.connect, stage: event.stage, statusMessage: event.message }
                  : current.connect,
            }))
          }
          else if (event.type === 'connect-failed') {
            setState((current) => {
              const code = normalizeConnectErrorCode(event.code)
              const needsPassword = code === 'bad-password' || code === 'password-required'
              return {
                ...current,
                connect: {
                  phase: needsPassword ? 'password' : 'failed',
                  address: current.connect.address,
                  serverName: current.connect.serverName,
                  error:
                    code === 'password-required' || !current.connect.address
                      ? undefined
                      : { address: current.connect.address, code, message: event.message },
                },
              }
            })
          }
          else if (event.type === 'connect-succeeded') {
            setState((current) => ({
              ...current,
              connect: { ...current.connect, phase: 'connecting', stage: 'joining' },
            }))
            window.setTimeout(() => {
              setState((current) =>
                current.connect.stage === 'joining'
                  ? { ...current, connect: { phase: 'idle', address: null } }
                  : current,
              )
            }, 900)
          }
        }
      },
    }
    send('menu:ready')
    return () => {
      delete window.__neonMenu
    }
  }, [])

  const command = useCallback((name: string, fallback?: () => void) => {
    if (!send(name)) fallback?.()
  }, [])

  const setLanguage = useCallback((locale: string) => {
    setState((current) => ({ ...current, locale }))
    send('menu:setLanguage', locale)
  }, [])

  const submitConnectionPassword = useCallback((password: string) => {
    if (!state.connect.address) return
    send('sb:connect', state.connect.address.ip, String(state.connect.address.port), password)
  }, [state.connect.address])

  const retryConnection = useCallback(() => {
    if (!state.connect.address) return
    send('sb:connect', state.connect.address.ip, String(state.connect.address.port), '')
  }, [state.connect.address])

  const dismissConnection = useCallback(() => {
    send('sb:cancelConnect')
    setState((current) => ({ ...current, connect: { phase: 'idle', address: null } }))
  }, [])

  return { state, command, setLanguage, submitConnectionPassword, retryConnection, dismissConnection }
}
