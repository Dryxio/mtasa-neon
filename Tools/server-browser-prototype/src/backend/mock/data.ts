import type { ServerItem, ServerLink, ServerPlayer } from '../../types'
import { serverKey } from '../../types'

/**
 * Petit réseau simulé de serveurs compatibles Neon. Le faible volume est
 * intentionnel : la maquette doit rester crédible au lancement du projet.
 */

const CLIENT_VERSION = '1.6'

let seed = 0x5eed
function rand(): number {
  // xorshift32 déterministe : mêmes serveurs à chaque lancement.
  seed ^= seed << 13
  seed ^= seed >>> 17
  seed ^= seed << 5
  return ((seed >>> 0) % 10_000) / 10_000
}

function pick<T>(items: readonly T[]): T {
  return items[Math.floor(rand() * items.length)]!
}

function makePlayers(count: number): ServerPlayer[] {
  const names = [
    'romancenoire', 'nando', 'kayzen', 'wolfie', 'estella', 'drift_kid', 'papy',
    'sunset', 'ghost', 'mirage', 'tacos', 'neonrider', 'lucky', 'baron', 'vega',
    'cactus', 'jinx', 'karma', 'echo', 'pixel', 'sable', 'orion', 'fennec',
    'dune', 'rhodes', 'clutch', 'mona', 'ferris', 'atlas', 'nyx',
  ]
  const players: ServerPlayer[] = []
  const shown = Math.min(count, 300)
  for (let i = 0; i < shown; i++) {
    const base = pick(names)
    const suffix = rand() > 0.25 ? String(Math.floor(rand() * 999)) : ''
    players.push({
      name: `${base}${suffix}`,
      score: Math.floor(rand() * 900),
      ping: 15 + Math.floor(rand() * 120),
    })
  }
  return players
}

interface HeroSpec {
  ip: string
  name: string
  tagline: string
  description: string
  gameMode: string
  map: string
  country: string
  countries?: string[]
  languages: string[]
  tags: string[]
  players: number
  maxPlayers: number
  ping: number
  passworded?: boolean
  featured?: boolean
  version?: string
  accent: string
  logoUrl?: string
  bannerUrl?: string
  links?: ServerLink[]
}

