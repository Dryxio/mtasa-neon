import { type ReactNode, type UIEvent, useEffect, useId, useRef, useState } from 'react'
import { createPortal } from 'react-dom'
import { SETTINGS_COPY, type SettingId, type SettingValue } from './settingsSchema'
import { type SettingsState, useSettingsBridge } from './settingsBridge'
import './Settings.css'

interface SettingsProps {
  onClose(): void
}

const CATEGORIES = ['Game', 'Graphics', 'Audio', 'Controls', 'Interface', 'Neon', 'Advanced'] as const

export function Settings({ onClose }: SettingsProps) {
  const settings = useSettingsBridge()
  const { state } = settings
  const values = state.values
  const [activeCategory, setActiveCategory] = useState<'Graphics' | 'Neon'>('Graphics')
  const [confirmation, setConfirmation] = useState<{ id: SettingId; value: SettingValue; title: string; message: string } | null>(null)
  const scrollEndTimer = useRef<number | undefined>(undefined)
  const scrollActive = useRef(false)

  const closeAndDiscard = () => {
    settings.cancel()
    settings.close()
    onClose()
  }

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key !== 'Escape' || document.querySelector('[role="tooltip"]')) return
      event.preventDefault()
      if (confirmation) {
        setConfirmation(null)
        return
      }
      closeAndDiscard()
    }
    window.addEventListener('keydown', onKeyDown)
    return () => window.removeEventListener('keydown', onKeyDown)
  })

  useEffect(() => () => window.clearTimeout(scrollEndTimer.current), [])

  const handleContentScroll = (event: UIEvent<HTMLElement>) => {
    const content = event.currentTarget
    if (!scrollActive.current) {
      scrollActive.current = true
      content.classList.add('settings__content--scrolling')
      window.dispatchEvent(new Event('neon-settings-scroll-start'))
    }
    window.clearTimeout(scrollEndTimer.current)
    scrollEndTimer.current = window.setTimeout(() => {
      scrollActive.current = false
      content.classList.remove('settings__content--scrolling')
    }, 120)
  }

  const skyEnabled = Boolean(values['skyGfx.enabled']) && !state.managed.skyGfx
  const colorFilterEnabled = skyEnabled && Boolean(values['skyGfx.colorFilter'])
  const radiosityEnabled = skyEnabled && Boolean(values['skyGfx.radiosity'])
  const setValue = (id: SettingId, value: SettingValue) => {
    if (id === 'graphics.dpiAware' && value === true && !values[id]) {
      setConfirmation({
        id, value, title: 'Experimental feature',
        message: 'DPI awareness is intended for scaled Windows displays and may cause graphical issues. It also requires an MTA restart.',
      })
      return
    }
    if (id === 'graphics.volumetricShadows' && value === true && !values[id]) {
      setConfirmation({
        id, value, title: 'Performance warning',
        message: 'Volumetric shadows can significantly reduce performance on some systems. Enable them anyway?',
      })
      return
    }
    settings.setValue(id, value)
  }

  return (
    <main className="settings" aria-label="Settings preview">
      <div className="settings__wash" />
      <header className="settings__header">
        <div>
          <span className="settings__eyebrow">Neon control center</span>
          <h1>Settings</h1>
        </div>
        <button className="settings__close" type="button" onClick={closeAndDiscard} aria-label="Close settings and discard unapplied changes">
          <span aria-hidden="true">Esc</span> Back
        </button>
      </header>

      <div className="settings__body">
        <nav className="settings__nav" aria-label="Settings categories">
          {CATEGORIES.map((category) => (
            <button
              key={category}
              type="button"
              className={category === activeCategory ? 'settings__nav-item settings__nav-item--active' : 'settings__nav-item'}
              disabled={category !== 'Neon' && category !== 'Graphics'}
              aria-current={category === activeCategory ? 'page' : undefined}
              onClick={() => (category === 'Neon' || category === 'Graphics') && setActiveCategory(category)}
            >
              <span aria-hidden="true">›</span>{category}
              {category !== 'Neon' && category !== 'Graphics' && <small>Later</small>}
            </button>
          ))}
        </nav>

        <section className="settings__content" aria-labelledby="settings-category-heading" onScroll={handleContentScroll}>
          <div className="settings__content-heading">
            <div>
              <span>{activeCategory === 'Neon' ? 'Neon-specific features' : 'Display and rendering'}</span>
              <h2 id="settings-category-heading">{activeCategory}</h2>
            </div>
            <button type="button" className="settings__reset" onClick={() => settings.reset(activeCategory === 'Neon' ? 'neon' : 'graphics')}>Reset {activeCategory}</button>
          </div>

          {!state.ready && <div className="settings__loading">Reading native settings…</div>}

          {activeCategory === 'Graphics' && <GraphicsSettings state={state} onChange={setValue} onAction={settings.action} />}

          {activeCategory === 'Neon' && <><SettingsGroup title="Extended world" caption="Stock world visibility beyond GTA's original limits.">
            <ToggleRow id="extendedWorld.enabled" value={Boolean(values['extendedWorld.enabled'])} onChange={setValue} />
            <RangeRow id="extendedWorld.distance" value={Number(values['extendedWorld.distance'])} min={300} max={5000} step={100} suffix=" m"
              disabled={!values['extendedWorld.enabled']} onChange={setValue} />
          </SettingsGroup>

          <SettingsGroup title="Project2DFX" caption="Integrated distant city lights — no ASI plugin required.">
            <ToggleRow id="distantLights.enabled" value={Boolean(values['distantLights.enabled'])} onChange={setValue} />
            <RangeRow id="distantLights.distance" value={Number(values['distantLights.distance'])} min={300} max={5000} step={100} suffix=" m"
              disabled={!values['distantLights.enabled']} onChange={setValue} />
            <RangeRow id="distantLights.coronaSize" value={Number(values['distantLights.coronaSize'])} min={0.1} max={1} step={0.05}
              format={(value) => `${Math.round(value * 100)}%`} disabled={!values['distantLights.enabled']} onChange={setValue} />
            <ActionRow
              label="Rebuild distant lights"
              description="Re-scans the loaded GTA world for Project2DFX light sources. Use this after world assets change during a session."
              disabled={!state.availability.rebuildDistantLights}
              button="Rebuild now"
              onAction={() => settings.action('rebuildDistantLights')}
            />
          </SettingsGroup>

          <SettingsGroup title="SkyGfx" caption={`Bridge status: ${state.skyGfxStatus}`} managed={state.managed.skyGfx}>
            <ToggleRow id="skyGfx.enabled" value={Boolean(values['skyGfx.enabled'])} disabled={state.managed.skyGfx} onChange={setValue} />
            <ToggleRow id="skyGfx.colorFilter" value={Boolean(values['skyGfx.colorFilter'])} disabled={!skyEnabled} onChange={setValue} />
            <ToggleRow id="skyGfx.colorFilterBlur" value={Boolean(values['skyGfx.colorFilterBlur'])} disabled={!colorFilterEnabled} onChange={setValue} />
            <ToggleRow id="skyGfx.pcTimecycle" value={Boolean(values['skyGfx.pcTimecycle'])} disabled={!colorFilterEnabled} onChange={setValue} />
            <ToggleRow id="skyGfx.depthBias" value={Boolean(values['skyGfx.depthBias'])} disabled={!skyEnabled} onChange={setValue} />
            <ToggleRow id="skyGfx.ycbcr" value={Boolean(values['skyGfx.ycbcr'])} disabled={!skyEnabled} onChange={setValue} />
            <ToggleRow id="skyGfx.radiosity" value={Boolean(values['skyGfx.radiosity'])} disabled={!skyEnabled} onChange={setValue} />
            <SelectRow id="skyGfx.radiosityIntensity" value={Number(values['skyGfx.radiosityIntensity'])} disabled={!radiosityEnabled}
              options={[['Soft · 24', 24], ['PS2 default · 35', 35], ['Strong · 48', 48], ['Very strong · 64', 64]]} onChange={setValue} />
            <SelectRow id="skyGfx.radiosityFilterPasses" value={Number(values['skyGfx.radiosityFilterPasses'])} disabled={!radiosityEnabled}
              options={[["1 pass", 1], ["2 passes", 2], ["3 passes", 3], ["4 passes", 4]]} onChange={setValue} />
            <SelectRow id="skyGfx.radiosityRenderPasses" value={Number(values['skyGfx.radiosityRenderPasses'])} disabled={!radiosityEnabled}
              options={[["1 pass", 1], ["2 passes", 2], ["3 passes", 3], ["4 passes", 4]]} onChange={setValue} />
          </SettingsGroup>

          <SettingsGroup title="Radar" caption="Position and shape use GTA's reference coordinate space." managed={state.managed.radar}>
            <SelectRow id="radar.style" value={Number(values['radar.style'])} disabled={state.managed.radar}
              options={[["GTA vanilla", 0], ["Definitive", 1]]} onChange={setValue} />
            <div className="settings__preset-row" aria-label="Radar size presets">
              <span>Size presets</span>
              <button type="button" disabled={state.managed.radar} onClick={() => settings.action('radarPreset', 'neon')}>Neon</button>
              <button type="button" disabled={state.managed.radar} onClick={() => settings.action('radarPreset', 'vanilla')}>Vanilla</button>
            </div>
            <RangeRow id="radar.positionX" value={Number(values['radar.positionX'])} min={0} max={640} step={1} disabled={state.managed.radar} onChange={setValue} />
            <RangeRow id="radar.positionY" value={Number(values['radar.positionY'])} min={0} max={448} step={1} disabled={state.managed.radar} onChange={setValue} />
            <RangeRow id="radar.width" value={Number(values['radar.width'])} min={40} max={200} step={0.5} disabled={state.managed.radar} onChange={setValue} />
            <RangeRow id="radar.height" value={Number(values['radar.height'])} min={40} max={200} step={0.5} disabled={state.managed.radar} onChange={setValue} />
            <ToggleRow id="radar.widescreenSafe" value={Boolean(values['radar.widescreenSafe'])} disabled={state.managed.radar} onChange={setValue} />
          </SettingsGroup></>}
        </section>
      </div>

      <footer className="settings__footer">
        <p>{state.dirty ? 'Unapplied changes' : state.restartRequired ? 'Applied · restart required' : 'All changes applied'}</p>
        <div>
          {state.restartRequired && <button type="button" className="settings__button settings__button--restart" onClick={() => settings.action('restartNow')}>Restart now</button>}
          <button type="button" className="settings__button settings__button--quiet" onClick={closeAndDiscard}>Cancel</button>
          <button type="button" className="settings__button settings__button--primary" disabled={!state.dirty || !state.ready} onClick={settings.apply}>Apply</button>
        </div>
      </footer>
      {confirmation && (
        <div className="settings-confirm" role="dialog" aria-modal="true" aria-labelledby="settings-confirm-title">
          <div className="settings-confirm__panel">
            <span>Graphics // confirmation</span>
            <h2 id="settings-confirm-title">{confirmation.title}</h2>
            <p>{confirmation.message}</p>
            <div>
              <button type="button" className="settings__button settings__button--quiet" onClick={() => setConfirmation(null)}>Cancel</button>
              <button type="button" className="settings__button settings__button--primary" onClick={() => {
                settings.setValue(confirmation.id, confirmation.value)
                setConfirmation(null)
              }}>Enable</button>
            </div>
          </div>
        </div>
      )}
    </main>
  )
}

