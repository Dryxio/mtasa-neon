import { useEffect, useRef, useState } from 'react'
import { useI18n, type TranslationKey } from '../i18n'
import type { BrowserFilters } from '../search'
import { ALL_SOURCES, type ServerSource } from '../types'
import { playUiSound } from '../uiSound'
import {
  IconCheck,
  IconChevronDown,
  IconFilter,
  IconLink,
  IconPlay,
  IconRefresh,
  IconSearch,
} from './Icons'

interface ToolbarProps {
  source: ServerSource
  refreshing: boolean
  query: string
  filters: BrowserFilters
  directTarget: string | null
  searchRef: React.RefObject<HTMLInputElement | null>
  onSource: (source: ServerSource) => void
  onRefresh: () => void
  onQuery: (query: string) => void
  onFilters: (filters: BrowserFilters) => void
  onConnect: () => void
}

const SOURCE_LABEL_KEYS: Record<ServerSource, TranslationKey> = {
  internet: 'source.neon',
  lan: 'source.local',
  favourites: 'source.favourites',
  recent: 'source.recent',
}

const FILTER_LABEL_KEYS: Record<keyof BrowserFilters, TranslationKey> = {
  hideFull: 'browser.filter.hideFull',
  hideEmpty: 'browser.filter.hideEmpty',
  hideLocked: 'browser.filter.hideLocked',
  hideIncompatible: 'browser.filter.hideIncompatible',
  hideOffline: 'browser.filter.hideOffline',
}

/** Ferme le menu au clic hors de l'ancre. */
function useClickOutside(open: boolean, close: () => void) {
  const ref = useRef<HTMLDivElement>(null)
  useEffect(() => {
    if (!open) return
    const onDown = (e: MouseEvent) => {
      if (ref.current && !ref.current.contains(e.target as Node)) close()
    }
    window.addEventListener('mousedown', onDown)
    return () => window.removeEventListener('mousedown', onDown)
  }, [open, close])
  return ref
}

export function Toolbar(props: ToolbarProps) {
  const { t } = useI18n()
  const [sourceOpen, setSourceOpen] = useState(false)
  const [filtersOpen, setFiltersOpen] = useState(false)
  const sourceRef = useClickOutside(sourceOpen, () => setSourceOpen(false))
  const filtersRef = useClickOutside(filtersOpen, () => setFiltersOpen(false))

  const activeFilters = Object.values(props.filters).filter(Boolean).length

  return (
    <div className="toolbar">
      <div className="menu-anchor" ref={sourceRef}>
        <button
          type="button"
          className="tool-btn"
          onClick={() => {
            playUiSound('select')
            setSourceOpen((v) => !v)
          }}
        >
          {t(SOURCE_LABEL_KEYS[props.source])}
          <span className="chevron">
            <IconChevronDown size={14} />
          </span>
        </button>
        {sourceOpen && (
          <div className="menu menu--left" role="menu">
            {ALL_SOURCES.map((source) => (
              <button
                key={source}
                type="button"
                className={`menu__item${source === props.source ? ' menu__item--active' : ''}`}
                onMouseEnter={() => playUiSound('highlight')}
                onClick={() => {
                  playUiSound('select')
                  setSourceOpen(false)
                  props.onSource(source)
                }}
              >
                {t(SOURCE_LABEL_KEYS[source])}
                {source === props.source && (
                  <span className="check">
                    <IconCheck size={14} />
                  </span>
                )}
              </button>
            ))}
          </div>
        )}
      </div>

      <button
        type="button"
        className={`tool-btn tool-btn--icon${props.refreshing ? ' tool-btn--refreshing' : ''}`}
        title={t('common.refresh')}
        onClick={() => {
          playUiSound('select')
          props.onRefresh()
        }}
      >
        <IconRefresh size={15} />
      </button>

      <div className={`searchbar${props.directTarget ? ' searchbar--direct' : ''}`}>
        {props.directTarget ? <IconLink size={15} /> : <IconSearch size={15} />}
        <input
          ref={props.searchRef}
          value={props.query}
          onChange={(e) => props.onQuery(e.target.value)}
          placeholder={t('browser.searchPlaceholder')}
          spellCheck={false}
        />
        {props.directTarget && <span className="searchbar__mode">{t('browser.direct')}</span>}
      </div>

      <button
        type="button"
        className="tool-btn tool-btn--connect"
        onClick={() => {
          playUiSound('select')
          props.onConnect()
        }}
      >
        <IconPlay size={12} />
        {props.directTarget ? t('browser.connectToAddress') : t('common.connect')}
      </button>

      <div className="menu-anchor" ref={filtersRef}>
        <button
          type="button"
          className="tool-btn"
          onClick={() => {
            playUiSound('select')
            setFiltersOpen((v) => !v)
          }}
        >
          {t('common.filters')}{activeFilters > 0 ? ` · ${activeFilters}` : ''}
          <span className="chevron" style={{ color: 'var(--accent)' }}>
            <IconFilter size={13} />
          </span>
        </button>
        {filtersOpen && (
          <div className="menu menu--right" role="menu">
            {(Object.keys(FILTER_LABEL_KEYS) as (keyof BrowserFilters)[]).map((key) => {
              const on = props.filters[key]
              return (
                <button
                  key={key}
                  type="button"
                  className="menu__item"
                  onMouseEnter={() => playUiSound('highlight')}
                  onClick={() => {
                    playUiSound('select')
                    props.onFilters({ ...props.filters, [key]: !on })
                  }}
                >
                  <span className={`menu__toggle-box${on ? ' menu__toggle-box--on' : ''}`}>
                    {on && <IconCheck size={11} />}
                  </span>
                  {t(FILTER_LABEL_KEYS[key])}
                </button>
              )
            })}
          </div>
        )}
      </div>
    </div>
  )
}
