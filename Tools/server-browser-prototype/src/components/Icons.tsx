import type { JSX } from 'react'
import type { LinkKind } from '../types'

/** Icônes SVG inline (base feather/simple-icons) — aucune dépendance réseau. */

interface IconProps {
  size?: number
}

function icon(path: JSX.Element, viewBox = '0 0 24 24', fill = false) {
  return function Icon({ size = 16 }: IconProps) {
    return (
      <svg
        width={size}
        height={size}
        viewBox={viewBox}
        fill={fill ? 'currentColor' : 'none'}
        stroke={fill ? 'none' : 'currentColor'}
        strokeWidth={fill ? 0 : 2}
        strokeLinecap="round"
        strokeLinejoin="round"
        aria-hidden
      >
        {path}
      </svg>
    )
  }
}

export const IconUsers = icon(
  <>
    <path d="M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2" />
    <circle cx="9" cy="7" r="4" />
    <path d="M23 21v-2a4 4 0 0 0-3-3.87" />
    <path d="M16 3.13a4 4 0 0 1 0 7.75" />
  </>,
)

export const IconActivity = icon(<polyline points="22 12 18 12 15 21 9 3 6 12 2 12" />)

export const IconClose = icon(
  <>
    <line x1="18" y1="6" x2="6" y2="18" />
    <line x1="6" y1="6" x2="18" y2="18" />
  </>,
)

export const IconChevronDown = icon(<polyline points="6 9 12 15 18 9" />)

export const IconRefresh = icon(
  <>
    <polyline points="23 4 23 10 17 10" />
    <path d="M20.49 15a9 9 0 1 1-2.12-9.36L23 10" />
  </>,
)

export const IconSearch = icon(
  <>
    <circle cx="11" cy="11" r="8" />
    <line x1="21" y1="21" x2="16.65" y2="16.65" />
  </>,
)

export const IconPlay = icon(<polygon points="6 3 20 12 6 21 6 3" />, '0 0 24 24', true)

export const IconFilter = icon(
  <polygon points="22 3 2 3 10 12.46 10 19 14 21 14 12.46 22 3" />,
  '0 0 24 24',
  true,
)

export const IconLock = icon(
  <>
    <rect x="3" y="11" width="18" height="11" rx="2" ry="2" />
    <path d="M7 11V7a5 5 0 0 1 10 0v4" />
  </>,
)

export const IconStar = icon(
  <polygon points="12 2 15.09 8.26 22 9.27 17 14.14 18.18 21.02 12 17.77 5.82 21.02 7 14.14 2 9.27 8.91 8.26 12 2" />,
)

export const IconStarFilled = icon(
  <polygon points="12 2 15.09 8.26 22 9.27 17 14.14 18.18 21.02 12 17.77 5.82 21.02 7 14.14 2 9.27 8.91 8.26 12 2" />,
  '0 0 24 24',
  true,
)

export const IconCopy = icon(
  <>
    <rect x="9" y="9" width="13" height="13" rx="2" ry="2" />
    <path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1" />
  </>,
)

export const IconCheck = icon(<polyline points="20 6 9 17 4 12" />)

export const IconGlobe = icon(
  <>
    <circle cx="12" cy="12" r="10" />
    <line x1="2" y1="12" x2="22" y2="12" />
    <path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z" />
  </>,
)

export const IconTag = icon(
  <>
    <path d="M20.59 13.41l-7.17 7.17a2 2 0 0 1-2.83 0L2 12V2h10l8.59 8.59a2 2 0 0 1 0 2.82z" />
    <line x1="7" y1="7" x2="7.01" y2="7" />
  </>,
)

export const IconAt = icon(
  <>
    <circle cx="12" cy="12" r="4" />
    <path d="M16 8v5a3 3 0 0 0 6 0v-1a10 10 0 1 0-3.92 7.94" />
  </>,
)