function GraphicsSettings({ state, onChange, onAction }: {
  state: SettingsState
  onChange(id: SettingId, value: SettingValue): void
  onAction(name: string, argument?: string): void
}) {
  const values = state.values
  const fxQuality = Number(values['graphics.fxQuality'])
  const calibrationActive = Boolean(values['graphics.applyWindowed']) || Boolean(values['graphics.applyFullscreen'])
  const anisotropicOptions: Array<[string, number]> = [['Off', 0]]
  for (let level = 1; level <= state.availability.maxAnisotropic; level += 1) anisotropicOptions.push([`${2 ** level}x`, level])

  return <>
    <SettingsGroup title="Display" caption="Window mode, output resolution and presentation timing.">
      <SelectRow id="graphics.displayMode" value={Number(values['graphics.displayMode'])}
        options={[["Windowed", 0], ["Fullscreen", 1], ["Borderless window", 2], ["Borderless · keep resolution", 3]]} onChange={onChange} />
      <SelectRow id="graphics.videoMode" value={Number(values['graphics.videoMode'])}
        options={state.resolutions.map((resolution) => [
          `${resolution.width} × ${resolution.height} × ${resolution.depth}${resolution.unsafe ? ' · unsafe' : ''}`,
          resolution.mode,
        ])} onChange={onChange} />
      <ToggleRow id="graphics.vsync" value={Boolean(values['graphics.vsync'])} onChange={onChange} />
      {state.availability.multiMonitor && <ToggleRow id="graphics.fullscreenMinimize" value={Boolean(values['graphics.fullscreenMinimize'])}
        disabled={Number(values['graphics.displayMode']) === 0} onChange={onChange} />}
    </SettingsGroup>

    <SettingsGroup title="View & HUD" caption="Camera framing, original brightness and widescreen layout.">
      <RangeRow id="graphics.fov" value={Number(values['graphics.fov'])} min={70} max={90} step={5} suffix="°" onChange={onChange} />
      <RangeRow id="graphics.brightness" value={Number(values['graphics.brightness'])} min={0} max={100} step={1} suffix="%" onChange={onChange} />
      <SelectRow id="graphics.aspectRatio" value={Number(values['graphics.aspectRatio'])}
        options={[["Auto", 0], ["4:3", 1], ["16:10", 2], ["16:9", 3]]} onChange={onChange} />
      <ToggleRow id="graphics.hudMatchAspectRatio" value={Boolean(values['graphics.hudMatchAspectRatio'])} onChange={onChange} />
    </SettingsGroup>

    <SettingsGroup title="Rendering quality" caption="GTA world range, filtering and multisampling.">
      <RangeRow id="graphics.drawDistance" value={Number(values['graphics.drawDistance'])} min={0} max={100} step={1} suffix="%" onChange={onChange} />
      <SelectRow id="graphics.fxQuality" value={fxQuality}
        options={[["Low", 0], ["Medium", 1], ["High", 2], ["Very high", 3]]} onChange={onChange} />
      <SelectRow id="graphics.antiAliasing" value={Number(values['graphics.antiAliasing'])}
        options={[["Off", 1], ["1x", 2], ["2x", 3], ["3x", 4]]} onChange={onChange} />
      {state.availability.maxAnisotropic > 0 && <SelectRow id="graphics.anisotropic" value={Number(values['graphics.anisotropic'])}
        options={anisotropicOptions} onChange={onChange} />}
    </SettingsGroup>

    <SettingsGroup title="Visual effects" caption="Original GTA particles, shadows and camera effects.">
      <ToggleRow id="graphics.volumetricShadows" value={Boolean(values['graphics.volumetricShadows'])} disabled={fxQuality === 0} onChange={onChange} />
      <ToggleRow id="graphics.grass" value={Boolean(values['graphics.grass'])} disabled={fxQuality === 0} onChange={onChange} />
      <ToggleRow id="graphics.dynamicPedShadows" value={Boolean(values['graphics.dynamicPedShadows'])} disabled={fxQuality < 2} onChange={onChange} />
      <ToggleRow id="graphics.heatHaze" value={Boolean(values['graphics.heatHaze'])} onChange={onChange} />
      <ToggleRow id="graphics.tyreSmoke" value={Boolean(values['graphics.tyreSmoke'])} onChange={onChange} />
      <ToggleRow id="graphics.motionBlur" value={Boolean(values['graphics.motionBlur'])} onChange={onChange} />
      <ToggleRow id="graphics.coronaReflections" value={Boolean(values['graphics.coronaReflections'])} onChange={onChange} />
    </SettingsGroup>

    <SettingsGroup title="World details" caption="Force nearby actors to retain their highest-detail models.">
      <ToggleRow id="graphics.highDetailVehicles" value={Boolean(values['graphics.highDetailVehicles'])} onChange={onChange} />
      <ToggleRow id="graphics.highDetailPeds" value={Boolean(values['graphics.highDetailPeds'])} onChange={onChange} />
    </SettingsGroup>

    <SettingsGroup title="Display calibration" caption="Live, reversible color tuning for each presentation path.">
      <ToggleRow id="graphics.applyWindowed" value={Boolean(values['graphics.applyWindowed'])} onChange={onChange} />
      <ToggleRow id="graphics.applyFullscreen" value={Boolean(values['graphics.applyFullscreen'])} onChange={onChange} />
      <ToggleRow id="graphics.gammaEnabled" value={Boolean(values['graphics.gammaEnabled'])} disabled={!calibrationActive} onChange={onChange} />
      <RangeRow id="graphics.gamma" value={Number(values['graphics.gamma'])} min={0.5} max={2} step={0.01} format={(value) => `${value.toFixed(2)}x`}
        disabled={!calibrationActive || !values['graphics.gammaEnabled']} onChange={onChange} />
      <ToggleRow id="graphics.brightnessEnabled" value={Boolean(values['graphics.brightnessEnabled'])} disabled={!calibrationActive} onChange={onChange} />
      <RangeRow id="graphics.brightnessScale" value={Number(values['graphics.brightnessScale'])} min={0.5} max={2} step={0.01}
        format={(value) => `${value.toFixed(2)}x`} disabled={!calibrationActive || !values['graphics.brightnessEnabled']} onChange={onChange} />
      <ToggleRow id="graphics.contrastEnabled" value={Boolean(values['graphics.contrastEnabled'])} disabled={!calibrationActive} onChange={onChange} />
      <RangeRow id="graphics.contrast" value={Number(values['graphics.contrast'])} min={0.5} max={2} step={0.01} format={(value) => `${value.toFixed(2)}x`}
        disabled={!calibrationActive || !values['graphics.contrastEnabled']} onChange={onChange} />
      <ToggleRow id="graphics.saturationEnabled" value={Boolean(values['graphics.saturationEnabled'])} disabled={!calibrationActive} onChange={onChange} />
      <RangeRow id="graphics.saturation" value={Number(values['graphics.saturation'])} min={0.5} max={2} step={0.01} format={(value) => `${value.toFixed(2)}x`}
        disabled={!calibrationActive || !values['graphics.saturationEnabled']} onChange={onChange} />
      <ActionRow label="Reset display calibration" description="Restores neutral 1.00x values and disables every calibration adjustment and scope."
        disabled={false} button="Reset calibration" onAction={() => onAction('resetDisplayCalibration')} />
    </SettingsGroup>

    <SettingsGroup title="Compatibility" caption="Advanced display startup and troubleshooting options.">
      <ToggleRow id="graphics.dpiAware" value={Boolean(values['graphics.dpiAware'])} onChange={onChange} />
      {state.availability.unsafeResolutions && <ToggleRow id="graphics.showUnsafeResolutions" value={Boolean(values['graphics.showUnsafeResolutions'])}
        onChange={onChange} />}
      {state.availability.multiMonitor && <ToggleRow id="graphics.deviceSelectionDialog" value={Boolean(values['graphics.deviceSelectionDialog'])}
        onChange={onChange} />}
    </SettingsGroup>
  </>
}

