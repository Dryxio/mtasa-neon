import { createContext, useCallback, useContext, useMemo, type ReactNode } from 'react'

export const DEFAULT_TRANSLATIONS = {
  'route.loadingServerBrowser': 'Loading server browser',
  'aria.mainMenu': 'MTA Neon main menu',
  'aria.mainNavigation': 'Main navigation',
  'aria.chooseLanguage': 'Choose language',
  'aria.serverList': 'Server list',
  'main.discordConnecting': 'Connecting Discord',
  'main.discordConnected': 'Discord connected',
  'main.discordLink': 'Link your Discord',
  'main.discordSignOut': 'Sign out',
  'main.featuredServer': 'Neon featured',
  'main.playFeatured': 'Play now',
  'main.linkDiscordToPlay': 'Link Discord to play',
  'main.resumeGame': 'Resume game',
  'main.resumeCaption': 'Return to the streets.',
  'main.settingsCaption': 'Video, audio, controls and account preferences.',
  'main.disconnectCaption': 'Leave the current server and return to the main menu.',
  'main.quitInGameCaption': 'Leave the server and return to the desktop.',
  'main.browseServers': 'Browse servers',
  'main.browseCaption': 'Find a world and join the streets of San Andreas.',
  'main.quickConnect': 'Quick connect',
  'main.quickConnectCaption': 'Reconnect instantly or enter a server address.',
  'main.mapEditor': 'Map editor',
  'main.mapEditorCaption': 'Build your own corner of San Andreas.',
  'main.aboutCaption': 'Credits, contributors and information about MTA Neon.',
  'main.quitGame': 'Quit game',
  'main.quitCaption': 'Return to the desktop.',
  'main.identity': 'Neon Identity',
  'main.inGame': 'MTA Neon — In game',
  'main.preview': 'v1.6 — Neon Preview',
  'common.settings': 'Settings',
  'common.disconnect': 'Disconnect',
  'common.about': 'About',
  'common.cancel': 'Cancel',
  'common.connect': 'Connect',
  'common.close': 'Close',
  'common.retry': 'Retry',
  'common.resume': 'Resume',
  'common.select': 'Select',
  'common.confirm': 'Confirm',
  'common.back': 'Back',
  'common.refresh': 'Refresh',
  'common.filters': 'Filters',
  'common.players': 'Players',
  'common.ping': 'Ping',
  'common.mode': 'Mode',
  'common.navigate': 'Navigate',
  'source.neon': 'Neon servers',
  'source.local': 'Local',
  'source.favourites': 'Favourites',
  'source.recent': 'Recent',
  'browser.title': 'Choose your server',
  'browser.destinationsCount': '{count} destinations',
  'browser.playersOnlineCount': '{count} players online',
  'browser.backToMain': 'Back to main menu',
  'browser.searchPlaceholder': 'Find a Neon server',
  'browser.heading.destinations': 'Neon destinations',
  'browser.filter.hideFull': 'Hide full servers',
  'browser.filter.hideEmpty': 'Hide empty servers',
  'browser.filter.hideLocked': 'Hide locked servers',
  'browser.filter.hideIncompatible': 'Hide other versions',
  'browser.filter.hideOffline': 'Hide offline servers',
  'browser.empty': 'No servers match your search.',
  'browser.emptyHint': 'Try clearing filters or refreshing the list.',
  'server.passwordProtected': 'Password protected',
  'server.removeFavourite': 'Remove from favourites',
  'server.addFavourite': 'Add to favourites',
  'server.playersUnverified': 'Player count not verified',
  'server.offline': 'Offline',
  'server.featured': 'Neon featured',
  'details.selectServer': 'Select a server to see',
  'details.selectServerHint': 'its details here.',
  'details.selectedDestination': 'Selected destination',
  'details.copyAddress': 'Copy {address}',
  'details.regionsLanguages': 'Regions & languages',
  'details.playersOnline': '{count} players online',
  'details.viewAllPlayers': 'View all {count} players',
  'details.noPlayersOnline': 'No players online',
  'details.backToServerDetails': 'Back to server details',
  'details.ready': 'Selected server — ready to join',
  'details.joinServer': 'Join server',
  'modal.passwordRequired': 'Password required',
  'modal.connectionKicker': 'Neon network // connection',
  'modal.restrictedServer': 'Restricted server',
  'modal.thisServer': 'This server',
  'modal.protectedServer': '{server} is protected. Enter the server password to join.',
  'modal.serverPassword': 'Server password',
  'modal.connecting': 'Connecting…',
  'modal.joining': 'Joining {server}',
  'modal.enteringSanAndreas': 'Entering San Andreas',
  'modal.contactingServer': 'Contacting the server and checking its version…',
  'modal.authorizingIdentity': 'Neon Identity',
  'modal.authorizingHint': 'Authorizing this server with your linked Discord identity…',
  'modal.connectionAccepted': 'Welcome to the streets',
  'modal.enteringGame': 'Connection accepted. Preparing the game…',
  'modal.connectionFailed': 'Connection failed',
  'modal.unknownError': 'Unknown error.',
  'modal.serverFull': 'Server is full',
  'modal.serverFullHint': 'No free slot is available. Try again in a moment.',
  'modal.connectionTimedOut': 'No answer from the streets',
  'modal.connectionTimedOutHint': 'The server did not answer in time. Check your connection and try again.',
  'modal.passwordRejected': 'Wrong password',
  'modal.passwordRejectedHint': 'Enter the server password again.',
  'modal.identityRequired': 'Neon Identity required',
  'modal.identityRequiredHint': 'Return to the main menu and link Discord before joining this server.',
  'modal.identityFailed': 'Identity check failed',
  'modal.identityFailedHint': 'Neon could not authorize this connection. Try again in a moment.',
  'modal.versionMismatch': 'Different game version',
  'modal.versionMismatchHint': 'This server requires another MTA version.',
  'modal.connectionDenied': 'Access denied',
  'modal.connectionDeniedHint': 'This server refused the connection.',
  'modal.serverError': 'Server response error',
  'modal.serverErrorHint': 'The server returned data this client cannot use.',
  'modal.playerNameInvalid': 'Player name invalid',
  'modal.playerNameInvalidHint': 'Change your nickname in Settings before connecting.',
  'modal.connectionLost': 'Connection lost',
  'modal.connectionLostHint': 'The server closed the connection. You can try again.',
  'status.registeredServers': '{count} registered servers',
  'status.playersOnline': '{count} players online.',
  'status.scanning': 'Scanning {scanned} / {total}…',
  'status.joinServer': 'Join server',
} as const

