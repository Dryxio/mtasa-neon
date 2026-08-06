import { useEffect, useRef, useState } from 'react'
import { useI18n } from '../i18n'
import type { ConnectFlow } from '../store'
import type { ServerItem } from '../types'
import { playUiSound } from '../uiSound'
import { IconAt, IconClose, IconLock, IconPlay } from './Icons'

interface ConnectModalsProps {
  connect: ConnectFlow
  onSubmitPassword: (password: string) => void
  onRetry: () => void
  onDismiss: () => void
}

/** Modales du flux de connexion : mot de passe, connexion en cours, échec. */
export function ConnectModals({ connect, onSubmitPassword, onRetry, onDismiss }: ConnectModalsProps) {
  const [password, setPassword] = useState('')
  const inputRef = useRef<HTMLInputElement>(null)
  const { t } = useI18n()

  useEffect(() => {
    if (connect.phase === 'password') {
      setPassword('')
      inputRef.current?.focus()
    }
  }, [connect.phase])

  useEffect(() => {
    if (connect.phase === 'failed' || connect.error?.code === 'bad-password') playUiSound('error')
  }, [connect.error?.code, connect.phase])

  if (connect.phase === 'idle') return null

  return (
    <div className="overlay" onMouseDown={(e) => e.target === e.currentTarget && onDismiss()}>
      {connect.phase === 'password' && (
        <form
          className="modal"
          onSubmit={(e) => {
            e.preventDefault()
            onSubmitPassword(password)
          }}
        >
          <div className="modal__title">
            <IconLock size={16} />
            {t('modal.passwordRequired')}
          </div>
          <p className="modal__text">
            {t('modal.protectedServer', { server: connect.serverName ?? t('modal.thisServer') })}
          </p>
          {connect.error?.code === 'bad-password' && (
            <p className="modal__error">{connect.error.message}</p>
          )}
          <input
            ref={inputRef}
            className="modal__input"
            type="password"
            value={password}
            onChange={(e) => setPassword(e.target.value)}
            placeholder={t('modal.serverPassword')}
          />
          <div className="modal__actions">
            <button type="button" className="modal__btn" onClick={onDismiss}>
              {t('common.cancel')}
            </button>
            <button type="submit" className="modal__btn modal__btn--primary">
              {t('common.connect')}
            </button>
          </div>
        </form>
      )}

      {connect.phase === 'connecting' && (
        <div className="modal">
          <div className="modal__title">
            <IconPlay size={14} />
            {t('modal.connecting')}
          </div>
          <div className="connecting__spinner" />
          <p className="modal__text" style={{ textAlign: 'center' }}>
            {t('modal.joining', {
              server: connect.serverName ?? `${connect.address?.ip}:${connect.address?.port}`,
            })}
          </p>
          <div className="modal__actions">
            <button type="button" className="modal__btn" onClick={onDismiss}>
              {t('common.cancel')}
            </button>
          </div>
        </div>
      )}

      {connect.phase === 'failed' && (
        <div className="modal">
          <div className="modal__title">
            <IconClose size={16} />
            {t('modal.connectionFailed')}
          </div>
          <p className="modal__error">{connect.error?.message ?? t('modal.unknownError')}</p>
          <div className="modal__actions">
            <button type="button" className="modal__btn" onClick={onDismiss}>
              {t('common.close')}
            </button>
            <button type="button" className="modal__btn modal__btn--primary" onClick={onRetry}>
              {t('common.retry')}
            </button>
          </div>
        </div>
      )}
    </div>
  )
}

interface PlayersModalProps {
  server: ServerItem
  onClose: () => void
}

export function PlayersModal({ server, onClose }: PlayersModalProps) {
  const { formatNumber, t } = useI18n()
  return (
    <div className="overlay" onMouseDown={(e) => e.target === e.currentTarget && onClose()}>
      <div className="modal modal--players">
        <div className="modal__title">
          <IconAt size={16} />
          {t('modal.playersCount', { count: formatNumber(server.players) })}
        </div>
        <div className="modal__players">
          {server.playerList.map((player, i) => (
            <span key={player.name + i} className="chip">
              @{player.name}
            </span>
          ))}
        </div>
        <div className="modal__actions">
          <button type="button" className="modal__btn" onClick={onClose}>
            {t('common.close')}
          </button>
        </div>
      </div>
    </div>
  )
}
