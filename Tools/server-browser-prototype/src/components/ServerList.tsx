import { memo, useEffect, useRef, useState } from 'react'
import type { ServerItem } from '../types'
import { Flag, IconLock, IconStar, IconStarFilled } from './Icons'

const ROW_HEIGHT = 120
const ROW_GAP = 2
const STRIDE = ROW_HEIGHT + ROW_GAP
const OVERSCAN = 6

interface ServerListProps {
  servers: readonly ServerItem[]
  selectedId: string | null
  onSelect: (id: string) => void
  onJoin: (server: ServerItem) => void
  onToggleFavourite: (server: ServerItem) => void
}

function serverCountries(server: ServerItem): string[] {
  if (server.countries?.length) return server.countries
  return server.country ? [server.country] : []
}

const Row = memo(function Row({
  server,
  top,
  selected,
  onSelect,
  onJoin,
  onToggleFavourite,
}: {
  server: ServerItem
  top: number
  selected: boolean
  onSelect: (id: string) => void
  onJoin: (server: ServerItem) => void
  onToggleFavourite: (server: ServerItem) => void
}) {
  const offline = server.scanState === 'offline'
  return (
    <div
      className={`server-row${selected ? ' server-row--selected' : ''}${offline ? ' server-row--offline' : ''}`}
      style={{ top }}
      onClick={() => onSelect(server.id)}
      onDoubleClick={() => onJoin(server)}
      role="row"
      aria-selected={selected}
    >
      <div className="server-row__body">
        <div className="server-row__top">
          <span className="server-row__name">{server.name}</span>
        </div>
        <span className="server-row__tagline">{server.tagline}</span>
      </div>
      <div className="server-row__side">
        {server.passworded && (
          <span className="server-row__lock" title="Password protected">
            <IconLock size={13} />
          </span>
        )}
        <span className="server-row__countries" title={serverCountries(server).join(' / ')}>
          {serverCountries(server).map((country) => (
            <Flag key={country} country={country} />
          ))}
        </span>
        <button
          type="button"
          className={`server-row__star${server.isFavourite ? ' server-row__star--active' : ''}`}
          title={server.isFavourite ? 'Remove from favourites' : 'Add to favourites'}
          onClick={(e) => {
            e.stopPropagation()
            onToggleFavourite(server)
          }}
        >
          {server.isFavourite ? <IconStarFilled size={17} /> : <IconStar size={17} />}
        </button>
        <div className="server-row__meta">
          <span
            className={`server-row__players${server.isStatusVerified ? '' : ' server-row__players--unverified'}`}
            title={server.isStatusVerified ? undefined : 'Player count not verified'}
          >
            {offline ? '—' : `${server.players} / ${server.maxPlayers}${server.isStatusVerified ? '' : ' *'}`}
          </span>
          <span className="server-row__ping">
            {offline ? 'offline' : server.ping !== undefined ? `${server.ping} ms` : '…'}
          </span>
        </div>
      </div>
    </div>
  )
})

export function ServerList(props: ServerListProps) {
  const scrollRef = useRef<HTMLDivElement>(null)
  const [scrollTop, setScrollTop] = useState(0)
  const [viewport, setViewport] = useState(600)

  useEffect(() => {
    const el = scrollRef.current
    if (!el) return
    const observer = new ResizeObserver(() => setViewport(el.clientHeight))
    observer.observe(el)
    return () => observer.disconnect()
  }, [])

  // Garde la sélection visible lors de la navigation clavier — uniquement
  // quand la sélection change, pas quand la liste bouge pendant le scan.
  const lastSelected = useRef<string | null>(null)
  useEffect(() => {
    const el = scrollRef.current
    if (!el || !props.selectedId || props.selectedId === lastSelected.current) return
    lastSelected.current = props.selectedId
    const index = props.servers.findIndex((s) => s.id === props.selectedId)
    if (index < 0) return
    const rowTop = index * STRIDE
    const rowBottom = rowTop + ROW_HEIGHT
    if (rowTop < el.scrollTop) el.scrollTo({ top: rowTop })
    else if (rowBottom > el.scrollTop + el.clientHeight) {
      el.scrollTo({ top: rowBottom - el.clientHeight })
    }
  }, [props.selectedId, props.servers])

  if (props.servers.length === 0) {
    return (
      <div className="server-list">
        <div className="list-empty">
          No servers match your search.
          <br />
          Try clearing filters or refreshing the list.
        </div>
      </div>
    )
  }

  const first = Math.max(0, Math.floor(scrollTop / STRIDE) - OVERSCAN)
  const last = Math.min(
    props.servers.length,
    Math.ceil((scrollTop + viewport) / STRIDE) + OVERSCAN,
  )

  return (
    <div
      className="server-list"
      ref={scrollRef}
      onScroll={(e) => setScrollTop(e.currentTarget.scrollTop)}
      role="grid"
      aria-label="Server list"
    >
      <div className="server-list__spacer" style={{ height: props.servers.length * STRIDE - ROW_GAP }}>
        {props.servers.slice(first, last).map((server, i) => (
          <Row
            key={server.id}
            server={server}
            top={(first + i) * STRIDE}
            selected={server.id === props.selectedId}
            onSelect={props.onSelect}
            onJoin={props.onJoin}
            onToggleFavourite={props.onToggleFavourite}
          />
        ))}
      </div>
    </div>
  )
}
