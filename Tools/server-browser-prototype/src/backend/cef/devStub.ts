/**
 * Simulateur du bridge natif pour le développement dans un navigateur.
 *
 * Activé avec `?cefsim` dans l'URL : installe un faux `window.mta` AVANT la
 * sélection du backend, si bien que l'application utilise le vrai CefBackend
 * et parle le vrai protocole (sb:* / __neonSB.emit). Sert à la fois de banc
 * de test du CefBackend et de documentation exécutable du contrat C++.
 *
 * Comportement simulé (miroir de CServerBrowserWeb.cpp) :
 *  - sb:ready       → init + favourites + list-reset + vagues de serveurs
 *  - sb:setSource   → list-reset + snapshot de la source
 *  - sb:refresh     → re-scan avec progression
 *  - sb:connect     → mot de passe ou séquence started/progress/succeeded
 *  - sb:favourite   → favourites (clés mises à jour)
 *  - sb:copyServerLink → copie simulée + accusé de réception
 *  - sb:close       → console.log (le natif masque la vue)
 */

interface StubServer {
  ip: string
  port: number
  name: string
  gameMode: string
  map: string
  version: string
  players: number
  maxPlayers: number
  ping: number
  passworded: boolean
}

const STUB_SERVERS: StubServer[] = [
  { ip: '141.94.26.7', port: 22003, name: 'Southland Roleplay', gameMode: 'Roleplay', map: 'Los Santos', version: '1.6', players: 262, maxPlayers: 500, ping: 22, passworded: true },
  { ip: '146.59.208.11', port: 22003, name: 'Proxima-RP [RU] | Role Play', gameMode: 'Role Play', map: 'San Andreas', version: '1.6', players: 317, maxPlayers: 850, ping: 21, passworded: false },
  { ip: '178.32.220.100', port: 22003, name: 'FFS Gaming', gameMode: 'Multigamemode', map: 'Mixed', version: '1.6', players: 165, maxPlayers: 999, ping: 56, passworded: false },
  { ip: '144.76.68.79', port: 22003, name: '| Vio | German Reallife |', gameMode: 'Reallife', map: 'San Andreas', version: '1.5.9', players: 76, maxPlayers: 200, ping: 31, passworded: false },
]

for (let i = 0; i < 40; i++) {
  STUB_SERVERS.push({
    ip: `88.10.${Math.floor(i / 250)}.${(i % 250) + 1}`,
    port: 22003,
    name: `Stub Server #${i + 1}`,
    gameMode: i % 3 === 0 ? 'Race' : 'Freeroam',
    map: 'San Andreas',
    version: '1.6',
    players: (i * 7) % 64,
    maxPlayers: 64,
    ping: 20 + ((i * 13) % 180),
    passworded: i % 11 === 0,
  })
}

