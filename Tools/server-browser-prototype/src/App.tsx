import { useEffect, useMemo, useRef, useState } from 'react'
import { DetailsPanel } from './components/DetailsPanel'
import { Header } from './components/Header'
import { ServerList } from './components/ServerList'
import { StatusBar } from './components/StatusBar'
import { Toolbar } from './components/Toolbar'
import { visibleServers } from './search'
import { actions, useBrowserState } from './store'
import { chooseLaunchLoadscreen } from './loadscreen'
import { parseServerAddress } from './types'
import { playUiSound } from './uiSound'
import { useI18n } from './i18n'

export function App() {
  const { t } = useI18n()
  const state = useBrowserState()
  const searchRef = useRef<HTMLInputElement>(null)
  const hasInitialSelection = useRef(false)
  const [loadscreen] = useState(chooseLaunchLoadscreen)

  useEffect(() => {
    void actions.init()
  }, [])

  const servers = useMemo(
    () => visibleServers([...state.servers.values()], state.query, state.filters),
    [state.servers, state.query, state.filters],
  )

  const selected = state.selectedId ? (state.servers.get(state.selectedId) ?? null) : null

  useEffect(() => {
    if (!state.selectedId) return
    if (hasInitialSelection.current) playUiSound('highlight')
    else hasInitialSelection.current = true
  }, [state.selectedId])

  // Bouton Connect de la barre d'outils : adresse directe si la recherche en
  // contient une ("ip:port" / "mtasa://…"), sinon serveur sélectionné.
  const connectFromToolbar = () => {
    const address = parseServerAddress(state.query)
    if (address) actions.connectToAddress(address)
    else if (selected) actions.connectTo(selected)
  }

  // Navigation clavier globale : ↑/↓ naviguent, ↵ rejoint, Échap ferme.
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (state.connect.phase !== 'idle') {
        if (e.key === 'Escape') {
          actions.dismissConnect()
        }
        return
      }
      const inSearch = document.activeElement === searchRef.current
      if (e.key === 'ArrowUp' || e.key === 'ArrowDown') {
        e.preventDefault()
        actions.moveSelection(
          servers.map((s) => s.id),
          e.key === 'ArrowDown' ? 1 : -1,
        )
      } else if (e.key === 'Enter') {
        const address = inSearch ? parseServerAddress(state.query) : null
        if (address) {
          playUiSound('select')
          actions.connectToAddress(address)
        } else if (selected) {
          playUiSound('select')
          actions.connectTo(selected)
        }
      } else if (e.key === 'Escape' && inSearch) {
        playUiSound('back')
        actions.setQuery('')
        searchRef.current?.blur()
      } else if (e.key === 'Escape') {
        playUiSound('back')
        actions.closeBrowser()
      } else if (!inSearch && e.key.length === 1 && !e.metaKey && !e.ctrlKey && !e.altKey) {
        e.preventDefault()
        searchRef.current?.focus()
        actions.setQuery(`${state.query}${e.key}`)
      }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [servers, selected, state.connect.phase, state.query])

  return (
    <div className="shell">
      <div className="browser-art" aria-hidden="true">
        <img src={loadscreen} alt="" draggable={false} />
      </div>
      <div className="browser-wash" aria-hidden="true" />
      <Header
        stats={state.stats}
        onClose={() => {
          playUiSound('back')
          actions.closeBrowser()
        }}
      />
      <div className="main">
        <Toolbar
          source={state.source}
          refreshing={state.refreshing}
          query={state.query}
          filters={state.filters}
          searchRef={searchRef}
          onSource={(source) => void actions.setSource(source)}
          onRefresh={() => void actions.refresh()}
          onQuery={actions.setQuery}
          onFilters={actions.setFilters}
          onConnect={connectFromToolbar}
        />
        <div className="list-zone">
          <div className="list-zone__headings" aria-hidden="true">
            <span>{t('browser.heading.destinations')}</span>
            <span>{t('common.players')} &nbsp; {t('common.ping')}</span>
          </div>
          <ServerList
            servers={servers}
            selectedId={state.selectedId}
            onSelect={actions.select}
            onJoin={(server) => {
              playUiSound('select')
              actions.connectTo(server)
            }}
            onToggleFavourite={(server) => {
              playUiSound('select')
              actions.toggleFavourite(server)
            }}
          />
        </div>
        <div className="details-zone">
          <DetailsPanel
            server={selected}
            onConnect={(server) => {
              playUiSound('select')
              actions.connectTo(server)
            }}
            onOpenLink={actions.openExternal}
          />
        </div>
      </div>
      <StatusBar stats={state.stats} progress={state.progress} notice={state.notice} />

    </div>
  )
}