export type TranslationKey = keyof typeof DEFAULT_TRANSLATIONS
export type TranslationMap = Partial<Record<TranslationKey, string>>
type TranslationValues = Record<string, string | number>

interface I18nContextValue {
  t: (key: TranslationKey, values?: TranslationValues) => string
  formatNumber: (value: number) => string
}

const defaultValue: I18nContextValue = {
  t: (key, values) => interpolate(DEFAULT_TRANSLATIONS[key], values),
  formatNumber: (value) => String(value),
}

const I18nContext = createContext<I18nContextValue>(defaultValue)

export function normalizeTranslations(translations?: Record<string, string>): TranslationMap {
  if (!translations) return {}
  const normalized: TranslationMap = {}
  for (const key of Object.keys(DEFAULT_TRANSLATIONS) as TranslationKey[]) {
    const value = translations[key]
    if (typeof value === 'string' && value !== '') normalized[key] = value
  }
  return normalized
}

export function I18nProvider({
  locale,
  translations,
  children,
}: {
  locale: string
  translations: TranslationMap
  children: ReactNode
}) {
  const t = useCallback(
    (key: TranslationKey, values?: TranslationValues) =>
      interpolate(translations[key] ?? DEFAULT_TRANSLATIONS[key], values),
    [translations],
  )
  const formatNumber = useMemo(() => {
    try {
      const formatter = new Intl.NumberFormat(locale.replace('_', '-'))
      return (value: number) => formatter.format(value)
    } catch {
      return (value: number) => String(value)
    }
  }, [locale])

  return <I18nContext.Provider value={{ t, formatNumber }}>{children}</I18nContext.Provider>
}

export function useI18n(): I18nContextValue {
  return useContext(I18nContext)
}

function interpolate(template: string, values?: TranslationValues): string {
  if (!values) return template
  return template.replace(/\{([a-zA-Z0-9_]+)\}/g, (placeholder, name: string) =>
    Object.hasOwn(values, name) ? String(values[name]) : placeholder,
  )
}
