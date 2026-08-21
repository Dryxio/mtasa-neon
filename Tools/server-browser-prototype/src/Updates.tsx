import { useEffect, useRef, useState } from 'react'
import { chooseLaunchLoadscreen } from './loadscreen'
import {
  findNeonRelease,
  formatReleaseDate,
  formatReleaseVersion,
  LATEST_NEON_RELEASE,
  NEON_RELEASES,
} from './releaseNotes'
import { playUiSound } from './uiSound'
import './Updates.css'

interface UpdatesProps {
  buildNumber: number
  onClose: () => void
}

export function Updates({ buildNumber, onClose }: UpdatesProps) {
  const currentRelease = findNeonRelease(buildNumber) ?? (buildNumber === 0 ? LATEST_NEON_RELEASE : undefined)
  const currentIndex = Math.max(0, NEON_RELEASES.findIndex((release) => release.build === currentRelease?.build))
  const [selectedIndex, setSelectedIndex] = useState(currentIndex)
  const [loadscreen] = useState(chooseLaunchLoadscreen)
  const shellRef = useRef<HTMLElement>(null)
  const historyRef = useRef<HTMLDivElement>(null)
  const detailRef = useRef<HTMLElement>(null)
  const selected = NEON_RELEASES[selectedIndex] ?? LATEST_NEON_RELEASE

  useEffect(() => {
    const shell = shellRef.current
    if (!shell) return

    const onWheel = (event: WheelEvent) => {
      const detail = detailRef.current
      const target = event.target
      if (!detail || !(target instanceof Node) || event.deltaY === 0 || detail.contains(target)) return

      const history = historyRef.current
      if (history?.contains(target)) {
        const historyCanMove = event.deltaY < 0
          ? history.scrollTop > 0
          : history.scrollTop + history.clientHeight < history.scrollHeight
        if (historyCanMove) return
      }

      const detailCanMove = event.deltaY < 0
        ? detail.scrollTop > 0
        : detail.scrollTop + detail.clientHeight < detail.scrollHeight
      if (!detailCanMove) return

      const scale = event.deltaMode === WheelEvent.DOM_DELTA_LINE
        ? 40
        : event.deltaMode === WheelEvent.DOM_DELTA_PAGE
          ? detail.clientHeight
          : 1
      detail.scrollTop += event.deltaY * scale
      event.preventDefault()
    }

    // CEF latches a wheel gesture to the element beneath its first event. The
    // history, gutters and header are not useful scroll targets most of the
    // time, so forward only those otherwise inert gestures to the release body.
    shell.addEventListener('wheel', onWheel, { capture: true, passive: false })
    return () => shell.removeEventListener('wheel', onWheel, { capture: true })
  }, [])

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') {
        event.preventDefault()
        playUiSound('back')
        onClose()
      } else if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
        event.preventDefault()
        const direction = event.key === 'ArrowDown' ? 1 : -1
        setSelectedIndex((current) => (current + direction + NEON_RELEASES.length) % NEON_RELEASES.length)
        playUiSound('highlight')
      } else if (event.key === 'Home') {
        event.preventDefault()
        setSelectedIndex(0)
      } else if (event.key === 'End') {
        event.preventDefault()
        setSelectedIndex(NEON_RELEASES.length - 1)
      }
    }

    window.addEventListener('keydown', onKeyDown)
    return () => window.removeEventListener('keydown', onKeyDown)
  }, [onClose])

  return (
    <main ref={shellRef} className="updates-shell">
      <div className="updates-shell__art" aria-hidden="true">
        <img src={loadscreen} alt="" draggable={false} />
      </div>
      <div className="updates-shell__wash" aria-hidden="true" />

      <header className="updates-header">
        <div>
          <span className="updates-header__eyebrow">MTA:SA Neon</span>
          <h1>What’s new</h1>
          <p>The latest Neon changes, without the technical noise.</p>
        </div>
        <button
          type="button"
          className="updates-header__back"
          onClick={() => {
            playUiSound('back')
            onClose()
          }}
        >
          <span aria-hidden="true">‹</span> Back to menu
        </button>
      </header>

      <div className="updates-layout">
        <aside className="updates-history" aria-label="Release history">
          <div className="updates-history__heading">
            <span>Release history</span>
            <small>{NEON_RELEASES.length} releases</small>
          </div>
          <div ref={historyRef} className="updates-history__list">
            {NEON_RELEASES.map((entry, index) => {
              const active = index === selectedIndex
              return (
                <button
                  type="button"
                  key={entry.build}
                  className={`updates-history__item${active ? ' updates-history__item--active' : ''}`}
                  aria-current={active ? 'true' : undefined}
                  onMouseEnter={() => setSelectedIndex(index)}
                  onFocus={() => setSelectedIndex(index)}
                  onClick={() => {
                    playUiSound('select')
                    setSelectedIndex(index)
                  }}
                >
                  <span className="updates-history__marker" aria-hidden="true">▸</span>
                  <span className="updates-history__copy">
                    <strong>Neon {formatReleaseVersion(entry)}</strong>
                    <span>{entry.title}</span>
                    <time dateTime={entry.date}>{formatReleaseDate(entry)}</time>
                  </span>
                  {entry.build === currentRelease?.build && <span className="updates-history__badge">Current</span>}
                </button>
              )
            })}
          </div>
        </aside>

        <article ref={detailRef} className="updates-detail" key={selected.build}>
          <header className="updates-detail__header">
            <div className="updates-detail__meta">
              <span>Version {formatReleaseVersion(selected)}</span>
              <time dateTime={selected.date}>{formatReleaseDate(selected)}</time>
            </div>
            <h2>{selected.title}</h2>
            <p>{selected.summary}</p>
          </header>

          <div className="updates-detail__sections">
            {selected.sections.map((section) => (
              <section className={`updates-section updates-section--${section.tone}`} key={section.label}>
                <h3>{section.label}</h3>
                <ul>
                  {section.items.map((item) => <li key={item}>{item}</li>)}
                </ul>
              </section>
            ))}
          </div>

          <footer className="updates-detail__footer">
            <span>Neon build {selected.build}</span>
            <span>{selected.sections.reduce((total, section) => total + section.items.length, 0)} documented changes</span>
          </footer>
        </article>
      </div>

      <footer className="updates-controls">
        <span>Neon release notes</span>
        <span><kbd>↑</kbd><kbd>↓</kbd> Change release&nbsp;&nbsp; <kbd>Esc</kbd> Back</span>
      </footer>
    </main>
  )
}