const HEROES: HeroSpec[] = [
  {
    ip: '213.32.90.138',
    name: 'MTA:SA Neon - BUST',
    tagline: 'Competitive 1v1 Pursuits - Ranked ON',
    description: 'Ranked 1v1 vehicle pursuits across the open world of San Andreas. Hunt your rival or escape until time runs out, then climb the leaderboard. Every street, shortcut and stunt can decide the match.',
    gameMode: 'BUST', map: 'San Andreas', country: 'FR',
    countries: ['BR', 'GB', 'FR', 'RU', 'TR', 'PL', 'ID', 'ES'],
    languages: ['Portuguese (Brazil)', 'English', 'French', 'Russian', 'Turkish', 'Polish', 'Indonesian', 'Spanish'],
    tags: ['neon', 'bust', '1v1', 'pursuit'],
    players: 4, maxPlayers: 32, ping: 32, accent: '#e4d29f', featured: true,
    logoUrl: 'https://identity.mta-neon.com/v1/server-registry/assets/fbb84b984b9417e04af995e0984b04fff76f152aab66dc0bedab1f2c61c0a0fa',
    bannerUrl: 'https://identity.mta-neon.com/v1/server-registry/assets/b884ecc5c538e248772a1c2e00b41f06ed9b0d7cb5952093f40ccd2d87a1ab47',
  },
  {
    ip: '146.59.208.11',
    name: 'Los Santos Stories',
    tagline: 'Une nouvelle vie à Los Santos 🌴 économie, factions et métiers sur mesure.',
    description:
      'Los Santos Stories propose un roleplay accessible et persistant : économie vivante, entreprises, services publics et histoires façonnées par les joueurs.',
    gameMode: 'Role Play', map: 'San Andreas', country: 'FR', countries: ['FR', 'BE', 'CH'],
    languages: ['French', 'English'], tags: ['roleplay', 'serious', 'economy'],
    players: 317, maxPlayers: 850, ping: 21, accent: '#4d7cc3',
    links: [
      { kind: 'website', label: 'https://proxima-rp.ru/', url: 'https://proxima-rp.ru/' },
      { kind: 'vk', label: '@proximarp', url: 'https://vk.com/proximarp' },
      { kind: 'discord', label: '.gg/proximarp', url: 'https://discord.gg/proximarp' },
    ],
  },
  {
    ip: '51.222.14.90',
    name: 'Vida — Brasil Roleplay',
    tagline: 'O melhor RP do Brasil 🌎 empregos, favelas, corridas 🏁 e muito mais.',
    description:
      'Brasil Vida Boa — roleplay pesado com economia própria, facções, empregos legais e ilegais. Comunidade ativa desde 2016.',
    gameMode: 'Roleplay', map: 'San Andreas', country: 'BR', countries: ['BR', 'PT'],
    languages: ['Portuguese'], tags: ['fivem', 'bvb', 'emprego', 'favela', 'trafico'],
    players: 336, maxPlayers: 700, ping: 78, accent: '#3aa35c',
    links: [
      { kind: 'website', label: 'https://vidaboa.com.br/', url: 'https://vidaboa.com.br/' },
      { kind: 'instagram', label: '@brasilvidaboa', url: 'https://instagram.com/brasilvidaboa' },
      { kind: 'discord', label: '.gg/vidaboa', url: 'https://discord.gg/vidaboa' },
    ],
  },
  {
    ip: '185.87.121.44',
    name: 'Türkiye — Voice Roleplay',
    tagline: 'Türkiye’nin sesli roleplay sunucusu 🎙️ polis, jandarma, mafya.',
    description:
      'Janti Roleplay — Türkçe sesli roleplay. Polis ve jandarma birimleri, mafya yapılanmaları, gelişmiş meslek sistemleri.',
    gameMode: 'Roleplay', map: 'San Andreas', country: 'TR',
    languages: ['Turkish'], tags: ['turk', 'polis', 'jandarma', 'mafyalar'],
    players: 342, maxPlayers: 900, ping: 38, accent: '#c34d4d',
    links: [
      { kind: 'discord', label: '.gg/janti', url: 'https://discord.gg/janti' },
      { kind: 'youtube', label: '@jantirp', url: 'https://youtube.com/@jantirp' },
    ],
  },
  {
    ip: '141.94.26.7',
    name: 'Southland — Serious Roleplay',
    tagline: 'RP français nouvelle génération 🌴 illégal & légal, LSPD, staff actif 🎯',
    description:
      'Southland Roleplay est un serveur RP français exigeant : économie persistante, entreprises joueurs, LSPD & EMS structurés, scènes illégales encadrées. Whitelist active, ambiance west coast.',
    gameMode: 'Roleplay', map: 'Los Santos', country: 'FR', countries: ['FR', 'GB'],
    languages: ['French', 'English'], tags: ['french', 'roleplay', 'heavy', 'southland.fr'],
    players: 262, maxPlayers: 500, ping: 22, passworded: true, accent: '#e8862b',
    links: [
      { kind: 'website', label: 'https://southland.fr/', url: 'https://southland.fr/' },
      { kind: 'instagram', label: '@southlandrp', url: 'https://instagram.com/southlandrp' },
      { kind: 'x', label: '@southlandrp', url: 'https://x.com/southlandrp' },
      { kind: 'facebook', label: '@southlandrp', url: 'https://facebook.com/southlandrp' },
      { kind: 'vk', label: '@southlandrp', url: 'https://vk.com/southlandrp' },
      { kind: 'discord', label: '.gg/southlandrp', url: 'https://discord.gg/southlandrp' },
    ],
  },
  {
    ip: '5.63.12.201',
    name: 'Persia — Roleplay',
    tagline: 'بهترین سرور رول‌پلی ایران 🔥 شغل‌ها، گنگ‌ها، اقتصاد واقعی 🚕',
    description:
      'Roxan MTA — بزرگ‌ترین جامعهٔ رول‌پلی فارسی‌زبان. سیستم شغل پیشرفته، گنگ‌وار، املاک و اقتصاد پویا.',
    gameMode: 'RolePlay', map: 'San Andreas', country: 'IR',
    languages: ['Persian'], tags: ['iranian', 'roleplay', 'roxan'],
    players: 171, maxPlayers: 250, ping: 47, accent: '#8a3a3a',
    links: [
      { kind: 'discord', label: '.gg/roxan', url: 'https://discord.gg/roxan' },
      { kind: 'instagram', label: '@roxanmta', url: 'https://instagram.com/roxanmta' },
    ],
  },
  {
    ip: '178.32.220.100',
    name: 'World Tour Racing',
    tagline: 'Race, drift and street events across San Andreas. One world, three regions.',
    description:
      'World Tour rassemble courses, drift, contre-la-montre et événements de rue dans une progression partagée entre plusieurs régions.',
    gameMode: 'Multigamemode', map: 'Mixed', country: 'GB', countries: ['GB', 'DE', 'NL'],
    languages: ['English'], tags: ['deathmatch', 'race', 'minigames', 'shooter', 'geoguesser'],
    players: 165, maxPlayers: 999, ping: 56, accent: '#d8a13a',
    links: [
      { kind: 'website', label: 'https://ffs.gg/', url: 'https://ffs.gg/' },
      { kind: 'discord', label: '.gg/ffsgaming', url: 'https://discord.gg/ffsgaming' },
    ],
  },
  {
    ip: '91.134.166.75',
    name: '[HUN] DawnMTA | GazdagRP | discord.gg/dawnmta',
    tagline: 'Magyar roleplay 🇭🇺 munkák, frakciók, saját gazdaság 💰',
    description:
      'DawnMTA — magyar roleplay szerver saját gazdasági rendszerrel, frakciókkal és heti eseményekkel. Csatlakozz a discordra!',
    gameMode: 'Roleplay', map: 'San Andreas', country: 'HU',
    languages: ['Hungarian'], tags: ['roleplay', 'hun', 'gazdag'],
    players: 112, maxPlayers: 999, ping: 124, accent: '#3a8a6e',
    links: [
      { kind: 'discord', label: '.gg/dawnmta', url: 'https://discord.gg/dawnmta' },
      { kind: 'facebook', label: '@dawnmta', url: 'https://facebook.com/dawnmta' },
    ],
  },
  {
    ip: '144.76.68.79',
    name: '| Vio | German Reallife | forum.vio-sa.com |',
    tagline: 'Deutsches Reallife seit 2010 🚓 Fraktionen, Taktik, Events 🎉',
    description:
      'Vio Reallife — der deutsche Reallife-Klassiker. Fraktionskriege, Wirtschaftssystem, regelmäßige Community-Events. Forum: vio-sa.com.',
    gameMode: 'Reallife', map: 'San Andreas', country: 'DE',
    languages: ['German'], tags: ['roleplay', 'reallife', 'tactics', 'vio-sa.com'],
    players: 76, maxPlayers: 200, ping: 31, accent: '#7d4dc3',
    links: [
      { kind: 'website', label: 'https://forum.vio-sa.com/', url: 'https://forum.vio-sa.com/' },
      { kind: 'discord', label: '.gg/vio', url: 'https://discord.gg/vio' },
    ],
  },
]

