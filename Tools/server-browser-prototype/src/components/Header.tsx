import { formatThousands } from '../types'
import type { NetworkStats } from '../store'
import { IconClose } from './Icons'

export function Header({ stats, onClose }: { stats: NetworkStats; onClose: () => void }) {
  return (
    <header className="header">
      <div className="header__identity">
        <span className="header__kicker">MTA Neon Network</span>
        <h1 className="header__title">Choose your server</h1>
      </div>
      <div className="header__side">
        <div className="header__stats">
          <span>{stats.serverCount} destinations</span>
          <strong>{formatThousands(stats.playersOnline)} players online</strong>
        </div>
        <button type="button" className="header__close" title="Back to main menu" onClick={onClose}>
          <IconClose size={17} />
          <span>Back</span>
        </button>
      </div>
    </header>
  )
}
