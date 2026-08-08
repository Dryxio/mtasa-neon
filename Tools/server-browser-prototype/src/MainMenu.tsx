import { useEffect, useMemo, useRef, useState } from 'react'
import { LanguageSelector } from './components/LanguageSelector'
import { useI18n } from './i18n'
import { chooseLaunchLoadscreen } from './loadscreen'
import { notifyMenuVisualReady, type MenuFeaturedServer, type MenuIdentity, type MenuLanguage } from './menuBridge'
import { playUiSound } from './uiSound'
import './MainMenu.css'

interface MainMenuProps {
  inGame: boolean
  identity: MenuIdentity
  featuredServer: MenuFeaturedServer | null
  locale: string
  languages: readonly MenuLanguage[]
  onBrowseServers: () => void
  onResume: () => void
  onDisconnect: () => void
  onQuickConnect: () => void
  onMapEditor: () => void
  onSettings: () => void
  onAbout: () => void
  onIdentity: () => void
  onPlayFeatured: () => void
  onLanguage: (locale: string) => void
  onQuit: () => void
}

interface MenuItem {
  id: string
  label: string
  caption: string
  action?: () => void
}

function DiscordIcon() {
  return (
    <svg viewBox="0 0 24 24" aria-hidden="true">
      <path d="M19.5 5.4a16 16 0 0 0-4-1.3l-.5 1a14.8 14.8 0 0 0-6 0l-.5-1a16 16 0 0 0-4 1.3C2 9.1 1.3 12.7 1.6 16.2a16.4 16.4 0 0 0 4.9 2.5l1.2-1.7a10.7 10.7 0 0 1-1.9-.9l.5-.4a11.5 11.5 0 0 0 11.4 0l.5.4a11 11 0 0 1-1.9.9l1.2 1.7a16.4 16.4 0 0 0 4.9-2.5c.4-4.1-.8-7.6-2.9-10.8ZM8.5 14.5c-1.1 0-2-1-2-2.2s.9-2.2 2-2.2 2 1 2 2.2-.9 2.2-2 2.2Zm7 0c-1.1 0-2-1-2-2.2s.9-2.2 2-2.2 2 1 2 2.2-.9 2.2-2 2.2Z" />
    </svg>
  )
}

