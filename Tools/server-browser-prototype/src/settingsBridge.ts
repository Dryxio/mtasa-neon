import { useCallback, useEffect, useRef, useState } from 'react'
import { DEFAULT_SETTINGS, type SettingId, type SettingValue, type SettingValues } from './settingsSchema'

export interface SettingsState {
  values: SettingValues
  managed: { skyGfx: boolean; radar: boolean }
  availability: { rebuildDistantLights: boolean; maxAnisotropic: number; multiMonitor: boolean; unsafeResolutions: boolean }
  resolutions: Array<{ mode: number; width: number; height: number; depth: number; unsafe: boolean }>
  skyGfxStatus: string
  dirty: boolean
  restartRequired: boolean
  ready: boolean
}

type NativeSettingsEvent = {
  type: 'init' | 'state'
  values: SettingValues
  managed: SettingsState['managed']
  availability: SettingsState['availability']
  resolutions: SettingsState['resolutions']
  skyGfxStatus: string
  dirty: boolean
  restartRequired: boolean
}

declare global {
  interface Window {
    __neonSettings?: { emit(events: NativeSettingsEvent[]): void }
  }
}

const INITIAL_STATE: SettingsState = {
  values: DEFAULT_SETTINGS,
  managed: { skyGfx: false, radar: false },
  availability: { rebuildDistantLights: false, maxAnisotropic: 4, multiMonitor: true, unsafeResolutions: true },
  resolutions: [
    { mode: 0, width: 1920, height: 1080, depth: 32, unsafe: false },
    { mode: 1, width: 1600, height: 900, depth: 32, unsafe: false },
    { mode: 2, width: 1280, height: 720, depth: 32, unsafe: false },
  ],
  skyGfxStatus: 'Preview outside the MTA client',
  dirty: false,
  restartRequired: false,
  ready: false,
}

function send(name: string, ...args: string[]): boolean {
  if (typeof window.mta?.triggerEvent !== 'function') return false
  window.mta.triggerEvent(name, ...args)
  return true
}

export function useSettingsBridge() {
  const [state, setState] = useState<SettingsState>(INITIAL_STATE)
  const appliedValues = useRef<SettingValues>(DEFAULT_SETTINGS)

  useEffect(() => {
    window.__neonSettings = {
      emit(events) {
        for (const event of events) {
          if (event.type === 'init' || event.type === 'state') {
            setState({ ...event, ready: true })
            if (!event.dirty) appliedValues.current = event.values
          }
        }
      },
    }
    if (!send('settings:ready')) setState((current) => ({ ...current, ready: true }))
    return () => {
      delete window.__neonSettings
    }
  }, [])

  const setValue = useCallback((id: SettingId, value: SettingValue) => {
    setState((current) => ({
      ...current,
      values: { ...current.values, [id]: value },
      dirty: true,
    }))
    send('settings:set', id, String(value))
  }, [])

  const apply = useCallback(() => {
    setState((current) => {
      appliedValues.current = current.values
      return { ...current, dirty: false }
    })
    send('settings:apply')
  }, [])

  const cancel = useCallback(() => {
    setState((current) => ({ ...current, values: appliedValues.current, dirty: false }))
    send('settings:cancel')
  }, [])

  const reset = useCallback((section: 'neon' | 'graphics') => {
    send('settings:resetSection', section)
  }, [])

  const action = useCallback((name: string, argument?: string) => {
    send('settings:action', name, ...(argument ? [argument] : []))
    if (name !== 'radarPreset') return
    setState((current) => ({
      ...current,
      values: {
        ...current.values,
        'radar.positionX': 40,
        'radar.positionY': 104,
        'radar.width': argument === 'vanilla' ? 94 : 85.5,
        'radar.height': argument === 'vanilla' ? 76 : 78,
        'radar.widescreenSafe': true,
      },
      dirty: true,
    }))
  }, [])

  const close = useCallback(() => send('settings:close'), [])

  return { state, setValue, apply, cancel, reset, action, close }
}
