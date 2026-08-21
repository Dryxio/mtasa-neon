import { lazy, Suspense, useEffect, useState } from 'react'
import { I18nProvider, useI18n } from './i18n'
import { MainMenu } from './MainMenu'
import { ConnectModals, NativeMessageModal } from './components/Modals'
import { useMenuBridge } from './menuBridge'

// Keep the server store and its native bridge out of the startup path. This
// also prevents the master-server scan from beginning behind the Main Menu.
const ServerBrowser = lazy(() => import('./App').then(({ App }) => ({ default: App })))
const loadSettings = () => import('./Settings').then(({ Settings }) => ({ default: Settings }))
const Settings = lazy(loadSettings)
const Updates = lazy(() => import('./Updates').then(({ Updates }) => ({ default: Updates })))

type View = 'main-menu' | 'server-browser' | 'settings' | 'updates'

function viewFromLocation(): View {
  if (window.location.hash === '#/servers') return 'server-browser'
  if (window.location.hash === '#/settings') return 'settings'
  if (window.location.hash === '#/updates') return 'updates'
  return 'main-menu'
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

  useEffect(() => {
    // Keep Settings out of the first paint, then warm its small JS/CSS chunk
    // immediately afterwards. Waiting for an idle callback was observable on
    // busy game frames and made the first visit feel slower than later visits.
    let secondFrame: number | undefined
    const firstFrame = window.requestAnimationFrame(() => {
      secondFrame = window.requestAnimationFrame(() => { void loadSettings() })
    })
    return () => {
      window.cancelAnimationFrame(firstFrame)
      if (secondFrame !== undefined) window.cancelAnimationFrame(secondFrame)
    }
  }, [])

  return (
    <I18nProvider locale={menu.state.locale} translations={menu.state.translations}>
      {view === 'server-browser' ? <ServerBrowserRoute /> : view === 'settings' ? (
        <Suspense fallback={<div className="route-loading" aria-label="Loading settings" />}>
          <Settings onClose={() => { window.location.hash = '' }} />
        </Suspense>
      ) : view === 'updates' ? (
        <Suspense fallback={<div className="route-loading" aria-label="Loading updates" />}>
          <Updates buildNumber={menu.state.buildNumber} onClose={() => { window.location.hash = '' }} />
        </Suspense>
      ) : (
          <MainMenu
            buildNumber={menu.state.buildNumber}
            inGame={menu.state.inGame}
            identity={menu.state.identity}
            featuredServer={menu.state.featuredServer}
            locale={menu.state.locale}
            languages={menu.state.languages}
            onBrowseServers={() => {
              window.location.hash = '/servers'
            }}
            onResume={() => menu.command('menu:resume')}
            onDisconnect={() => menu.command('menu:disconnect')}
            onQuickConnect={() => menu.command('menu:quickConnect', () => { window.location.hash = '/servers' })}
            onMapEditor={() => menu.command('menu:mapEditor')}
            onSettings={() => menu.command('menu:settings', () => { window.location.hash = '/settings' })}
            onAbout={() => menu.command('menu:about')}
            onUpdates={() => { window.location.hash = '/updates' }}
            onIdentity={() => menu.command('menu:identity')}
            onPlayFeatured={() => menu.command('menu:playFeatured')}
            onLanguage={menu.setLanguage}
            onQuit={() => menu.command('menu:quit', () => window.close())}
          />
        )}
      <ConnectModals
        connect={menu.state.connect}
        onSubmitPassword={menu.submitConnectionPassword}
        onRetry={menu.retryConnection}
        onDismiss={menu.dismissConnection}
        onLinkIdentity={() => {
          // Identity is linked from the main-menu profile. Take the player
          // there before opening Discord so the return path is unambiguous.
          menu.dismissConnection()
          window.location.hash = ''
          menu.command('menu:identity')
        }}
      />
      <NativeMessageModal dialog={menu.state.dialog} onRespond={menu.respondToDialog} />
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