function SettingsGroup({ title, caption, managed = false, children }: { title: string; caption: string; managed?: boolean; children: ReactNode }) {
  return (
    <section className="settings-group">
      <header>
        <div><h3>{title}</h3><p>{caption}</p></div>
        {managed && <span className="settings-group__managed">Managed by server</span>}
      </header>
      <div className="settings-group__rows">{children}</div>
    </section>
  )
}

interface RowProps {
  id: SettingId
  disabled?: boolean
  onChange(id: SettingId, value: SettingValue): void
}

function ToggleRow({ id, value, disabled = false, onChange }: RowProps & { value: boolean }) {
  return (
    <SettingRow id={id} disabled={disabled}>
      <button className="settings-toggle" type="button" role="switch" aria-checked={value} disabled={disabled} onClick={() => onChange(id, !value)}>
        <span />{value ? 'On' : 'Off'}
      </button>
    </SettingRow>
  )
}

function RangeRow({ id, value, min, max, step, suffix = '', format, disabled = false, onChange }: RowProps & {
  value: number; min: number; max: number; step: number; suffix?: string; format?: (value: number) => string
}) {
  return (
    <SettingRow id={id} disabled={disabled}>
      <div className="settings-range">
        <input type="range" aria-label={SETTINGS_COPY[id].label} value={value} min={min} max={max} step={step} disabled={disabled}
          onChange={(event) => onChange(id, Number(event.target.value))} />
        <output>{format ? format(value) : `${value}${suffix}`}</output>
      </div>
    </SettingRow>
  )
}

