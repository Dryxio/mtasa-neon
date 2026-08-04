export type UiSound = 'highlight' | 'select' | 'back' | 'error'

let lastHighlightAt = 0

/**
 * Uses GTA SA's resident frontend bank through the native bridge. Highlight
 * events are lightly throttled so fast mouse movement cannot stack samples.
 */
export function playUiSound(sound: UiSound): void {
  if (sound === 'highlight') {
    const now = performance.now()
    if (now - lastHighlightAt < 45) return
    lastHighlightAt = now
  }

  window.mta?.triggerEvent('menu:sound', sound)
}
