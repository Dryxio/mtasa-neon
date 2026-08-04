import { formatThousands } from '../types'
import type { NetworkStats } from '../store'
import { IconArrowDown, IconArrowUp, IconEnter } from './Icons'

interface StatusBarProps {
  stats: NetworkStats
  progress: { scanned: number; total: number } | null
  notice: string | null
}

export function StatusBar({ stats, progress, notice }: StatusBarProps) {
  return (
    <footer className="statusbar">
      <span>
        Neon Network — {formatThousands(stats.serverCount)} registered servers ·{' '}
        {formatThousands(stats.playersOnline)} players online.
        {progress && (
          <span className="statusbar__progress">
            {'  '}Scanning {progress.scanned} / {progress.total}…
          </span>
        )}
        {notice && <span className="statusbar__notice"> {notice}</span>}
      </span>
      <div className="statusbar__right">
        <span className="statusbar__hint">
          <span className="statusbar__key">
            <IconArrowUp size={13} />
          </span>
          <span className="statusbar__key">
            <IconArrowDown size={13} />
          </span>
          Navigate
        </span>
        <span className="statusbar__hint">
          <span className="statusbar__key">
            <IconEnter size={13} />
          </span>
          Join Server
        </span>
      </div>
    </footer>
  )
}
