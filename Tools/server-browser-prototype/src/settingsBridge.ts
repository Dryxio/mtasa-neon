import { useCallback, useEffect, useRef, useState } from 'react'
import { DEFAULT_SETTINGS, type SettingId, type SettingValue, type SettingValues } from './settingsSchema'

export interface SettingsBindRow {
  id: string
  section: string
  label: string
  keys: string[]
}

export interface JoypadAxis {
  index: number
  output: string
  input: string
}

export interface SettingsState {
  values: SettingValues
  managed: { skyGfx: boolean; radar: boolean }
  availability: {
    rebuildDistantLights: boolean
    maxAnisotropic: number
    multiMonitor: boolean
    unsafeResolutions: boolean
    customizedSAFiles: boolean
    connected: boolean
    streamingMemoryMin: number
    streamingMemoryMax: number
    resourceCachePath: string
  }
  resolutions: Array<{ mode: number; width: number; height: number; depth: number; unsafe: boolean }>
  locales: Array<{ code: string; label: string }>
  chatPresets: Array<{ id: string; name: string }>
  skins: string[]
  browserBlacklist: string[]
  browserWhitelist: string[]
  binds: SettingsBindRow[]
  joypad: { connected: boolean; name: string; capturingAxis: number; axes: JoypadAxis[] }
  capture: { bindId: string; slot: number } | null
  error: string
  skyGfxStatus: string
  dirty: boolean
  restartRequired: boolean
  disconnectRequired: boolean
  ready: boolean
}

type NativeSettingsEvent = {
  type: 'init' | 'state'
  values: SettingValues
  managed: SettingsState['managed']
  availability: SettingsState['availability']
  resolutions: SettingsState['resolutions']
  locales: SettingsState['locales']
  chatPresets: SettingsState['chatPresets']
  skins: SettingsState['skins']
  browserBlacklist: SettingsState['browserBlacklist']
  browserWhitelist: SettingsState['browserWhitelist']
  binds: SettingsState['binds']
  joypad: SettingsState['joypad']
  capture: SettingsState['capture']
  error: string
  skyGfxStatus: string
  dirty: boolean
  restartRequired: boolean
  disconnectRequired: boolean
}

declare global {
  interface Window {
    __neonSettings?: { emit(events: NativeSettingsEvent[]): void }
  }
}

const INITIAL_STATE: SettingsState = {
  values: DEFAULT_SETTINGS,
  managed: { skyGfx: false, radar: false },
  availability: {
    rebuildDistantLights: false, maxAnisotropic: 4, multiMonitor: true, unsafeResolutions: true,
    customizedSAFiles: false, connected: false, streamingMemoryMin: 32, streamingMemoryMax: 512, resourceCachePath: '',
  },
  resolutions: [
    { mode: 0, width: 1920, height: 1080, depth: 32, unsafe: false },
    { mode: 1, width: 1600, height: 900, depth: 32, unsafe: false },
    { mode: 2, width: 1280, height: 720, depth: 32, unsafe: false },
  ],
  locales: [{ code: 'en_US', label: 'English' }],
  chatPresets: [],
  skins: ['Default'],
  browserBlacklist: [],
  browserWhitelist: [],
  binds: [],
  joypad: { connected: false, name: 'No joypad detected', capturingAxis: -1, axes: [] },
  capture: null,
  error: '',
  skyGfxStatus: 'Preview outside the MTA client',
  dirty: false,
  restartRequired: false,
  disconnectRequired: false,
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
    send('settings:apply')
  }, [])

  const cancel = useCallback(() => {
    setState((current) => ({ ...current, values: appliedValues.current, dirty: false }))
    send('settings:cancel')
  }, [])

  const reset = useCallback((section: 'game' | 'graphics' | 'audio' | 'controls' | 'interface' | 'neon' | 'advanced') => {
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
