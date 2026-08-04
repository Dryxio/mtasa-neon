import { useEffect, useRef, useState } from 'react'
import type { BrowserFilters } from '../search'
import { ALL_SOURCES, SOURCE_LABELS, type ServerSource } from '../types'
import { playUiSound } from '../uiSound'
import {
  IconCheck,
  IconChevronDown,
  IconFilter,
  IconPlay,
  IconRefresh,
  IconSearch,
} from './Icons'

interface ToolbarProps {
  source: ServerSource
  refreshing: boolean
  query: string
  filters: BrowserFilters
  searchRef: React.RefObject<HTMLInputElement | null>
  onSource: (source: ServerSource) => void
  onRefresh: () => void
  onQuery: (query: string) => void
  onFilters: (filters: BrowserFilters) => void
  onConnect: () => void
}

const FILTER_LABELS: Record<keyof BrowserFilters, string> = {
  hideFull: 'Hide full servers',
  hideEmpty: 'Hide empty servers',
  hideLocked: 'Hide locked servers',
  hideIncompatible: 'Hide other versions',
  hideOffline: 'Hide offline servers',
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
          {SOURCE_LABELS[props.source]}
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
                {SOURCE_LABELS[source]}
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
        title="Refresh"
        onClick={() => {
          playUiSound('select')
          props.onRefresh()
        }}
      >
        <IconRefresh size={15} />
      </button>

      <div className="searchbar">
        <IconSearch size={15} />
        <input
          ref={props.searchRef}
          value={props.query}
          onChange={(e) => props.onQuery(e.target.value)}
          placeholder="Find a Neon server"
          spellCheck={false}
        />
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
        Connect
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
          Filters{activeFilters > 0 ? ` · ${activeFilters}` : ''}
          <span className="chevron" style={{ color: 'var(--accent)' }}>
            <IconFilter size={13} />
          </span>
        </button>
        {filtersOpen && (
          <div className="menu menu--right" role="menu">
            {(Object.keys(FILTER_LABELS) as (keyof BrowserFilters)[]).map((key) => {
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
                  {FILTER_LABELS[key]}
                </button>
              )
            })}
          </div>
        )}
      </div>
    </div>
  )
}