export const IconLink = icon(
  <>
    <path d="M10 13a5 5 0 0 0 7.54.54l3-3a5 5 0 0 0-7.07-7.07l-1.72 1.71" />
    <path d="M14 11a5 5 0 0 0-7.54-.54l-3 3a5 5 0 0 0 7.07 7.07l1.71-1.71" />
  </>,
)

export const IconArrowUp = icon(
  <>
    <line x1="12" y1="19" x2="12" y2="5" />
    <polyline points="5 12 12 5 19 12" />
  </>,
)

export const IconArrowDown = icon(
  <>
    <line x1="12" y1="5" x2="12" y2="19" />
    <polyline points="19 12 12 19 5 12" />
  </>,
)

export const IconEnter = icon(
  <>
    <polyline points="9 10 4 15 9 20" />
    <path d="M20 4v7a4 4 0 0 1-4 4H4" />
  </>,
)

const IconDiscord = icon(
  <path d="M20.32 4.37a19.8 19.8 0 0 0-4.89-1.52.07.07 0 0 0-.08.04c-.21.38-.44.87-.6 1.25a18.3 18.3 0 0 0-5.5 0 12.6 12.6 0 0 0-.61-1.25.08.08 0 0 0-.08-.04 19.7 19.7 0 0 0-4.88 1.52.07.07 0 0 0-.04.03C.53 9.05-.32 13.58.1 18.06a.08.08 0 0 0 .03.05 19.9 19.9 0 0 0 6 3.03.08.08 0 0 0 .08-.02c.46-.63.87-1.3 1.22-2a.08.08 0 0 0-.04-.11 13.1 13.1 0 0 1-1.87-.9.08.08 0 0 1-.01-.12c.13-.1.25-.2.37-.3a.07.07 0 0 1 .08 0 14.2 14.2 0 0 0 12.06 0 .07.07 0 0 1 .08 0c.12.1.25.21.38.3a.08.08 0 0 1 0 .13c-.6.34-1.22.64-1.88.89a.08.08 0 0 0-.04.11c.36.7.77 1.37 1.22 2a.08.08 0 0 0 .09.03 19.8 19.8 0 0 0 6-3.03.08.08 0 0 0 .04-.05c.5-5.18-.84-9.67-3.55-13.66a.06.06 0 0 0-.03-.03zM8.02 15.33c-1.18 0-2.16-1.08-2.16-2.42 0-1.33.96-2.42 2.16-2.42 1.21 0 2.18 1.1 2.16 2.42 0 1.34-.96 2.42-2.16 2.42zm7.97 0c-1.18 0-2.15-1.08-2.15-2.42 0-1.33.95-2.42 2.15-2.42 1.22 0 2.18 1.1 2.16 2.42 0 1.34-.94 2.42-2.16 2.42z" />,
  '0 0 24 24',
  true,
)

const IconInstagram = icon(
  <>
    <rect x="2" y="2" width="20" height="20" rx="5" ry="5" />
    <path d="M16 11.37A4 4 0 1 1 12.63 8 4 4 0 0 1 16 11.37z" />
    <line x1="17.5" y1="6.5" x2="17.51" y2="6.5" />
  </>,
)

const IconX = icon(
  <path d="M18.9 1.15h3.68l-8.04 9.19L24 22.85h-7.41l-5.8-7.58-6.64 7.58H.47l8.6-9.83L0 1.15h7.59l5.24 6.93zm-1.29 19.5h2.04L6.49 3.24H4.3z" />,
  '0 0 24 24',
  true,
)

const IconFacebook = icon(
  <path d="M18 2h-3a5 5 0 0 0-5 5v3H7v4h3v8h4v-8h3l1-4h-4V7a1 1 0 0 1 1-1h3z" />,
  '0 0 24 24',
  true,
)

