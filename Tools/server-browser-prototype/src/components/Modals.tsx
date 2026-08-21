import { useEffect, useRef, useState } from 'react'
import { useI18n } from '../i18n'
import type { NativeMessageDialog } from '../menuBridge'
import type { ConnectFlow } from '../store'
import { playUiSound } from '../uiSound'
import { IconClose, IconLock } from './Icons'

interface ConnectModalsProps {
  connect: ConnectFlow
  onSubmitPassword: (password: string) => void
  onRetry: () => void
  onDismiss: () => void
  onLinkIdentity: () => void
}

/** Flux de connexion intégré au shell, inspiré des écrans frontend de GTA SA. */
export function ConnectModals({ connect, onSubmitPassword, onRetry, onDismiss, onLinkIdentity }: ConnectModalsProps) {
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

  const destination = connect.serverName ?? `${connect.address?.ip}:${connect.address?.port}`
  const stage = connect.stage ?? 'contacting'
  const stageTitle =
    stage === 'authorizing'
      ? t('modal.authorizingIdentity')
      : stage === 'joining'
        ? t('modal.enteringSanAndreas')
        : t('modal.connecting')
  const stageMessage =
    connect.statusMessage ??
    (stage === 'authorizing'
      ? t('modal.authorizingHint')
      : stage === 'joining'
        ? t('modal.enteringGame')
        : t('modal.contactingServer'))
  const failure = getFailurePresentation(connect.error?.code, t)

  return (
    <div className="overlay overlay--connection">
      {connect.phase === 'password' && (
        <form
          className="modal modal--connection"
          onSubmit={(e) => {
            e.preventDefault()
            onSubmitPassword(password)
          }}
        >
          <div className="modal__title">{t('modal.restrictedServer')}</div>
          <div className="modal__destination">
            <IconLock size={14} />
            <span>{destination}</span>
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
        <div className="modal modal--connection" role="dialog" aria-modal="true" aria-live="polite">
          <div className="modal__title">{stageTitle}</div>
          <div className="modal__destination">
            <span className="modal__marker">▶</span>
            <span>{destination}</span>
          </div>
          <p className="modal__text modal__text--status">{stageMessage}</p>
          <div className={`connecting__bar connecting__bar--${stage}`} aria-hidden="true">
            <span />
          </div>
          <div className="modal__actions">
            <button type="button" className="modal__btn" onClick={onDismiss}>
              {t('common.cancel')}
            </button>
          </div>
        </div>
      )}

      {connect.phase === 'failed' && (
        <div className="modal modal--connection modal--failed" role="alertdialog" aria-modal="true">
          <div className="modal__title">{failure.title}</div>
          <div className="modal__destination">
            <IconClose size={14} />
            <span>{destination}</span>
          </div>
          <p className="modal__error">{connect.error?.message || failure.hint}</p>
          {connect.error?.message && <p className="modal__text modal__text--hint">{failure.hint}</p>}
          <div className="modal__actions">
            <button type="button" className="modal__btn" onClick={onDismiss}>
              {t('common.close')}
            </button>
            {failure.retry && (
              <button type="button" className="modal__btn modal__btn--primary" onClick={onRetry}>
                {t('common.retry')}
              </button>
            )}
            {connect.error?.code === 'identity-required' && (
              <button
                type="button"
                className="modal__btn modal__btn--primary"
                onClick={() => {
                  playUiSound('select')
                  onLinkIdentity()
                }}
              >
                {t('main.discordLink')}
              </button>
            )}
          </div>
        </div>
      )}
    </div>
  )
}

interface NativeMessageModalProps {
  dialog: NativeMessageDialog | null
  onRespond: (id: number, action: string) => void
}

