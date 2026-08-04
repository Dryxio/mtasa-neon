import type { BrowserBackend } from './BrowserBackend'
import { CefBackend, isCefEnvironment } from './cef/CefBackend'
import { installCefDevStub } from './cef/devStub'
import { MockBackend } from './mock/MockBackend'

/**
 * Point d'injection du backend : seul endroit du code qui connaît les
 * implémentations concrètes.
 *
 * - Dans le client MTA (CEF), `window.mta` existe → CefBackend (bridge natif).
 * - Dans un navigateur avec `?cefsim` → CefBackend sur un bridge simulé,
 *   pour tester le protocole sans le client.
 * - Sinon → MockBackend (données simulées, développement UI).
 */

if (typeof window !== 'undefined' && new URLSearchParams(window.location.search).has('cefsim')) {
  installCefDevStub()
}

export const backend: BrowserBackend = isCefEnvironment() ? new CefBackend() : new MockBackend()

export type { BackendEvent, BrowserBackend, ConnectError } from './BrowserBackend'
