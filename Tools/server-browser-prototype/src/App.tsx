import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { DetailsPanel } from './components/DetailsPanel'
import { Header } from './components/Header'
import { ServerList } from './components/ServerList'
import { StatusBar } from './components/StatusBar'
import { Toolbar } from './components/Toolbar'
import { visibleServers } from './search'
import { actions, useBrowserState } from './store'
import { chooseLaunchLoadscreen } from './loadscreen'
import { formatNeonServerLink, parseServerAddress } from './types'
import { playUiSound } from './uiSound'
import { useI18n } from './i18n'

export function App() {
  const { t } = useI18n()
  const state = useBrowserState()
  const searchRef = useRef<HTMLInputElement>(null)
  const hasInitialSelection = useRef(false)
  const [loadscreen] = useState(chooseLaunchLoadscreen)
  const [playersViewServerId, setPlayersViewServerId] = useState<string | null>(null)

  useEffect(() => {
    actions.resume()
    void actions.init()
    return actions.suspend
  }, [])

  const directAddress = useMemo(() => parseServerAddress(state.query), [state.query])
  const directTarget = directAddress ? formatNeonServerLink(directAddress) : null
  const servers = useMemo(
    // A recognized address is a connection intent, not a search term. Keep the
    // current destinations visible so Direct mode never looks like an empty list.
    () => visibleServers([...state.servers.values()], directAddress ? '' : state.query, state.filters),
    [directAddress, state.servers, state.query, state.filters],
  )

  const selected = state.selectedId ? (state.servers.get(state.selectedId) ?? null) : null
  const playersViewOpen = selected !== null && playersViewServerId === selected.id

  useEffect(() => {
    if (!directAddress) return
    const matchingServer = [...state.servers.values()].find(
      (server) => server.ip.toLowerCase() === directAddress.ip && server.gamePort === directAddress.port,
    )
    if (matchingServer && matchingServer.id !== state.selectedId) actions.select(matchingServer.id)
  }, [directAddress, state.selectedId, state.servers])

  useEffect(() => {
    if (!state.selectedId) return
    if (hasInitialSelection.current) playUiSound('highlight')
    else hasInitialSelection.current = true
  }, [state.selectedId])

  useEffect(() => {
    if (playersViewServerId !== null && playersViewServerId !== state.selectedId) setPlayersViewServerId(null)
  }, [playersViewServerId, state.selectedId])

  // Bouton Connect de la barre d'outils : adresse directe si la recherche en
  // contient une, sinon serveur sélectionné.
  const connectFromToolbar = useCallback(() => {
    if (directAddress) actions.connectToAddress(directAddress)
    else if (selected) actions.connectTo(selected)
  }, [directAddress, selected])

  // Keep row callbacks stable so background scan/progress updates do not
  // invalidate every memoized server card while the list is scrolling.
  const joinServer = useCallback((server: Parameters<typeof actions.connectTo>[0]) => {
    playUiSound('select')
    actions.connectTo(server)
  }, [])

  const toggleFavourite = useCallback((server: Parameters<typeof actions.toggleFavourite>[0]) => {
    playUiSound('select')
    actions.toggleFavourite(server)
  }, [])

  // Navigation clavier globale : ↑/↓ naviguent, ↵ rejoint, Échap ferme.
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (state.connect.phase !== 'idle') {
        if (e.key === 'Escape') {
          actions.dismissConnect()
        }
        return
      }
      if (playersViewOpen) {
        if (e.key === 'Escape') {
          e.preventDefault()
          playUiSound('back')
          setPlayersViewServerId(null)
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
        if (directAddress) {
          playUiSound('select')
          actions.connectToAddress(directAddress)
        } else if (selected) {
          playUiSound('select')
          actions.connectTo(selected)
        }
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
  }, [directAddress, playersViewOpen, servers, selected, state.connect.phase, state.query])

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
          directTarget={directTarget}
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
            directTarget={directTarget}
            onSelect={actions.select}
            onJoin={joinServer}
            onToggleFavourite={toggleFavourite}
          />
        </div>
        <div className="details-zone">
          <DetailsPanel
            server={selected}
            playersViewOpen={playersViewOpen}
            onOpenPlayers={() => {
              if (!selected) return
              playUiSound('select')
              setPlayersViewServerId(selected.id)
            }}
            onClosePlayers={() => {
              playUiSound('back')
              setPlayersViewServerId(null)
            }}
            onConnect={(server) => {
              playUiSound('select')
              actions.connectTo(server)
            }}
            onCopyLink={actions.copyServerLink}
            onOpenLink={actions.openExternal}
          />
        </div>
      </div>
      <StatusBar progress={state.progress} notice={state.notice} />

    </div>
  )
}
