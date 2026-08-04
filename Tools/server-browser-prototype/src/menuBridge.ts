import { useCallback, useEffect, useState } from 'react'

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
}

type NativeMenuEvent =
  | ({ type: 'init' } & MenuState)
  | { type: 'context'; inGame: boolean }
  | { type: 'identity'; identity: MenuIdentity }

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
}

function send(name: string, ...args: string[]): boolean {
  if (typeof window.mta?.triggerEvent !== 'function') return false
  window.mta.triggerEvent(name, ...args)
  return true
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
            setState(event)
          }
          else if (event.type === 'context') {
            enforceInGameMenuRoute(event.inGame)
            setState((current) => ({ ...current, inGame: event.inGame }))
          }
          else if (event.type === 'identity') {
            setState((current) => ({ ...current, identity: event.identity }))
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

  return { state, command, setLanguage }
}