/** Presents translated core lifecycle errors without exposing the CEGUI layer below CEF. */
export function NativeMessageModal({ dialog, onRespond }: NativeMessageModalProps) {
  const buttonRefs = useRef<Array<HTMLButtonElement | null>>([])
  const modalRef = useRef<HTMLElement>(null)

  useEffect(() => {
    if (!dialog) return
    if (dialog.severity === 'error' || dialog.severity === 'warning') playUiSound('error')
    const primaryIndex = dialog.actions.findIndex((action) => action.variant === 'primary')
    ;(buttonRefs.current[primaryIndex >= 0 ? primaryIndex : 0] ?? modalRef.current)?.focus()
  }, [dialog])

  useEffect(() => {
    if (!dialog) return
    const onKeyDown = (event: KeyboardEvent) => {
      // Match CEGUI modal ownership: navigation, shortcuts and text must not
      // reach the Settings or Server Browser route underneath the overlay.
      event.preventDefault()
      event.stopImmediatePropagation()
      if (event.key === 'Tab') {
        const buttons = buttonRefs.current.filter((button): button is HTMLButtonElement => button !== null)
        if (buttons.length === 0) {
          modalRef.current?.focus()
          return
        }
        const current = buttons.indexOf(document.activeElement as HTMLButtonElement)
        const direction = event.shiftKey ? -1 : 1
        buttons[(current + direction + buttons.length) % buttons.length]?.focus()
        return
      }
      if (event.key === 'Escape' && dialog.escapeAction) {
        onRespond(dialog.id, dialog.escapeAction)
        return
      }
      if (event.key !== 'Enter' && event.key !== ' ') return
      const focusedIndex = buttonRefs.current.findIndex((button) => button === document.activeElement)
      const action = dialog.actions[focusedIndex] ?? dialog.actions.find((candidate) => candidate.variant === 'primary') ?? dialog.actions[0]
      if (action) onRespond(dialog.id, action.id)
    }
    window.addEventListener('keydown', onKeyDown, true)
    return () => window.removeEventListener('keydown', onKeyDown, true)
  }, [dialog, onRespond])

  if (!dialog) return null

  return (
    <div className="overlay overlay--system">
      <section
        ref={modalRef}
        className={`modal modal--system modal--${dialog.severity}`}
        role={dialog.severity === 'error' || dialog.severity === 'warning' ? 'alertdialog' : 'dialog'}
        aria-modal="true"
        aria-labelledby="native-dialog-title"
        aria-describedby="native-dialog-message"
        tabIndex={-1}
      >
        <h2 id="native-dialog-title" className="modal__title">
          {(dialog.severity === 'error' || dialog.severity === 'warning') && <IconClose size={16} />}
          {dialog.title}
        </h2>
        <p id="native-dialog-message" className={dialog.severity === 'error' ? 'modal__error modal__message' : 'modal__text modal__message'}>
          {dialog.message}
        </p>
        {dialog.actions.length > 0 && (
          <div className="modal__actions">
            {dialog.actions.map((action, index) => (
              <button
                key={action.id}
                ref={(element) => { buttonRefs.current[index] = element }}
                type="button"
                className={`modal__btn${action.variant === 'primary' ? ' modal__btn--primary' : ''}`}
                onClick={() => onRespond(dialog.id, action.id)}
              >
                {action.label}
              </button>
            ))}
          </div>
        )}
      </section>
    </div>
  )
}

type Translate = ReturnType<typeof useI18n>['t']

function getFailurePresentation(code: NonNullable<ConnectFlow['error']>['code'] | undefined, t: Translate) {
  switch (code) {
    case 'server-full':
      return { title: t('modal.serverFull'), hint: t('modal.serverFullHint'), retry: true }
    case 'timeout':
      return { title: t('modal.connectionTimedOut'), hint: t('modal.connectionTimedOutHint'), retry: true }
    case 'bad-password':
      return { title: t('modal.passwordRejected'), hint: t('modal.passwordRejectedHint'), retry: true }
    case 'identity-required':
      return { title: t('modal.identityRequired'), hint: t('modal.identityRequiredHint'), retry: false }
    case 'identity-failed':
      return { title: t('modal.identityFailed'), hint: t('modal.identityFailedHint'), retry: true }
    case 'version-mismatch':
      return { title: t('modal.versionMismatch'), hint: t('modal.versionMismatchHint'), retry: false }
    case 'refused':
      return { title: t('modal.connectionDenied'), hint: t('modal.connectionDeniedHint'), retry: true }
    case 'banned':
      return { title: t('modal.connectionDenied'), hint: t('modal.connectionDeniedHint'), retry: false }
    case 'bad-response':
    case 'mod-unavailable':
      return { title: t('modal.serverError'), hint: t('modal.serverErrorHint'), retry: false }
    case 'invalid-nick':
      return { title: t('modal.playerNameInvalid'), hint: t('modal.playerNameInvalidHint'), retry: false }
    case 'disconnected':
      return { title: t('modal.connectionLost'), hint: t('modal.connectionLostHint'), retry: true }
    default:
      return { title: t('modal.connectionFailed'), hint: t('modal.unknownError'), retry: true }
  }
}