export function MainMenu(props: MainMenuProps) {
  const { t } = useI18n()
  const [selectedIndex, setSelectedIndex] = useState(0)
  const [loadscreen] = useState(chooseLaunchLoadscreen)
  const [artReady, setArtReady] = useState(false)
  const previousSelectedIndex = useRef(selectedIndex)
  const visualReadySent = useRef(false)
  const discordTitle = props.identity.signingIn
    ? t('main.discordConnecting')
    : props.identity.authenticated
      ? (props.identity.displayName || t('main.discordConnected'))
      : t('main.discordLink')
  const discordAction = props.identity.signingIn
    ? t('common.cancel')
    : props.identity.authenticated
      ? t('main.discordSignOut')
      : t('common.connect')
  const featuredAction = props.identity.signingIn
    ? t('main.discordConnecting')
    : props.identity.authenticated
      ? t('main.playFeatured')
      : t('main.linkDiscordToPlay')

  const items = useMemo<MenuItem[]>(
    () => props.inGame
      ? [
          {
            id: 'resume',
            label: t('main.resumeGame'),
            caption: t('main.resumeCaption'),
            action: props.onResume,
          },
          {
            id: 'settings',
            label: t('common.settings'),
            caption: t('main.settingsCaption'),
            action: props.onSettings,
          },
          {
            id: 'disconnect',
            label: t('common.disconnect'),
            caption: t('main.disconnectCaption'),
            action: props.onDisconnect,
          },
          {
            id: 'quit',
            label: t('main.quitGame'),
            caption: t('main.quitInGameCaption'),
            action: props.onQuit,
          },
        ]
      : [
          {
            id: 'play',
            label: t('main.browseServers'),
            caption: t('main.browseCaption'),
            action: props.onBrowseServers,
          },
          {
            id: 'quick-connect',
            label: t('main.quickConnect'),
            caption: t('main.quickConnectCaption'),
            action: props.onQuickConnect,
          },
          {
            id: 'map-editor',
            label: t('main.mapEditor'),
            caption: t('main.mapEditorCaption'),
            action: props.onMapEditor,
          },
          {
            id: 'settings',
            label: t('common.settings'),
            caption: t('main.settingsCaption'),
            action: props.onSettings,
          },
          {
            id: 'about',
            label: t('common.about'),
            caption: t('main.aboutCaption'),
            action: props.onAbout,
          },
          {
            id: 'quit',
            label: t('main.quitGame'),
            caption: t('main.quitCaption'),
            action: props.onQuit,
          },
        ],
    [props, t],
  )

  useEffect(() => {
    setSelectedIndex(0)
  }, [props.inGame])

  useEffect(() => {
    if (previousSelectedIndex.current !== selectedIndex) playUiSound('highlight')
    previousSelectedIndex.current = selectedIndex
  }, [selectedIndex])

  useEffect(() => {
    if (visualReadySent.current || (!props.inGame && !artReady)) return

    let cancelled = false
    let firstFrame = 0
    let secondFrame = 0
    let animationTimer = 0
    const notifyAfterPaint = () => {
      firstFrame = window.requestAnimationFrame(() => {
        secondFrame = window.requestAnimationFrame(() => {
          if (cancelled || visualReadySent.current) return
          visualReadySent.current = true
          notifyMenuVisualReady()
        })
      })
    }

    const fontsReady = document.fonts.ready.then(() => undefined, () => undefined)
    const animationReady = props.inGame
      ? Promise.resolve()
      : new Promise<void>((resolve) => {
          animationTimer = window.setTimeout(resolve, 190)
        })
    void Promise.all([fontsReady, animationReady]).then(notifyAfterPaint)
    return () => {
      cancelled = true
      window.clearTimeout(animationTimer)
      window.cancelAnimationFrame(firstFrame)
      window.cancelAnimationFrame(secondFrame)
    }
  }, [artReady, props.inGame])

  const activate = (item: MenuItem) => {
    playUiSound('select')
    item.action?.()
  }

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
        event.preventDefault()
        const direction = event.key === 'ArrowDown' ? 1 : -1
        setSelectedIndex((current) => (current + direction + items.length) % items.length)
      } else if (event.key === 'Enter') {
        const item = items[selectedIndex]
        if (item) activate(item)
      }
    }

    window.addEventListener('keydown', onKeyDown)
    return () => window.removeEventListener('keydown', onKeyDown)
  }, [items, selectedIndex])

  const selected = items[selectedIndex] ?? items[0]!

  return (
    <main className={`main-menu${props.inGame ? ' main-menu--ingame' : ''}`}>
      {!props.inGame && (
        <div className="main-menu__art" aria-hidden="true">
          <img
            className="main-menu__scene"
            src={loadscreen}
            alt=""
            draggable={false}
            fetchPriority="high"
            onLoad={() => setArtReady(true)}
            onError={() => setArtReady(true)}
          />
        </div>
      )}
      <div className="main-menu__wash" aria-hidden="true" />

      <section className="main-menu__content" aria-label={t('aria.mainMenu')}>
        <header className="main-menu__brand">
          <h1>
            <span>MTA:SA</span>
            <strong>Neon</strong>
          </h1>
        </header>

        <nav className="main-menu__nav" aria-label={t('aria.mainNavigation')}>
          {items.map((item, index) => {
            const isSelected = index === selectedIndex
            return (
              <button
                key={item.id}
                type="button"
                className={`main-menu__item${isSelected ? ' main-menu__item--selected' : ''}`}
                aria-current={isSelected ? 'page' : undefined}
                onMouseEnter={() => setSelectedIndex(index)}
                onFocus={() => setSelectedIndex(index)}
                onClick={() => activate(item)}
              >
                <span className="main-menu__marker" aria-hidden="true">
                  &#9656;
                </span>
                <span>{item.label}</span>
              </button>
            )
          })}
        </nav>

        <p className="main-menu__caption" aria-live="polite">
          {selected.caption}
        </p>
      </section>

      {!props.inGame && props.featuredServer && (
        <button
          type="button"
          className="main-menu__featured"
          disabled={props.identity.signingIn}
          aria-label={`${props.featuredServer.name}. ${featuredAction}`}
          onClick={() => {
            playUiSound('select')
            props.onPlayFeatured()
          }}
        >
          {props.featuredServer.bannerUrl && (
            <img className="main-menu__featured-banner" src={props.featuredServer.bannerUrl} alt="" draggable={false} />
          )}
          <span className="main-menu__featured-shade" aria-hidden="true" />
          <span className="main-menu__featured-logo">
            {props.featuredServer.logoUrl
              ? <img src={props.featuredServer.logoUrl} alt="" draggable={false} />
              : <span>B</span>}
          </span>
          <span className="main-menu__featured-copy">
            <span className="main-menu__featured-kicker">{t('main.featuredServer')}</span>
            <strong>{props.featuredServer.name}</strong>
            <span className="main-menu__featured-tagline">{props.featuredServer.tagline}</span>
          </span>
          <span className="main-menu__featured-action">{featuredAction} &#9656;</span>
        </button>
      )}

      {!props.inGame && (
        <button
          type="button"
          className={`main-menu__profile${props.identity.authenticated ? ' main-menu__profile--connected' : ''}`}
          aria-label={`${discordTitle}. ${discordAction}`}
          onClick={() => {
            playUiSound('select')
            props.onIdentity()
          }}
        >
          <span className="main-menu__profile-icon">
            <DiscordIcon />
          </span>
          <span className="main-menu__profile-copy">
            <span className="main-menu__profile-kicker">{t('main.identity')}</span>
            <strong>{discordTitle}</strong>
            <span className="main-menu__profile-meta">
              <span className="main-menu__online">
                <i aria-hidden="true" /> {props.identity.status}
              </span>
              <span className="main-menu__profile-action">{discordAction} &#9656;</span>
            </span>
          </span>
        </button>
      )}

      <footer className="main-menu__footer">
        <span>{props.inGame ? t('main.inGame') : t('main.preview')}</span>
        <span className="main-menu__controls">
          {props.inGame && <><kbd>Esc</kbd> {t('common.resume')}&nbsp;&nbsp; </>}
          <kbd>↑</kbd><kbd>↓</kbd> {t('common.select')}&nbsp;&nbsp; <kbd>Enter</kbd> {t('common.confirm')}
        </span>
      </footer>

      {!props.inGame && (
        <LanguageSelector
          locale={props.locale}
          languages={props.languages}
          onSelect={props.onLanguage}
        />
      )}
    </main>
  )
}
