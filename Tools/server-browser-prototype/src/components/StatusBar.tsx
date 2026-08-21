import { useI18n } from '../i18n'
import { IconArrowDown, IconArrowUp, IconEnter } from './Icons'

interface StatusBarProps {
  progress: { scanned: number; total: number } | null
  notice: string | null
}

export function StatusBar({ progress, notice }: StatusBarProps) {
  const { formatNumber, t } = useI18n()
  return (
    <footer className="statusbar">
      <span>
        {progress && (
          <span className="statusbar__progress">
            {t('status.scanning', {
              scanned: formatNumber(progress.scanned),
              total: formatNumber(progress.total),
            })}
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
          {t('common.navigate')}
        </span>
        <span className="statusbar__hint">
          <span className="statusbar__key">
            <IconEnter size={13} />
          </span>
          {t('status.joinServer')}
        </span>
      </div>
    </footer>
  )
}