function SelectRow({ id, value, options, disabled = false, onChange }: RowProps & { value: number; options: Array<[string, number]> }) {
  return (
    <SettingRow id={id} disabled={disabled}>
      <select aria-label={SETTINGS_COPY[id].label} value={value} disabled={disabled} onChange={(event) => onChange(id, Number(event.target.value))}>
        {options.map(([label, optionValue]) => <option key={optionValue} value={optionValue}>{label}</option>)}
      </select>
    </SettingRow>
  )
}

function ActionRow({ label, description, button, disabled, onAction }: { label: string; description: string; button: string; disabled: boolean; onAction(): void }) {
  return (
    <SettingRow copy={{ label, description }} disabled={disabled}>
      <button className="settings-action" type="button" disabled={disabled} onClick={onAction}>{button}</button>
    </SettingRow>
  )
}

function SettingRow({ id, copy, disabled = false, children }: { id?: SettingId; copy?: { label: string; description: string }; disabled?: boolean; children: ReactNode }) {
  const content = id ? SETTINGS_COPY[id] : copy!
  const tooltipId = useId()
  const rowRef = useRef<HTMLDivElement>(null)
  const helpRef = useRef<HTMLButtonElement>(null)
  const delayRef = useRef<number | undefined>(undefined)
  const [visible, setVisible] = useState(false)
  const [position, setPosition] = useState({ left: 0, top: 0, above: false })

  const show = (delayed: boolean) => {
    window.clearTimeout(delayRef.current)
    delayRef.current = window.setTimeout(() => setVisible(true), delayed ? 200 : 0)
  }
  const hide = () => {
    window.clearTimeout(delayRef.current)
    setVisible(false)
  }
  useEffect(() => {
    if (!visible || !rowRef.current || !helpRef.current) return
    const rowRect = rowRef.current.getBoundingClientRect()
    const helpRect = helpRef.current.getBoundingClientRect()
    const controlRect = rowRef.current.querySelector('.setting-row__control')?.getBoundingClientRect()
    const width = Math.min(320, window.innerWidth - 24)
    const estimatedHeight = 104
    const above = helpRect.bottom + estimatedHeight + 12 > window.innerHeight
    const viewportMaximum = window.innerWidth - width - 12
    const controlMaximum = controlRect ? controlRect.left - width - 14 : viewportMaximum
    setPosition({
      left: Math.max(12, Math.min(helpRect.left, controlMaximum, viewportMaximum, rowRect.right - width)),
      top: above ? Math.max(12, helpRect.top - estimatedHeight - 8) : helpRect.bottom + 8,
      above,
    })
  }, [visible])

  useEffect(() => {
    const hideForScroll = () => {
      window.clearTimeout(delayRef.current)
      setVisible(false)
    }
    window.addEventListener('neon-settings-scroll-start', hideForScroll)
    return () => window.removeEventListener('neon-settings-scroll-start', hideForScroll)
  }, [])

  useEffect(() => {
    if (!visible) return
    const closeOnEscape = (event: KeyboardEvent) => {
      if (event.key !== 'Escape') return
      event.preventDefault()
      event.stopImmediatePropagation()
      hide()
    }
    window.addEventListener('keydown', closeOnEscape, true)
    return () => window.removeEventListener('keydown', closeOnEscape, true)
  }, [visible])

  useEffect(() => () => window.clearTimeout(delayRef.current), [])

  return (
    <div ref={rowRef} className={disabled ? 'setting-row setting-row--disabled' : 'setting-row'}>
      <div className="setting-row__label">
        <span>{content.label}</span>
        <button
          ref={helpRef}
          type="button"
          className="setting-row__help"
          aria-label={`Explain ${content.label}`}
          aria-describedby={visible ? tooltipId : undefined}
          onMouseEnter={() => show(true)}
          onMouseLeave={hide}
          onFocus={() => show(false)}
          onBlur={hide}
          onClick={() => setVisible((current) => !current)}
        >i</button>
      </div>
      <div className="setting-row__control">{children}</div>
      {visible && createPortal(
        <div id={tooltipId} role="tooltip" className={position.above ? 'settings-tooltip settings-tooltip--above' : 'settings-tooltip'}
          style={{ left: position.left, top: position.top }}>
          <strong>{content.label}</strong>
          <p>{content.description}</p>
        </div>, document.body,
      )}
    </div>
  )
}