const IconVk = icon(
  <path d="M12.78 18.4h1.2s.36-.04.55-.24c.17-.18.16-.53.16-.53s-.02-1.61.73-1.85c.74-.23 1.69 1.56 2.7 2.25.76.52 1.34.4 1.34.4l2.7-.03s1.41-.09.74-1.2c-.05-.09-.39-.82-2-2.32-1.7-1.57-1.47-1.32.57-4.04 1.24-1.65 1.74-2.66 1.58-3.1-.15-.4-1.06-.3-1.06-.3l-3.04.02s-.23-.03-.4.07c-.16.1-.26.32-.26.32s-.48 1.28-1.12 2.37c-1.35 2.29-1.9 2.41-2.12 2.27-.5-.33-.38-1.33-.38-2.04 0-2.22.34-3.15-.65-3.39-.33-.08-.57-.13-1.4-.14-1.08-.01-1.99 0-2.5.25-.35.17-.61.54-.45.56.2.03.65.12.89.45.3.42.29 1.37.29 1.37s.18 2.62-.41 2.94c-.4.22-.96-.23-2.16-2.3-.61-1.07-1.07-2.25-1.07-2.25s-.09-.22-.25-.34a1.25 1.25 0 0 0-.47-.19l-2.89.02s-.43.01-.59.2c-.14.17-.01.52-.01.52s2.26 5.29 4.83 7.96c2.35 2.44 5.02 2.28 5.02 2.28z" />,
  '0 0 24 24',
  true,
)

const IconYoutube = icon(
  <path d="M23.5 6.19a3.02 3.02 0 0 0-2.12-2.14C19.5 3.55 12 3.55 12 3.55s-7.5 0-9.38.5A3.02 3.02 0 0 0 .5 6.19C0 8.07 0 12 0 12s0 3.93.5 5.81a3.02 3.02 0 0 0 2.12 2.14c1.88.5 9.38.5 9.38.5s7.5 0 9.38-.5a3.02 3.02 0 0 0 2.12-2.14C24 15.93 24 12 24 12s0-3.93-.5-5.81zM9.55 15.57V8.43L15.82 12z" />,
  '0 0 24 24',
  true,
)

const IconTiktok = icon(
  <path d="M12.53.02C13.84 0 15.14.01 16.44 0c.08 1.53.63 3.09 1.75 4.17 1.12 1.11 2.7 1.62 4.24 1.79v4.03c-1.44-.05-2.89-.35-4.2-.97-.57-.26-1.1-.59-1.62-.93-.01 2.92.01 5.84-.02 8.75-.08 1.4-.54 2.79-1.35 3.94-1.31 1.92-3.58 3.17-5.91 3.21-1.43.08-2.86-.31-4.08-1.03-2.02-1.19-3.44-3.37-3.65-5.71-.02-.5-.03-1-.01-1.49.18-1.9 1.12-3.72 2.58-4.96 1.66-1.44 3.98-2.13 6.15-1.72.02 1.48-.04 2.96-.04 4.44-.99-.32-2.15-.23-3.02.37-.63.41-1.11 1.04-1.36 1.75-.21.51-.15 1.07-.14 1.61.24 1.64 1.82 3.02 3.5 2.87 1.12-.01 2.19-.66 2.77-1.61.19-.33.4-.67.41-1.06.1-1.79.06-3.57.07-5.36.01-4.03-.01-8.05.02-12.07z" />,
  '0 0 24 24',
  true,
)

export const LINK_ICONS: Record<LinkKind, (props: IconProps) => JSX.Element> = {
  website: IconLink,
  discord: IconDiscord,
  instagram: IconInstagram,
  x: IconX,
  facebook: IconFacebook,
  vk: IconVk,
  youtube: IconYoutube,
  tiktok: IconTiktok,
}

