import { lazy, Suspense, useEffect, useState } from 'react'
import { I18nProvider, useI18n } from './i18n'
import { MainMenu } from './MainMenu'
import { useMenuBridge } from './menuBridge'

// Keep the server store and its native bridge out of the startup path. This
// also prevents the master-server scan from beginning behind the Main Menu.
const ServerBrowser = lazy(() => import('./App').then(({ App }) => ({ default: App })))

type View = 'main-menu' | 'server-browser'

function viewFromLocation(): View {
  return window.location.hash === '#/servers' ? 'server-browser' : 'main-menu'
}

/**
 * Keeps the prototype's screens behind a deliberately tiny router. The native
 * client can replace this boundary with its CEF navigation without coupling
 * the main menu to the server browser store.
 */
export function Experience() {
  const [view, setView] = useState<View>(viewFromLocation)
  const menu = useMenuBridge()

  useEffect(() => {
    const syncView = () => setView(viewFromLocation())
    window.addEventListener('hashchange', syncView)
    return () => window.removeEventListener('hashchange', syncView)
  }, [])

  return (
    <I18nProvider locale={menu.state.locale} translations={menu.state.translations}>
      {view === 'server-browser' ? (
        <ServerBrowserRoute />
      ) : (
        <MainMenu
          inGame={menu.state.inGame}
          identity={menu.state.identity}
          locale={menu.state.locale}
          languages={menu.state.languages}
          onBrowseServers={() => {
            window.location.hash = '/servers'
          }}
          onResume={() => menu.command('menu:resume')}
          onDisconnect={() => menu.command('menu:disconnect')}
          onQuickConnect={() => menu.command('menu:quickConnect', () => { window.location.hash = '/servers' })}
          onMapEditor={() => menu.command('menu:mapEditor')}
          onSettings={() => menu.command('menu:settings')}
          onAbout={() => menu.command('menu:about')}
          onIdentity={() => menu.command('menu:identity')}
          onLanguage={menu.setLanguage}
          onQuit={() => menu.command('menu:quit', () => window.close())}
        />
      )}
    </I18nProvider>
  )
}

function ServerBrowserRoute() {
  const { t } = useI18n()
  return (
    <Suspense fallback={<div className="route-loading" aria-label={t('route.loadingServerBrowser')} />}>
      <ServerBrowser />
    </Suspense>
  )
}