function heroToServer(spec: HeroSpec): ServerItem {
  return {
    id: serverKey(spec.ip, 22003),
    ip: spec.ip,
    gamePort: 22003,
    httpPort: 22005,
    name: spec.name,
    tagline: spec.tagline,
    description: spec.description,
    gameMode: spec.gameMode,
    map: spec.map,
    version: spec.version ?? CLIENT_VERSION,
    isCompatible: (spec.version ?? CLIENT_VERSION) === CLIENT_VERSION,
    players: spec.players,
    maxPlayers: spec.maxPlayers,
    ping: spec.ping,
    passworded: spec.passworded ?? false,
    requiresSerial: false,
    isStatusVerified: true,
    scanState: 'queued',
    playerList: makePlayers(spec.players),
    isFavourite: false,
    isFeatured: spec.featured ?? false,
    country: spec.country,
    countries: spec.countries,
    languages: spec.languages,
    tags: spec.tags,
    links: spec.links ?? [],
    accent: spec.accent,
    logoUrl: spec.logoUrl,
    bannerUrl: spec.bannerUrl,
  }
}

export function buildInternetList(): ServerItem[] {
  return HEROES.slice(0, 6).map(heroToServer)
}

export function buildLanList(): ServerItem[] {
  const dev = heroToServer({
    ip: '192.168.1.42',
    name: 'Neon Dev Server',
    tagline: 'Local development build 🔧',
    description: 'Serveur local de développement Neon.',
    gameMode: 'Sandbox', map: 'San Andreas', country: 'FR',
    languages: ['French'], tags: ['dev', 'local'],
    players: 1, maxPlayers: 32, ping: 1, accent: '#e8862b',
  })
  return [dev]
}