function FlagArtwork({ code }: { code: string }) {
  switch (code) {
    case 'FR':
      return <><rect width="8" height="16" fill="#1d3f91" /><rect x="8" width="8" height="16" fill="#f3f0e8" /><rect x="16" width="8" height="16" fill="#d73b3e" /></>
    case 'GB':
      return <><rect width="24" height="16" fill="#23417c" /><path d="M0 0 24 16M24 0 0 16" stroke="#f3f0e8" strokeWidth="5" /><path d="M0 0 24 16M24 0 0 16" stroke="#c93942" strokeWidth="2" /><path d="M12 0v16M0 8h24" stroke="#f3f0e8" strokeWidth="5" /><path d="M12 0v16M0 8h24" stroke="#c93942" strokeWidth="2.5" /></>
    case 'BR':
      return <><rect width="24" height="16" fill="#25834a" /><path d="m12 2 9.5 6-9.5 6-9.5-6Z" fill="#e7c843" /><circle cx="12" cy="8" r="3.2" fill="#31528a" /><path d="M9.2 7.2c2.1-.7 4.2-.25 5.8.8" fill="none" stroke="#f3f0e8" strokeWidth=".7" /></>
    case 'RU':
      return <><rect width="24" height="5.34" fill="#f3f0e8" /><rect y="5.33" width="24" height="5.34" fill="#31539a" /><rect y="10.66" width="24" height="5.34" fill="#cc3d48" /></>
    case 'TR':
      return <><rect width="24" height="16" fill="#c93643" /><circle cx="10" cy="8" r="4.4" fill="#f3f0e8" /><circle cx="11.3" cy="8" r="3.5" fill="#c93643" /><path d="m14.2 8 2.7-.9-1.65 2.25V6.65l1.65 2.25Z" fill="#f3f0e8" /></>
    case 'PL':
      return <><rect width="24" height="8" fill="#f3f0e8" /><rect y="8" width="24" height="8" fill="#d43c55" /></>
    case 'ID':
      return <><rect width="24" height="8" fill="#d73b45" /><rect y="8" width="24" height="8" fill="#f3f0e8" /></>
    case 'ES':
      return <><rect width="24" height="4" fill="#b9323b" /><rect y="4" width="24" height="8" fill="#e5bd3d" /><rect y="12" width="24" height="4" fill="#b9323b" /></>
    case 'IR':
      return <><rect width="24" height="5.34" fill="#23945b" /><rect y="5.33" width="24" height="5.34" fill="#f3f0e8" /><rect y="10.66" width="24" height="5.34" fill="#ce3f47" /><path d="M12 5.9c-.9 1.05-1.35 1.8-1.35 2.55 0 .95.55 1.65 1.35 1.65s1.35-.7 1.35-1.65c0-.75-.45-1.5-1.35-2.55Zm0 .8v2.55" fill="none" stroke="#ce3f47" strokeWidth=".65" strokeLinecap="round" /></>
    case 'MA':
      return <><rect width="24" height="16" fill="#bd303b" /><path d="m12 4.1 2.3 7.05-6-4.35h7.4l-6 4.35Z" fill="none" stroke="#1d7b50" strokeWidth=".8" strokeLinejoin="round" /></>
    case 'HU':
      return <><rect width="24" height="5.34" fill="#ce3f47" /><rect y="5.33" width="24" height="5.34" fill="#f3f0e8" /><rect y="10.66" width="24" height="5.34" fill="#308b5b" /></>
    default:
      return <><rect width="24" height="16" fill="#151a1d" /><text x="12" y="11.3" textAnchor="middle" fill="#d9d4c7" fontSize="7" fontFamily="Arial, sans-serif">{code}</text></>
  }
}

/** Sprite SVG local : fiable dans CEF/Windows, contrairement aux emoji. */
export function Flag({ country }: { country?: string }) {
  if (!country || country.length !== 2) return null
  const code = country.toUpperCase()
  return (
    <svg className="server-row__flag" viewBox="0 0 24 16" role="img" aria-label={code}>
      <FlagArtwork code={code} />
    </svg>
  )
}
