import { useState } from 'react'
import { useI18n } from '../i18n'
import type { ServerItem } from '../types'
import { playUiSound } from '../uiSound'
import {
  IconCheck,
  IconCopy,
  IconGlobe,
  IconPlay,
  Flag,
  LINK_ICONS,
} from './Icons'

interface DetailsPanelProps {
  server: ServerItem | null
  onConnect: (server: ServerItem) => void
  /** Retourne true si le lien est pris en charge nativement (CEF). */
  onOpenLink: (url: string) => boolean
}

export function DetailsPanel({ server, onConnect, onOpenLink }: DetailsPanelProps) {
  const [copied, setCopied] = useState(false)
  const { formatNumber, t } = useI18n()

  if (!server) {
    return (
      <aside className="details-empty">
        {t('details.selectServer')}
        <br />
        {t('details.selectServerHint')}
      </aside>
    )
  }

  const copyAddress = () => {
    playUiSound('select')
    void navigator.clipboard?.writeText(`mtasa://${server.ip}:${server.gamePort}`)
    setCopied(true)
    setTimeout(() => setCopied(false), 1200)
  }

  return (
    <aside className="details">
      <div className="details__scroll">
        <span className="details__kicker">{t('details.selectedDestination')}</span>
        <div className="details__name">
          <span>{shortName(server.name)}</span>
          <button
            type="button"
            className="details__copy"
            title={t('details.copyAddress', { address: `mtasa://${server.ip}:${server.gamePort}` })}
            onClick={copyAddress}
          >
            {copied ? <IconCheck size={14} /> : <IconCopy size={14} />}
          </button>
        </div>

        <p className="details__desc">{server.description}</p>

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
