import { useEffect, useState } from 'react'
import { chooseLaunchLoadscreen } from './loadscreen'
import { playUiSound } from './uiSound'
import './Updates.css'

interface UpdateSection {
  label: string
  tone: 'players' | 'creators' | 'servers'
  items: readonly string[]
}

interface UpdateEntry {
  version: string
  date: string
  title: string
  summary: string
  current?: boolean
  sections: readonly UpdateSection[]
}

const PLACEHOLDER_UPDATES: readonly UpdateEntry[] = [
  {
    version: '2026.08.21.184',
    date: 'August 21, 2026',
    title: 'The streets enter a new dimension',
    summary:
      'An update focused on smoother play, more ambitious worlds and dependable long sessions.',
    current: true,
    sections: [
      {
        label: 'For players',
        tone: 'players',
        items: [
          'A direct radio station selector designed to stay readable while driving.',
          'The radar now preserves native markers and custom destinations more reliably.',
          'Menu and server browser transitions feel faster and more consistent.',
        ],
      },
      {
        label: 'For creators',
        tone: 'creators',
        items: [
          'New managed rope tools for physical interactions and cinematic scenes.',
          'More control over native worlds, their objects and visual effects.',
        ],
      },
      {
        label: 'Servers & stability',
        tone: 'servers',
        items: [
          'Lower rendering cost in dense lists and interface-heavy screens.',
          'Reconnect fixes and better continuity between Neon clients and servers.',
        ],
      },
    ],
  },
  {
    version: '2026.08.09.179',
    date: 'August 9, 2026',
    title: 'A stronger foundation for Neon',
    summary: 'This release strengthens public distribution and the network foundations unique to Neon.',
    sections: [
      {
        label: 'Highlights',
        tone: 'players',
        items: [
          'Simplified Windows installation from a signed public release.',
          'Client and server builds ship together to preserve compatibility with Neon features.',
          'Clearer version information throughout the launcher.',
        ],
      },
    ],
  },
  {
    version: '2026.08.08.177',
    date: 'August 8, 2026',
    title: 'Neon servers are easier to discover',
    summary: 'The browser gives communities published on the Neon network a clearer identity.',
    sections: [
      {
        label: 'Discovery',
        tone: 'players',
        items: [
          'Richer server profiles with descriptions, languages and community links.',
          'Faster access to favourites and recent destinations.',
        ],
      },
      {
        label: 'Hosting',
        tone: 'servers',
        items: ['Secure publication of public servers through the Neon registry.'],
      },
    ],
  },
  {
    version: '2026.08.07.174',
    date: 'August 7, 2026',
    title: 'Neon Identity across the whole experience',
    summary: 'A linked Discord identity can now follow the player between Neon experiences.',
    sections: [
      {
        label: 'Account',
        tone: 'players',
        items: [
          'Discord linking is available directly from the main menu.',
          'Session status stays visible without interrupting navigation.',
        ],
      },
    ],
  },
  {
    version: '2026.07',
    date: 'July 2026',
    title: 'Extended worlds and native tools',
    summary: 'A series of experiments opens the door to more ambitious maps and scenarios.',
    sections: [
      {
        label: 'Creation',
        tone: 'creators',
        items: [
          'Controlled loading of additional native worlds.',
          'New tools for missions, traffic and scripted scenes.',
          'Dedicated test harnesses for validating content before publication.',
        ],
      },
    ],
  },
]

interface UpdatesProps {
  onClose: () => void
}

export function Updates({ onClose }: UpdatesProps) {
  const [selectedIndex, setSelectedIndex] = useState(0)
  const [loadscreen] = useState(chooseLaunchLoadscreen)
  const selected = PLACEHOLDER_UPDATES[selectedIndex] ?? PLACEHOLDER_UPDATES[0]!

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') {
        event.preventDefault()
        playUiSound('back')
        onClose()
      } else if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
        event.preventDefault()
        const direction = event.key === 'ArrowDown' ? 1 : -1
        setSelectedIndex((current) => (current + direction + PLACEHOLDER_UPDATES.length) % PLACEHOLDER_UPDATES.length)
        playUiSound('highlight')
      } else if (event.key === 'Home') {
        event.preventDefault()
        setSelectedIndex(0)
      } else if (event.key === 'End') {
        event.preventDefault()
        setSelectedIndex(PLACEHOLDER_UPDATES.length - 1)
      }
    }

    window.addEventListener('keydown', onKeyDown)
    return () => window.removeEventListener('keydown', onKeyDown)
  }, [onClose])

  return (
    <main className="updates-shell">
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
            <small>{PLACEHOLDER_UPDATES.length} releases</small>
          </div>
          <div className="updates-history__list">
            {PLACEHOLDER_UPDATES.map((entry, index) => {
              const active = index === selectedIndex
              return (
                <button
                  type="button"
                  key={entry.version}
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
                    <strong>Neon {entry.version}</strong>
                    <span>{entry.title}</span>
                    <time>{entry.date}</time>
                  </span>
                  {entry.current && <span className="updates-history__badge">Current</span>}
                </button>
              )
            })}
          </div>
        </aside>

        <article className="updates-detail" key={selected.version}>
          <header className="updates-detail__header">
            <div className="updates-detail__meta">
              <span>Version {selected.version}</span>
              <time>{selected.date}</time>
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
            <span>Preview content</span>
            <span>Final notes will be published with each release.</span>
          </footer>
        </article>
      </div>

      <footer className="updates-controls">
        <span>Visual prototype · placeholder content</span>
        <span><kbd>↑</kbd><kbd>↓</kbd> Change release&nbsp;&nbsp; <kbd>Esc</kbd> Back</span>
      </footer>
    </main>
  )
}
