import { useI18n } from '../i18n'
import type { NetworkStats } from '../store'
import { IconArrowLeft } from './Icons'

export function Header({ stats, onClose }: { stats: NetworkStats; onClose: () => void }) {
  const { formatNumber, t } = useI18n()
  return (
    <header className="header">
      <div className="header__identity">
        <span className="header__kicker">MTA Neon Network</span>
        <h1 className="header__title">{t('browser.title')}</h1>
      </div>
      <div className="header__side">
        <div className="header__stats">
          <span>{t('browser.destinationsCount', { count: formatNumber(stats.serverCount) })}</span>
          <strong>{t('browser.playersOnlineCount', { count: formatNumber(stats.playersOnline) })}</strong>
        </div>
        <button type="button" className="header__close" title={t('browser.backToMain')} onClick={onClose}>
          <IconArrowLeft size={18} />
          <span>{t('browser.backToMain')}</span>
        </button>
      </div>
    </header>
  )
}
