const LOADSCREEN_COUNT = 14
const LOADSCREEN_SESSION_KEY = 'neon-main-menu-loadscreen'

/**
 * Choisit une illustration une seule fois par session afin que le menu et le
 * navigateur donnent l'impression d'appartenir au même écran GTA SA.
 */
export function chooseLaunchLoadscreen(): string {
  const override = Number(new URLSearchParams(window.location.search).get('loadscreen'))
  if (Number.isInteger(override) && override >= 1 && override <= LOADSCREEN_COUNT) {
    return `./loadscreens/loadsc${override}.webp`
  }

  const stored = window.sessionStorage.getItem(LOADSCREEN_SESSION_KEY)
  if (stored && /^\.\/loadscreens\/loadsc(?:[1-9]|1[0-4])\.webp$/.test(stored)) return stored

  const randomValue = new Uint32Array(1)
  window.crypto.getRandomValues(randomValue)
  const selected = `./loadscreens/loadsc${(randomValue[0]! % LOADSCREEN_COUNT) + 1}.webp`
  window.sessionStorage.setItem(LOADSCREEN_SESSION_KEY, selected)
  return selected
}