export function installCefDevStub(): void {
  const favourites = new Set<string>(['178.32.220.100:22003'])
  const inGame = new URLSearchParams(window.location.search).has('ingame')
  let scanTimer: ReturnType<typeof setInterval> | null = null
  let connectTimers: ReturnType<typeof setTimeout>[] = []

  const emit = (events: unknown[]) => {
    // Différé pour imiter ExecuteJavascript (jamais synchrone avec l'appel)
    setTimeout(() => window.__neonSB?.emit(events as never), 0)
  }

  const emitMenu = (events: unknown[]) => {
    setTimeout(() => window.__neonMenu?.emit(events as never), 0)
  }

  const serialize = (s: StubServer, scanned: boolean) => ({
    id: `${s.ip}:${s.port}`,
    ip: s.ip,
    port: s.port,
    httpPort: 22005,
    name: s.name,
    gameMode: s.gameMode,
    map: s.map,
    version: s.version,
    players: s.players,
    maxPlayers: s.maxPlayers,
    ping: scanned ? s.ping : -1,
    passworded: s.passworded,
    serials: false,
    verified: true,
    state: scanned ? 'online' : 'queued',
    favourite: favourites.has(`${s.ip}:${s.port}`),
    playerList: scanned ? ['romancenoire', 'nando', 'kayzen'].slice(0, s.players % 4) : [],
  })

  const startScan = (source: string) => {
    if (scanTimer) clearInterval(scanTimer)
    let cursor = 0
    emit([{ type: 'list-reset', source }])
    emit(STUB_SERVERS.map((s) => ({ type: 'server', source, server: serialize(s, false) })))
    scanTimer = setInterval(() => {
      const wave = STUB_SERVERS.slice(cursor, cursor + 8)
      cursor += 8
      const events: unknown[] = wave.map((s) => ({ type: 'server', source, server: serialize(s, true) }))
      events.push({ type: 'progress', source, scanned: Math.min(cursor, STUB_SERVERS.length), total: STUB_SERVERS.length })
      if (cursor >= STUB_SERVERS.length) {
        events.push({ type: 'refresh-finished', source })
        if (scanTimer) clearInterval(scanTimer)
        scanTimer = null
      }
      emit(events)
    }, 120)
  }

  window.mta = {
    triggerEvent(name: string, ...args: string[]) {
      console.log('[cefsim] triggerEvent', name, args)
      switch (name) {
        case 'menu:ready':
          emitMenu([{
            type: 'init',
            inGame,
            locale: 'en_US',
            languages: [
              { locale: 'en_US', name: 'English' },
              { locale: 'fr_FR', name: 'Français' },
            ],
            identity: {
              authenticated: true,
              signingIn: false,
              displayName: 'Dryxio',
              status: 'Neon ID connected',
            },
          }])
          break
        case 'sb:ready':
          emit([
            { type: 'init', version: '1.6', source: 'internet' },
            { type: 'favourites', keys: [...favourites] },
          ])
          startScan('internet')
          break
        case 'sb:setSource': {
          const source = args[0] ?? 'internet'
          if (source === 'internet') startScan(source)
          else if (source === 'favourites') {
            emit([{ type: 'list-reset', source }])
            emit(
              STUB_SERVERS.filter((s) => favourites.has(`${s.ip}:${s.port}`)).map((s) => ({
                type: 'server',
                source,
                server: serialize(s, true),
              })),
            )
          } else {
            emit([{ type: 'list-reset', source }])
          }
          break
        }
        case 'sb:refresh':
          startScan('internet')
          break
        case 'sb:connect': {
          const [host, port, password] = args
          const server = STUB_SERVERS.find((s) => s.ip === host)
          connectTimers.forEach(clearTimeout)
          connectTimers = []
          if (server?.passworded && !password) {
            emit([{ type: 'connect-password-required', host, port: Number(port), name: server.name }])
          } else {
            emit([{ type: 'connect-started', host, port: Number(port), name: server?.name }])
            if (password && password !== 'neon') {
              connectTimers.push(setTimeout(() => emit([{ type: 'connect-failed', code: 'bad-password', message: 'Incorrect server password.' }]), 750))
            } else {
              connectTimers.push(setTimeout(() => emit([{ type: 'connect-progress', stage: 'authorizing', message: 'Authorizing this server with Neon Identity...' }]), 650))
              connectTimers.push(setTimeout(() => emit([{ type: 'connect-progress', stage: 'joining', message: 'Connection accepted. Entering the game...' }]), 1350))
              connectTimers.push(setTimeout(() => emit([{ type: 'connect-succeeded' }]), 2050))
            }
          }
          break
        }
        case 'sb:cancelConnect':
          connectTimers.forEach(clearTimeout)
          connectTimers = []
          break
        case 'sb:favourite': {
          const [host, port, on] = args
          const key = `${host}:${port}`
          if (on === '1') favourites.add(key)
          else favourites.delete(key)
          emit([{ type: 'favourites', keys: [...favourites] }])
          break
        }
        case 'sb:openExternal':
          console.log('[cefsim] openExternal', args[0])
          break
        case 'sb:copyServerLink': {
          const [requestId, host, port] = args
          if (!navigator.clipboard) {
            emit([{ type: 'clipboard-result', requestId, success: false }])
            break
          }
          void navigator.clipboard
            .writeText(`mtaneon://${host}:${port}`)
            .then(() => emit([{ type: 'clipboard-result', requestId, success: true }]))
            .catch(() => emit([{ type: 'clipboard-result', requestId, success: false }]))
          break
        }
        case 'sb:close':
          console.log('[cefsim] close (le natif masque la vue et rend le menu)')
          break
      }
    },
  }
}
