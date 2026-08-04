import { useEffect, useRef, useState } from 'react'
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
            Password required
          </div>
          <p className="modal__text">
            {connect.serverName ?? 'This server'} is protected. Enter the server password to join.
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
            placeholder="Server password"
          />
          <div className="modal__actions">
            <button type="button" className="modal__btn" onClick={onDismiss}>
              Cancel
            </button>
            <button type="submit" className="modal__btn modal__btn--primary">
              Connect
            </button>
          </div>
        </form>
      )}

      {connect.phase === 'connecting' && (
        <div className="modal">
          <div className="modal__title">
            <IconPlay size={14} />
            Connecting…
          </div>
          <div className="connecting__spinner" />
          <p className="modal__text" style={{ textAlign: 'center' }}>
            Joining {connect.serverName ?? `${connect.address?.ip}:${connect.address?.port}`}
          </p>
          <div className="modal__actions">
            <button type="button" className="modal__btn" onClick={onDismiss}>
              Cancel
            </button>
          </div>
        </div>
      )}

      {connect.phase === 'failed' && (
        <div className="modal">
          <div className="modal__title">
            <IconClose size={16} />
            Connection failed
          </div>
          <p className="modal__error">{connect.error?.message ?? 'Unknown error.'}</p>
          <div className="modal__actions">
            <button type="button" className="modal__btn" onClick={onDismiss}>
              Close
            </button>
            <button type="button" className="modal__btn modal__btn--primary" onClick={onRetry}>
              Retry
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
  return (
    <div className="overlay" onMouseDown={(e) => e.target === e.currentTarget && onClose()}>
      <div className="modal modal--players">
        <div className="modal__title">
          <IconAt size={16} />
          Players — {server.players}
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
            Close
          </button>
        </div>
      </div>
    </div>
  )
}
