import { useEffect, useRef, useState, type CSSProperties } from 'react'
import { useI18n } from '../i18n'
import { formatNeonServerLink, type ServerItem } from '../types'
import { playUiSound } from '../uiSound'
import {
  IconArrowLeft,
  IconAt,
  IconCheck,
  IconCopy,
  IconGlobe,
  IconPlay,
  Flag,
  LINK_ICONS,
} from './Icons'

interface DetailsPanelProps {
  server: ServerItem | null
  playersViewOpen: boolean
  onOpenPlayers: () => void
  onClosePlayers: () => void
  onConnect: (server: ServerItem) => void
  onCopyLink: (server: ServerItem) => Promise<boolean>
  /** Retourne true si le lien est pris en charge nativement (CEF). */
  onOpenLink: (url: string) => boolean
}

export function DetailsPanel({
  server,
  playersViewOpen,
  onOpenPlayers,
  onClosePlayers,
  onConnect,
  onCopyLink,
  onOpenLink,
}: DetailsPanelProps) {
  const [copyState, setCopyState] = useState<'idle' | 'copied' | 'failed'>('idle')
  const copyResetTimer = useRef<ReturnType<typeof setTimeout> | null>(null)
  const copyGeneration = useRef(0)
  const playersButtonRef = useRef<HTMLButtonElement>(null)
  const backButtonRef = useRef<HTMLButtonElement>(null)
  const previousPlayersViewOpen = useRef(playersViewOpen)
  const { formatNumber, t } = useI18n()

  useEffect(() => {
    if (playersViewOpen && !previousPlayersViewOpen.current) backButtonRef.current?.focus()
    if (!playersViewOpen && previousPlayersViewOpen.current) playersButtonRef.current?.focus()
    previousPlayersViewOpen.current = playersViewOpen
  }, [playersViewOpen])

  useEffect(() => {
    copyGeneration.current += 1
    setCopyState('idle')
    if (copyResetTimer.current) clearTimeout(copyResetTimer.current)
    return () => {
      copyGeneration.current += 1
      if (copyResetTimer.current) clearTimeout(copyResetTimer.current)
    }
  }, [server?.id])

  if (!server) {
    return (
      <aside className="details-empty">
        {t('details.selectServer')}
        <br />
        {t('details.selectServerHint')}
      </aside>
    )
  }

  const copyAddress = async () => {
    playUiSound('select')
    const generation = ++copyGeneration.current
    const success = await onCopyLink(server)
    if (generation !== copyGeneration.current) return
    setCopyState(success ? 'copied' : 'failed')
    if (!success) playUiSound('error')
    if (copyResetTimer.current) clearTimeout(copyResetTimer.current)
    copyResetTimer.current = setTimeout(() => setCopyState('idle'), 1600)
  }

  const serverLink = formatNeonServerLink({ ip: server.ip, port: server.gamePort })

  return (
    <aside className="details">
      <div className="details__scroll">
        {playersViewOpen ? (
          <section id="server-player-list" className="players-view" aria-labelledby="players-view-title">
            <button
              ref={backButtonRef}
              type="button"
              className="players-view__back"
              onClick={onClosePlayers}
            >
              <IconArrowLeft size={14} />
              {t('details.backToServerDetails')}
            </button>
            <div className="players-view__heading">
              <span className="players-view__icon" aria-hidden="true">
                <IconAt size={21} />
              </span>
              <div>
                <span className="details__kicker">{shortName(server.name)}</span>
                <h2 id="players-view-title">{t('common.players')}</h2>
              </div>
              <span className="players-view__count">
                {formatNumber(server.players)} / {formatNumber(server.maxPlayers)}
              </span>
            </div>
            {server.players === 0 ? (
              <p className="players-view__empty">{t('details.noPlayersOnline')}</p>
            ) : (
              <ul
                className="players-view__list"
                aria-label={t('details.playersOnline', { count: formatNumber(server.players) })}
              >
                {server.playerList.map((player, index) => (
                  <li key={`${player.name}-${index}`}>
                    <span className="players-view__marker" aria-hidden="true">@</span>
                    <span className="players-view__name">{player.name}</span>
                    {player.ping !== undefined && <span className="players-view__ping">{player.ping} ms</span>}
                  </li>
                ))}
              </ul>
            )}
          </section>
        ) : (
          <>
            {server.bannerUrl && (
              <div className="details__banner" aria-hidden="true">
                <img src={server.bannerUrl} alt="" draggable={false} referrerPolicy="no-referrer" />
              </div>
            )}
            <div className="details__identity">
              <span
                className="details__logo"
                style={{ '--server-accent': server.accent ?? '#accbf1' } as CSSProperties}
                aria-hidden="true"
              >
                <span>{serverInitials(server.name)}</span>
                {server.logoUrl && <img src={server.logoUrl} alt="" draggable={false} referrerPolicy="no-referrer" />}
              </span>
              <div className="details__heading">
                <span className="details__kicker">{t('details.selectedDestination')}</span>
                <div className="details__name">
                  <span>{shortName(server.name)}</span>
                </div>
              </div>
            </div>

            <p className="details__desc">{server.description}</p>

            <div className={`details__share details__share--${copyState}`}>
              <div>
                <span>{t('details.serverLink')}</span>
                <code>{serverLink}</code>
              </div>
              <button
                type="button"
                title={t('details.copyAddress', { address: serverLink })}
                onClick={() => void copyAddress()}
              >
                {copyState === 'copied' ? <IconCheck size={14} /> : <IconCopy size={14} />}
                <span aria-live="polite">
                  {copyState === 'copied'
                    ? t('details.copied')
                    : copyState === 'failed'
                      ? t('details.copyFailed')
                      : t('details.copyLink')}
                </span>
              </button>
            </div>

            <div className="details__facts">
              <div>
                <span>{t('common.players')}</span>
                <strong>{formatNumber(server.players)} / {formatNumber(server.maxPlayers)}</strong>
              </div>
              <div>
                <span>{t('common.ping')}</span>
                <strong>{server.ping !== undefined ? `${server.ping} ms` : '—'}</strong>
              </div>
              <div>
                <span>{t('common.mode')}</span>
                <strong>{server.gameMode}</strong>
              </div>
            </div>

            {server.links.length > 0 && (
              <div className="details__links">
                {server.links.map((link) => {
                  const Icon = LINK_ICONS[link.kind]
                  return (
                    <a
                      key={link.kind + link.label}
                      className="details__link"
                      href={link.url}
                      target="_blank"
                      rel="noreferrer"
                      onClick={(e) => {
                        playUiSound('select')
                        if (onOpenLink(link.url)) e.preventDefault()
                      }}
                    >
                      <Icon size={14} />
                      {link.label}
                    </a>
                  )
                })}
              </div>
            )}

            <section className="details__section">
              <div className="details__section-head">
                <IconGlobe size={14} />
                {t('details.regionsLanguages')}
              </div>
              <div className="details__chips">
                {(server.countries?.length ? server.countries : server.country ? [server.country] : []).map(
                  (country) => <Flag key={country} country={country} />,
                )}
                {server.languages.map((lang) => (
                  <span key={lang} className="chip">
                    {lang}
                  </span>
                ))}
              </div>
            </section>

            <section className="details__section details__players-preview" aria-labelledby="players-preview-title">
              <div className="details__section-head">
                <IconAt size={14} />
                <span id="players-preview-title">
                  {t('details.playersOnline', { count: formatNumber(server.players) })}
                </span>
                {server.players > 0 && (
                  <button
                    ref={playersButtonRef}
                    type="button"
                    className="details__view-all"
                    aria-controls="server-player-list"
                    aria-expanded={playersViewOpen}
                    onClick={onOpenPlayers}
                  >
                    {t('details.viewAllPlayers', { count: formatNumber(server.players) })}
                  </button>
                )}
              </div>
              {server.players === 0 ? (
                <p className="details__players-empty">{t('details.noPlayersOnline')}</p>
              ) : (
                <ul
                  className="details__player-chips"
                  aria-label={t('details.playersOnline', { count: formatNumber(server.players) })}
                >
                  {server.playerList.slice(0, 6).map((player, index) => (
                    <li key={`${player.name}-${index}`} className="details__player-chip">
                      <span aria-hidden="true">@</span>
                      {player.name}
                    </li>
                  ))}
                </ul>
              )}
            </section>
          </>
        )}
      </div>

      <div className="details__footer" key={server.id}>
        <span>{t('details.ready')}</span>
        <button type="button" className="connect-btn" onClick={() => onConnect(server)}>
          <IconPlay size={18} />
          {t('details.joinServer')}
        </button>
      </div>
    </aside>
  )
}

/** Tronque les noms décorés ("[HUN] X | Y | discord…") pour la bannière. */
function shortName(name: string): string {
  const cleaned = name
    .replace(/[|[\]=-]+/g, ' ')
    .replace(/\s+/g, ' ')
    .trim()
  return cleaned.length > 26 ? `${cleaned.slice(0, 26)}…` : cleaned
}

function serverInitials(name: string): string {
  const words = name.replace(/[^\p{L}\p{N}]+/gu, ' ').trim().split(/\s+/)
  return words.slice(0, 2).map((word) => word[0]).join('').toUpperCase() || 'N'
}
