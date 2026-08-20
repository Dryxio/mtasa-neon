import { memo, useEffect, useRef, useState, type CSSProperties } from 'react'
import { useI18n } from '../i18n'
import type { ServerItem } from '../types'
import { Flag, IconLock, IconStar, IconStarFilled } from './Icons'

// Keep these values aligned with `.server-row`: the virtualizer needs the
// painted card height and the intentional breathing room between cards.
const ROW_HEIGHT = 118
const ROW_GAP = 4
const STRIDE = ROW_HEIGHT + ROW_GAP
const OVERSCAN = 6

interface ServerListProps {
  servers: readonly ServerItem[]
  selectedId: string | null
  directTarget: string | null
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
  const { formatNumber, t } = useI18n()
  const offline = server.scanState === 'offline'
  return (
    <div
      className={`server-row${selected ? ' server-row--selected' : ''}${server.isFeatured ? ' server-row--featured' : ''}${offline ? ' server-row--offline' : ''}`}
      style={{ top }}
      onClick={() => onSelect(server.id)}
      onDoubleClick={() => onJoin(server)}
      role="row"
      aria-selected={selected}
    >
      <span
        className="server-row__logo"
        style={{ '--server-accent': server.accent ?? '#accbf1' } as CSSProperties}
        aria-hidden="true"
      >
        <span>{serverInitials(server.name)}</span>
        {server.logoUrl && <img src={server.logoUrl} alt="" draggable={false} referrerPolicy="no-referrer" />}
      </span>
      <div className="server-row__body">
        <div className="server-row__top">
          <span className="server-row__name">{server.name}</span>
        </div>
        <div className="server-row__bottom">
          <span className="server-row__tagline">{server.tagline}</span>
          {server.isFeatured && <span className="server-row__featured">{t('server.featured')}</span>}
        </div>
      </div>
      <div className="server-row__side">
        {server.passworded && (
          <span className="server-row__lock" title={t('server.passwordProtected')}>
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
          title={server.isFavourite ? t('server.removeFavourite') : t('server.addFavourite')}
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
            title={server.isStatusVerified ? undefined : t('server.playersUnverified')}
          >
            {offline
              ? '—'
              : `${formatNumber(server.players)} / ${formatNumber(server.maxPlayers)}${server.isStatusVerified ? '' : ' *'}`}
          </span>
          <span className="server-row__ping">
            {offline ? t('server.offline') : server.ping !== undefined ? `${server.ping} ms` : '…'}
          </span>
        </div>
      </div>
    </div>
  )
})

function serverInitials(name: string): string {
  const words = name.replace(/[^\p{L}\p{N}]+/gu, ' ').trim().split(/\s+/)
  return words.slice(0, 2).map((word) => word[0]).join('').toUpperCase() || 'N'
}

export function ServerList(props: ServerListProps) {
  const { t } = useI18n()
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
        {props.directTarget ? (
          <div className="list-empty list-empty--direct">
            <strong>{t('browser.directReady')}</strong>
            <code>{props.directTarget}</code>
            <span>{t('browser.directHint')}</span>
          </div>
        ) : (
          <div className="list-empty">
            {t('browser.empty')}
            <br />
            {t('browser.emptyHint')}
          </div>
        )}
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
      aria-label={t('aria.serverList')}
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
