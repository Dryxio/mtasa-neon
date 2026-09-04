import { type ReactNode, useEffect, useId, useRef, useState } from 'react'
import { createPortal } from 'react-dom'
import { IconArrowLeft } from './components/Icons'
import { chooseLaunchLoadscreen } from './loadscreen'
import { SETTINGS_COPY, type SettingId, type SettingValue } from './settingsSchema'
import { type SettingsBindRow, type SettingsState, useSettingsBridge } from './settingsBridge'
import './Settings.css'

interface SettingsProps {
  onClose(): void
}

const CATEGORIES = ['Game', 'Graphics', 'Audio', 'Controls', 'Interface', 'Neon', 'Advanced'] as const
type SettingsCategory = typeof CATEGORIES[number]

const CATEGORY_COPY: Record<SettingsCategory, { eyebrow: string; reset: 'game' | 'graphics' | 'audio' | 'controls' | 'interface' | 'neon' | 'advanced' }> = {
  Game: { eyebrow: 'Player and multiplayer', reset: 'game' },
  Graphics: { eyebrow: 'Display and rendering', reset: 'graphics' },
  Audio: { eyebrow: 'Sound and user tracks', reset: 'audio' },
  Controls: { eyebrow: 'Input and key bindings', reset: 'controls' },
  Interface: { eyebrow: 'Chat, language and web privacy', reset: 'interface' },
  Neon: { eyebrow: 'Neon-specific features', reset: 'neon' },
  Advanced: { eyebrow: 'Compatibility and diagnostics', reset: 'advanced' },
}

export function Settings({ onClose }: SettingsProps) {
  const settings = useSettingsBridge()
  const { state } = settings
  const values = state.values
  const [loadscreen] = useState(chooseLaunchLoadscreen)
  const [activeCategory, setActiveCategory] = useState<SettingsCategory>('Graphics')
  const [confirmation, setConfirmation] = useState<{ id: SettingId; value: SettingValue; title: string; message: string } | null>(null)
  const contentRef = useRef<HTMLElement>(null)

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

  const selectCategory = (category: SettingsCategory) => {
    if (category === activeCategory) return

    // Both categories reuse the same scroll container, so reset it before React
    // replaces the rows instead of carrying the previous category's position.
    if (contentRef.current) contentRef.current.scrollTop = 0
    setActiveCategory(category)
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
    if (id === 'game.allowScreenUpload' && value === false && values[id]) {
      setConfirmation({ id, value, title: 'Screenshot compatibility', message: 'Some servers require screenshot uploads for anti-cheat. The chat box and MTA interface are excluded. Disable screenshot uploads anyway?' })
      return
    }
    if (id === 'game.allowExternalSounds' && value === false && values[id]) {
      setConfirmation({ id, value, title: 'External sounds', message: 'Some server scripts use internet radio and other external audio. Disabling this may reduce bandwidth use, but those sounds will no longer play. Disable them?' })
      return
    }
    if (id === 'game.customizedSAFiles' && value !== values[id]) {
      setConfirmation({ id, value, title: 'Restart required', message: 'Changing customized GTA file support requires an MTA restart and can affect server compatibility.' })
      return
    }
    if (id === 'game.discordShareData' && value === true && !values[id]) {
      setConfirmation({ id, value, title: 'Discord data sharing', message: 'Connected servers will be allowed to receive your Discord client ID and game-state information. Allow this data sharing?' })
      return
    }
    if (id === 'advanced.cpuAffinity' && value === false && values[id]) {
      setConfirmation({ id, value, title: 'Performance warning', message: 'Excluding CPU 0 is the recommended compatibility setting. Disable it only if you are troubleshooting a performance issue. Continue?' })
      return
    }
    settings.setValue(id, value)
  }

  return (
    <main className="settings" aria-label="Settings">
      <div className="settings__art" aria-hidden="true">
        <img src={loadscreen} alt="" draggable={false} />
      </div>
      <div className="settings__wash" aria-hidden="true" />
      <header className="settings__header">
        <div>
          <span className="settings__eyebrow">MTA Neon Control Center</span>
          <h1>Settings</h1>
        </div>
        <button className="settings__close" type="button" onClick={closeAndDiscard} title="Back to main menu"
          aria-label="Close settings and discard unapplied changes">
          <IconArrowLeft size={18} />
          <span>Back to main menu</span>
        </button>
      </header>

      <div className="settings__body">
        <nav className="settings__nav" aria-label="Settings categories">
          {CATEGORIES.map((category) => (
            <button
              key={category}
              type="button"
              className={category === activeCategory ? 'settings__nav-item settings__nav-item--active' : 'settings__nav-item'}
              aria-current={category === activeCategory ? 'page' : undefined}
              onClick={() => selectCategory(category)}
            >
              <span aria-hidden="true">›</span>{category}
            </button>
          ))}
        </nav>

        <section ref={contentRef} className="settings__content" aria-labelledby="settings-category-heading">
          <div className="settings__content-heading">
            <div>
              <span>{CATEGORY_COPY[activeCategory].eyebrow}</span>
              <h2 id="settings-category-heading">{activeCategory}</h2>
            </div>
            <button type="button" className="settings__reset" onClick={() => settings.reset(CATEGORY_COPY[activeCategory].reset)}>Reset {activeCategory}</button>
          </div>

          {!state.ready && <div className="settings__loading">Reading native settings…</div>}
          {state.error && <div className="settings__error" role="alert">{state.error}</div>}

          {activeCategory === 'Game' && <GameSettings state={state} onChange={setValue} onAction={settings.action} />}

          {activeCategory === 'Graphics' && <GraphicsSettings state={state} onChange={setValue} onAction={settings.action} />}

          {activeCategory === 'Audio' && <AudioSettings state={state} onChange={setValue} />}

          {activeCategory === 'Controls' && <ControlsSettings state={state} onChange={setValue} onAction={settings.action} />}

          {activeCategory === 'Interface' && <InterfaceSettings state={state} onChange={setValue} onAction={settings.action} />}

          {activeCategory === 'Neon' && <><SettingsGroup title="Extended world">
            <ToggleRow id="extendedWorld.enabled" value={Boolean(values['extendedWorld.enabled'])} onChange={setValue} />
            <RangeRow id="extendedWorld.distance" value={Number(values['extendedWorld.distance'])} min={300} max={5000} step={100} suffix=" m"
              disabled={!values['extendedWorld.enabled']} onChange={setValue} />
          </SettingsGroup>

          <SettingsGroup title="Project2DFX">
            <ToggleRow id="distantLights.enabled" value={Boolean(values['distantLights.enabled'])} onChange={setValue} />
            <ToggleRow id="distantLights.searchlights" value={Boolean(values['distantLights.searchlights'])}
              disabled={!values['distantLights.enabled']} onChange={setValue} />
            <RangeRow id="distantLights.coronaSize" value={Number(values['distantLights.coronaSize'])} min={0.1} max={1} step={0.05}
              format={(value) => `${Math.round(value * 100)}%`} disabled={!values['distantLights.enabled']} onChange={setValue} />
            <details>
              <summary>Advanced light settings</summary>
              <ToggleRow id="distantLights.autoDistance" value={Boolean(values['distantLights.autoDistance'])} disabled={!values['distantLights.enabled']} onChange={setValue} />
              {!values['distantLights.autoDistance'] && <>
            <RangeRow id="distantLights.distance" value={Number(values['distantLights.distance'])} min={300} max={5000} step={100} suffix=" m"
              disabled={!values['distantLights.enabled']} onChange={setValue} />
              </>}
              <ToggleRow id="distantLights.growWithDistance" value={Boolean(values['distantLights.growWithDistance'])} disabled={!values['distantLights.enabled']} onChange={setValue} />
              <RangeRow id="distantLights.nearAlpha" value={Number(values['distantLights.nearAlpha'])} min={0} max={1} step={0.05} disabled={!values['distantLights.enabled']} onChange={setValue} />
              <RangeRow id="distantLights.reachFullAlpha" value={Number(values['distantLights.reachFullAlpha'])} min={1} max={2000} step={1} disabled={!values['distantLights.enabled']} onChange={setValue} />
              <RangeRow id="distantLights.boostStart" value={Number(values['distantLights.boostStart'])} min={0} max={5000} step={10} disabled={!values['distantLights.enabled']} onChange={setValue} />
              <RangeRow id="distantLights.farAlphaBoost" value={Number(values['distantLights.farAlphaBoost'])} min={1} max={8} step={0.1} disabled={!values['distantLights.enabled']} onChange={setValue} />
            </details>
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

          {activeCategory === 'Advanced' && <AdvancedSettings state={state} onChange={setValue} onAction={settings.action} />}
        </section>
      </div>

      <footer className="settings__footer">
        <p>{state.dirty ? 'Unapplied changes' : state.restartRequired ? 'Applied · restart required' : state.disconnectRequired ? 'Applied · reconnect to activate browser permissions' : 'All changes applied'}</p>
        <div>
          {state.disconnectRequired && <button type="button" className="settings__button settings__button--restart" onClick={() => settings.action('disconnectNow')}>Disconnect now</button>}
          {state.restartRequired && <button type="button" className="settings__button settings__button--restart" onClick={() => settings.action('restartNow')}>Restart now</button>}
          <button type="button" className="settings__button settings__button--quiet" onClick={closeAndDiscard}>Cancel</button>
          <button type="button" className="settings__button settings__button--primary" disabled={!state.dirty || !state.ready} onClick={settings.apply}>Apply</button>
        </div>
      </footer>
      {confirmation && (
        <div className="settings-confirm" role="dialog" aria-modal="true" aria-labelledby="settings-confirm-title">
          <div className="settings-confirm__panel">
            <span>Settings // confirmation</span>
            <h2 id="settings-confirm-title">{confirmation.title}</h2>
            <p>{confirmation.message}</p>
            <div>
              <button type="button" className="settings__button settings__button--quiet" onClick={() => setConfirmation(null)}>Cancel</button>
              <button type="button" className="settings__button settings__button--primary" onClick={() => {
                settings.setValue(confirmation.id, confirmation.value)
                setConfirmation(null)
              }}>Confirm</button>
            </div>
          </div>
        </div>
      )}
    </main>
  )
}

function GameSettings({ state, onChange, onAction }: SettingsSectionProps) {
  const values = state.values
  return <>
    <SettingsGroup title="Player identity">
      <TextRow id="game.nickname" value={String(values['game.nickname'])} maxLength={22} onChange={onChange} />
      <ActionRow label="Generate a nickname" description="Creates a random valid nickname without contacting a server." button="Randomize"
        disabled={false} onAction={() => onAction('randomNickname')} />
      <ToggleRow id="game.savePasswords" value={Boolean(values['game.savePasswords'])} onChange={onChange} />
      <ToggleRow id="game.autoRefreshBrowser" value={Boolean(values['game.autoRefreshBrowser'])} onChange={onChange} />
      <ToggleRow id="game.askBeforeDisconnect" value={Boolean(values['game.askBeforeDisconnect'])} onChange={onChange} />
    </SettingsGroup>
    <SettingsGroup title="Server permissions">
      <ToggleRow id="game.allowScreenUpload" value={Boolean(values['game.allowScreenUpload'])} onChange={onChange} />
      <ToggleRow id="game.allowExternalSounds" value={Boolean(values['game.allowExternalSounds'])} onChange={onChange} />
      <ToggleRow id="game.alwaysShowTransferBox" value={Boolean(values['game.alwaysShowTransferBox'])} onChange={onChange} />
    </SettingsGroup>
    <SettingsGroup title="Presence and files">
      <ToggleRow id="game.discordRichPresence" value={Boolean(values['game.discordRichPresence'])} onChange={onChange} />
      <ToggleRow id="game.discordShareData" value={Boolean(values['game.discordShareData'])} disabled={!values['game.discordRichPresence']} onChange={onChange} />
      <ToggleRow id="game.steamStatus" value={Boolean(values['game.steamStatus'])} onChange={onChange} />
      <ToggleRow id="game.saveCameraPhotos" value={Boolean(values['game.saveCameraPhotos'])} onChange={onChange} />
      {state.availability.customizedSAFiles && <ToggleRow id="game.customizedSAFiles" value={Boolean(values['game.customizedSAFiles'])} onChange={onChange} />}
    </SettingsGroup>
    <SettingsGroup title="Player map">
      <RangeRow id="game.mapOpacity" value={Number(values['game.mapOpacity'])} min={0} max={100} step={1} suffix="%" onChange={onChange} />
      <SelectRow id="game.mapImage" value={Number(values['game.mapImage'])} options={[["Standard", 0], ["High detail", 1]]} onChange={onChange} />
    </SettingsGroup>
  </>
}

function AudioSettings({ state, onChange }: Omit<SettingsSectionProps, 'onAction'>) {
  const values = state.values
  const masterMuted = Boolean(values['audio.muteMaster'])
  return <>
    <SettingsGroup title="Volume">
      <RangeRow id="audio.masterVolume" value={Number(values['audio.masterVolume'])} min={0} max={100} step={1} suffix="%" onChange={onChange} />
      <RangeRow id="audio.radioVolume" value={Number(values['audio.radioVolume'])} min={0} max={100} step={1} suffix="%" onChange={onChange} />
      <RangeRow id="audio.sfxVolume" value={Number(values['audio.sfxVolume'])} min={0} max={100} step={1} suffix="%" onChange={onChange} />
      <RangeRow id="audio.mtaVolume" value={Number(values['audio.mtaVolume'])} min={0} max={100} step={1} suffix="%" onChange={onChange} />
      <RangeRow id="audio.voiceVolume" value={Number(values['audio.voiceVolume'])} min={0} max={100} step={1} suffix="%" onChange={onChange} />
    </SettingsGroup>
    <SettingsGroup title="Radio & user tracks">
      <ToggleRow id="audio.radioEqualizer" value={Boolean(values['audio.radioEqualizer'])} onChange={onChange} />
      <ToggleRow id="audio.radioAutotune" value={Boolean(values['audio.radioAutotune'])} onChange={onChange} />
      <ToggleRow id="audio.userTrackAutoScan" value={Boolean(values['audio.userTrackAutoScan'])} onChange={onChange} />
      <SelectRow id="audio.userTrackMode" value={Number(values['audio.userTrackMode'])}
        options={[["Radio", 0], ["Random", 1], ["Sequential", 2]]} onChange={onChange} />
    </SettingsGroup>
    <SettingsGroup title="When minimized">
      <ToggleRow id="audio.muteMaster" value={masterMuted} onChange={onChange} />
      <ToggleRow id="audio.muteRadio" value={Boolean(values['audio.muteRadio'])} disabled={masterMuted} onChange={onChange} />
      <ToggleRow id="audio.muteSfx" value={Boolean(values['audio.muteSfx'])} disabled={masterMuted} onChange={onChange} />
      <ToggleRow id="audio.muteMta" value={Boolean(values['audio.muteMta'])} disabled={masterMuted} onChange={onChange} />
      <ToggleRow id="audio.muteVoice" value={Boolean(values['audio.muteVoice'])} disabled={masterMuted} onChange={onChange} />
    </SettingsGroup>
  </>
}

function ControlsSettings({ state, onChange, onAction }: SettingsSectionProps) {
  const values = state.values
  const linkedAim = Boolean(values['controls.useMouseSensitivityForAiming'])
  return <>
    <SettingsGroup title="Mouse">
      <ToggleRow id="controls.invertMouse" value={Boolean(values['controls.invertMouse'])} onChange={onChange} />
      <ToggleRow id="controls.steerWithMouse" value={Boolean(values['controls.steerWithMouse'])} onChange={onChange} />
      <ToggleRow id="controls.flyWithMouse" value={Boolean(values['controls.flyWithMouse'])} onChange={onChange} />
      <RangeRow id="controls.mouseSensitivity" value={Number(values['controls.mouseSensitivity'])} min={0} max={100} step={1} suffix="%" onChange={onChange} />
      <ToggleRow id="controls.useMouseSensitivityForAiming" value={linkedAim} onChange={onChange} />
      <RangeRow id="controls.verticalAimSensitivity" value={Number(values['controls.verticalAimSensitivity'])} min={0} max={100} step={1}
        suffix="%" disabled={linkedAim} onChange={onChange} />
    </SettingsGroup>
    <SettingsGroup title="Joypad" caption={state.joypad.connected ? `${state.joypad.name} · axis binding and defaults are immediate actions` : 'No joypad detected — connect one and restart MTA.'}>
      <ToggleRow id="controls.classicControls" value={Boolean(values['controls.classicControls'])} onChange={onChange} />
      <RangeRow id="controls.joypadDeadZone" value={Number(values['controls.joypadDeadZone'])} min={0} max={49} step={1} suffix="%" onChange={onChange} />
      <RangeRow id="controls.joypadSaturation" value={Number(values['controls.joypadSaturation'])} min={0} max={100} step={1} suffix="%" onChange={onChange} />
      {state.joypad.axes.map((axis) => <ActionRow key={axis.index} label={axis.output}
        description={`Current controller input: ${axis.input || 'Unbound'}. Choose this row, then move an axis; Escape clears it.`}
        button={state.joypad.capturingAxis === axis.index ? 'Move an axis…' : axis.input || 'Bind axis'} disabled={!state.joypad.connected}
        onAction={() => onAction('captureJoypadAxis', String(axis.index))} />)}
    </SettingsGroup>
    <SettingsGroup title="Key bindings" caption="Select any slot, then press a keyboard, mouse or joypad button. Escape clears that slot.">
      <BindEditor rows={state.binds} capture={state.capture} onCapture={(id, slot) => onAction('captureBind', `${id}|${slot}`)} />
      <ActionRow label="Restore default bindings" description="Replaces user GTA and MTA bindings with the standard defaults."
        button="Load defaults" disabled={false} onAction={() => onAction('resetBinds')} />
    </SettingsGroup>
  </>
}

function InterfaceSettings({ state, onChange, onAction }: SettingsSectionProps) {
  const values = state.values
  return <>
    <SettingsGroup title="Language & legacy windows">
      <StringSelectRow id="interface.locale" value={String(values['interface.locale'])} disabled={state.availability.connected}
        options={state.locales.map((locale) => [locale.label, locale.code])} onChange={onChange} />
      <StringSelectRow id="interface.skin" value={String(values['interface.skin'])} disabled={state.availability.connected}
        options={state.skins.map((skin) => [skin, skin])} onChange={onChange} />
    </SettingsGroup>
    <SettingsGroup title="Chat appearance">
      <ChatPresetRow presets={state.chatPresets} onLoad={(id) => onAction('chatPreset', id)} />
      <ColorRow id="interface.chatBackgroundColor" value={Number(values['interface.chatBackgroundColor'])} onChange={onChange} />
      <ColorRow id="interface.chatTextColor" value={Number(values['interface.chatTextColor'])} minAlpha={128} onChange={onChange} />
      <ColorRow id="interface.chatInputBackgroundColor" value={Number(values['interface.chatInputBackgroundColor'])} onChange={onChange} />
      <ColorRow id="interface.chatInputTextColor" value={Number(values['interface.chatInputTextColor'])} minAlpha={128} onChange={onChange} />
      <SelectRow id="interface.chatFont" value={Number(values['interface.chatFont'])}
        options={[["Tahoma", 0], ["Verdana", 1], ["Tahoma Bold", 2], ["Arial", 3]]} onChange={onChange} />
      <RangeRow id="interface.chatLines" value={Number(values['interface.chatLines'])} min={3} max={62} step={1} onChange={onChange} />
      <RangeRow id="interface.chatScaleX" value={Number(values['interface.chatScaleX'])} min={0.5} max={3} step={0.1} format={(v) => `${v.toFixed(1)}x`} onChange={onChange} />
      <RangeRow id="interface.chatScaleY" value={Number(values['interface.chatScaleY'])} min={0.5} max={3} step={0.1} format={(v) => `${v.toFixed(1)}x`} onChange={onChange} />
      <RangeRow id="interface.chatWidth" value={Number(values['interface.chatWidth'])} min={0.5} max={4} step={0.1} format={(v) => `${v.toFixed(1)}x`} onChange={onChange} />
      <ToggleRow id="interface.chatCssText" value={Boolean(values['interface.chatCssText'])} onChange={onChange} />
      <ToggleRow id="interface.chatCssBackground" value={Boolean(values['interface.chatCssBackground'])} onChange={onChange} />
      <ToggleRow id="interface.chatNickCompletion" value={Boolean(values['interface.chatNickCompletion'])} onChange={onChange} />
      <ToggleRow id="interface.chatTextOutline" value={Boolean(values['interface.chatTextOutline'])} onChange={onChange} />
      <NumberRow id="interface.chatLineLife" value={Number(values['interface.chatLineLife'])} min={1} max={120000} step={1} suffix=" seconds" onChange={onChange} />
      <NumberRow id="interface.chatLineFadeOut" value={Number(values['interface.chatLineFadeOut'])} min={1} max={30000} step={1} suffix=" seconds" onChange={onChange} />
    </SettingsGroup>
    <SettingsGroup title="Chat layout" caption="Offsets use relative screen coordinates.">
      <SelectRow id="interface.chatPositionHorizontal" value={Number(values['interface.chatPositionHorizontal'])} options={[["Left", 0], ["Center", 1], ["Right", 2]]} onChange={onChange} />
      <SelectRow id="interface.chatPositionVertical" value={Number(values['interface.chatPositionVertical'])} options={[["Top", 0], ["Center", 1], ["Bottom", 2]]} onChange={onChange} />
      <SelectRow id="interface.chatTextAlignment" value={Number(values['interface.chatTextAlignment'])} options={[["Left", 0], ["Right", 1]]} onChange={onChange} />
      <RangeRow id="interface.chatOffsetX" value={Number(values['interface.chatOffsetX'])} min={-1} max={1} step={0.0025} format={(v) => v.toFixed(4)} onChange={onChange} />
      <RangeRow id="interface.chatOffsetY" value={Number(values['interface.chatOffsetY'])} min={-1} max={1} step={0.0025} format={(v) => v.toFixed(4)} onChange={onChange} />
    </SettingsGroup>
    <SettingsGroup title="Desktop notifications">
      <ToggleRow id="interface.flashWindow" value={Boolean(values['interface.flashWindow'])} onChange={onChange} />
      <ToggleRow id="interface.trayNotifications" value={Boolean(values['interface.trayNotifications'])} onChange={onChange} />
    </SettingsGroup>
    <SettingsGroup title="Web content & privacy" caption="CEF permissions used by browser resources. Domain lists apply after reconnecting.">
      <ToggleRow id="browser.remoteWebsites" value={Boolean(values['browser.remoteWebsites'])} onChange={onChange} />
      <ToggleRow id="browser.remoteJavascript" value={Boolean(values['browser.remoteJavascript'])} disabled={!values['browser.remoteWebsites']} onChange={onChange} />
      <ToggleRow id="browser.gpuRendering" value={Boolean(values['browser.gpuRendering'])} onChange={onChange} />
      <ToggleRow id="browser.videoAcceleration" value={Boolean(values['browser.videoAcceleration'])} onChange={onChange} />
      <DomainList title="Custom blacklist" entries={state.browserBlacklist} addLabel="Block domain"
        onAdd={(domain) => onAction('browserBlacklistAdd', domain)} onRemove={(domain) => onAction('browserBlacklistRemove', domain)}
        onClear={() => onAction('browserBlacklistClear')} />
      <DomainList title="Custom whitelist" entries={state.browserWhitelist} addLabel="Allow domain"
        onAdd={(domain) => onAction('browserWhitelistAdd', domain)} onRemove={(domain) => onAction('browserWhitelistRemove', domain)}
        onClear={() => onAction('browserWhitelistClear')} />
    </SettingsGroup>
  </>
}

function AdvancedSettings({ state, onChange, onAction }: SettingsSectionProps) {
  const values = state.values
  return <>
    <SettingsGroup title="Compatibility & performance">
      <SelectRow id="advanced.fastClothesLoading" value={Number(values['advanced.fastClothesLoading'])} options={[["Off", 0], ["Auto", 1], ["On", 2]]} onChange={onChange} />
      <SelectRow id="advanced.browserSpeed" value={Number(values['advanced.browserSpeed'])} options={[["Very slow", 0], ["Default", 1], ["Fast", 2]]} onChange={onChange} />
      <SelectRow id="advanced.singleConnection" value={Number(values['advanced.singleConnection'])} options={[["Default", 0], ["On", 1]]} onChange={onChange} />
      <SelectRow id="advanced.packetTag" value={Number(values['advanced.packetTag'])} options={[["Default", 0], ["On", 1]]} onChange={onChange} />
      <SelectRow id="advanced.progressAnimation" value={Number(values['advanced.progressAnimation'])} options={[["Off", 0], ["Default", 1]]} onChange={onChange} />
      <SelectRow id="advanced.processPriority" value={Number(values['advanced.processPriority'])} options={[["Normal", 0], ["Above normal", 1], ["High", 2]]} onChange={onChange} />
      <RangeRow id="advanced.streamingMemory" value={Number(values['advanced.streamingMemory'])} min={state.availability.streamingMemoryMin}
        max={state.availability.streamingMemoryMax} step={1} suffix=" MB" onChange={onChange} />
      <ToggleRow id="advanced.cpuAffinity" value={Boolean(values['advanced.cpuAffinity'])} onChange={onChange} />
    </SettingsGroup>
    <SettingsGroup title="Diagnostics">
      <SelectRow id="advanced.debugSetting" value={Number(values['advanced.debugSetting'])}
        options={[["Default", 0], ["#6734 Graphics", 1], ["#6732 D3D", 3], ["Log timing", 4], ["Joystick", 5], ["Lua trace", 6], ["Resize always", 7], ["Resize never", 8]]} onChange={onChange} />
      <ActionRow label="Client resource files" description={`Opens the MTA client resource cache in Windows Explorer.${state.availability.resourceCachePath ? ` Current location: ${state.availability.resourceCachePath}` : ''}`}
        button="Show in Explorer" disabled={false} onAction={() => onAction('openResourceFolder')} />
    </SettingsGroup>
    <SettingsGroup title="Updater">
      <SelectRow id="advanced.updateBuildType" value={Number(values['advanced.updateBuildType'])} options={[["Default", 0], ["Nightly", 2]]} onChange={onChange} />
      <SelectRow id="advanced.updateAutoInstall" value={Number(values['advanced.updateAutoInstall'])} options={[["Off", 0], ["Default", 1]]} onChange={onChange} />
      <ActionRow label="Check for updates" description="Starts an immediate update check using the selected channel."
        button="Check now" disabled={false} onAction={() => onAction('checkForUpdates')} />
    </SettingsGroup>
  </>
}

interface SettingsSectionProps {
  state: SettingsState
  onChange(id: SettingId, value: SettingValue): void
  onAction(name: string, argument?: string): void
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
    <SettingsGroup title="Display">
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

    <SettingsGroup title="View & HUD">
      <RangeRow id="graphics.fov" value={Number(values['graphics.fov'])} min={70} max={90} step={5} suffix="°" onChange={onChange} />
      <RangeRow id="graphics.brightness" value={Number(values['graphics.brightness'])} min={0} max={100} step={1} suffix="%" onChange={onChange} />
      <SelectRow id="graphics.aspectRatio" value={Number(values['graphics.aspectRatio'])}
        options={[["Auto", 0], ["4:3", 1], ["16:10", 2], ["16:9", 3]]} onChange={onChange} />
      <ToggleRow id="graphics.hudMatchAspectRatio" value={Boolean(values['graphics.hudMatchAspectRatio'])} onChange={onChange} />
    </SettingsGroup>

    <SettingsGroup title="Rendering quality">
      <RangeRow id="graphics.drawDistance" value={Number(values['graphics.drawDistance'])} min={0} max={100} step={1} suffix="%" onChange={onChange} />
      <SelectRow id="graphics.fxQuality" value={fxQuality}
        options={[["Low", 0], ["Medium", 1], ["High", 2], ["Very high", 3]]} onChange={onChange} />
      <SelectRow id="graphics.antiAliasing" value={Number(values['graphics.antiAliasing'])}
        options={[["Off", 1], ["1x", 2], ["2x", 3], ["3x", 4]]} onChange={onChange} />
      {state.availability.maxAnisotropic > 0 && <SelectRow id="graphics.anisotropic" value={Number(values['graphics.anisotropic'])}
        options={anisotropicOptions} onChange={onChange} />}
    </SettingsGroup>

    <SettingsGroup title="Visual effects">
      <ToggleRow id="graphics.volumetricShadows" value={Boolean(values['graphics.volumetricShadows'])} disabled={fxQuality === 0} onChange={onChange} />
      <ToggleRow id="graphics.grass" value={Boolean(values['graphics.grass'])} disabled={fxQuality === 0} onChange={onChange} />
      <ToggleRow id="graphics.dynamicPedShadows" value={Boolean(values['graphics.dynamicPedShadows'])} disabled={fxQuality < 2} onChange={onChange} />
      <ToggleRow id="graphics.heatHaze" value={Boolean(values['graphics.heatHaze'])} onChange={onChange} />
      <ToggleRow id="graphics.tyreSmoke" value={Boolean(values['graphics.tyreSmoke'])} onChange={onChange} />
      <ToggleRow id="graphics.motionBlur" value={Boolean(values['graphics.motionBlur'])} onChange={onChange} />
      <ToggleRow id="graphics.coronaReflections" value={Boolean(values['graphics.coronaReflections'])} onChange={onChange} />
    </SettingsGroup>

    <SettingsGroup title="World details">
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

    <SettingsGroup title="Compatibility">
      <ToggleRow id="graphics.dpiAware" value={Boolean(values['graphics.dpiAware'])} onChange={onChange} />
      {state.availability.unsafeResolutions && <ToggleRow id="graphics.showUnsafeResolutions" value={Boolean(values['graphics.showUnsafeResolutions'])}
        onChange={onChange} />}
      {state.availability.multiMonitor && <ToggleRow id="graphics.deviceSelectionDialog" value={Boolean(values['graphics.deviceSelectionDialog'])}
        onChange={onChange} />}
    </SettingsGroup>
  </>
}

function SettingsGroup({ title, caption, managed = false, children }: { title: string; caption?: string; managed?: boolean; children: ReactNode }) {
  return (
    <section className="settings-group">
      <header className={caption ? undefined : 'settings-group__header--compact'}>
        <div><h3>{title}</h3>{caption && <p>{caption}</p>}</div>
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

function StringSelectRow({ id, value, options, disabled = false, onChange }: RowProps & { value: string; options: Array<[string, string]> }) {
  return (
    <SettingRow id={id} disabled={disabled}>
      <select aria-label={SETTINGS_COPY[id].label} value={value} disabled={disabled} onChange={(event) => onChange(id, event.target.value)}>
        {options.map(([label, optionValue]) => <option key={optionValue} value={optionValue}>{label}</option>)}
      </select>
    </SettingRow>
  )
}

function TextRow({ id, value, maxLength, disabled = false, onChange }: RowProps & { value: string; maxLength: number }) {
  return (
    <SettingRow id={id} disabled={disabled}>
      <input className="settings-text" type="text" aria-label={SETTINGS_COPY[id].label} value={value} maxLength={maxLength} disabled={disabled}
        onChange={(event) => onChange(id, event.target.value)} />
    </SettingRow>
  )
}

function NumberRow({ id, value, min, max, step, suffix, disabled = false, onChange }: RowProps & {
  value: number; min: number; max: number; step: number; suffix?: string
}) {
  return (
    <SettingRow id={id} disabled={disabled}>
      <label className="settings-number">
        <input type="number" aria-label={SETTINGS_COPY[id].label} value={value} min={min} max={max} step={step} disabled={disabled}
          onChange={(event) => event.target.value !== '' && onChange(id, Number(event.target.value))} />
        {suffix && <span>{suffix}</span>}
      </label>
    </SettingRow>
  )
}

function ColorRow({ id, value, minAlpha = 0, disabled = false, onChange }: RowProps & { value: number; minAlpha?: number }) {
  const packed = value >>> 0
  const alpha = (packed >>> 24) & 0xff
  const rgb = packed & 0xffffff
  const color = `#${rgb.toString(16).padStart(6, '0')}`
  const setRgb = (hex: string) => onChange(id, ((alpha << 24) | Number.parseInt(hex.slice(1), 16)) >>> 0)
  const setAlpha = (nextAlpha: number) => onChange(id, ((Math.max(minAlpha, nextAlpha) << 24) | rgb) >>> 0)
  return (
    <SettingRow id={id} disabled={disabled}>
      <div className="settings-color">
        <input type="color" value={color} disabled={disabled} aria-label={`${SETTINGS_COPY[id].label} RGB`} onChange={(event) => setRgb(event.target.value)} />
        <input type="range" value={Math.max(minAlpha, alpha)} min={minAlpha} max={255} step={1} disabled={disabled} aria-label={`${SETTINGS_COPY[id].label} opacity`}
          onChange={(event) => setAlpha(Number(event.target.value))} />
        <output>{Math.round(alpha / 255 * 100)}%</output>
      </div>
    </SettingRow>
  )
}

function BindEditor({ rows, capture, onCapture }: {
  rows: SettingsBindRow[]
  capture: SettingsState['capture']
  onCapture(id: string, slot: number): void
}) {
  if (rows.length === 0) return <p className="settings-empty">No editable bindings are available.</p>
  let previousSection = ''
  return <div className="settings-binds">
    {rows.map((row) => {
      const showSection = row.section !== previousSection
      previousSection = row.section
      return <div key={row.id} className="settings-bind">
        {showSection && <h4>{row.section}</h4>}
        <span>{row.label}</span>
        <div>{[0, 1, 2, 3].map((slot) => {
          const active = capture?.bindId === row.id && capture.slot === slot
          return <button key={slot} type="button" className={active ? 'settings-bind__key settings-bind__key--capture' : 'settings-bind__key'}
            onClick={() => onCapture(row.id, slot)}>{active ? 'Press a key…' : row.keys[slot] || '—'}</button>
        })}</div>
      </div>
    })}
  </div>
}

function ChatPresetRow({ presets, onLoad }: { presets: SettingsState['chatPresets']; onLoad(id: string): void }) {
  const [selected, setSelected] = useState(presets[0]?.id ?? '')
  useEffect(() => {
    if (!presets.some((preset) => preset.id === selected)) setSelected(presets[0]?.id ?? '')
  }, [presets, selected])
  return (
    <SettingRow copy={{ label: 'Chat preset', description: 'Loads a saved chatbox preset into this draft. Review the resulting values, then choose Apply to keep them.' }} disabled={presets.length === 0}>
      <div className="settings-preset-select">
        <select aria-label="Chat preset" value={selected} disabled={presets.length === 0} onChange={(event) => setSelected(event.target.value)}>
          {presets.length === 0 && <option value="">No presets found</option>}
          {presets.map((preset) => <option key={preset.id} value={preset.id}>{preset.name}</option>)}
        </select>
        <button className="settings-action" type="button" disabled={!selected} onClick={() => onLoad(selected)}>Load</button>
      </div>
    </SettingRow>
  )
}

function DomainList({ title, entries, addLabel, onAdd, onRemove, onClear }: {
  title: string
  entries: string[]
  addLabel: string
  onAdd(domain: string): void
  onRemove(domain: string): void
  onClear(): void
}) {
  const [domain, setDomain] = useState('')
  const submit = () => {
    const value = domain.trim().toLowerCase()
    if (!value) return
    onAdd(value)
    setDomain('')
  }
  return <section className="settings-domains">
    <header><h4>{title}</h4><button type="button" disabled={entries.length === 0} onClick={onClear}>Remove all</button></header>
    <div className="settings-domains__add">
      <input type="text" value={domain} maxLength={253} placeholder="example.com" aria-label={`${title} domain`}
        onChange={(event) => setDomain(event.target.value)} onKeyDown={(event) => event.key === 'Enter' && submit()} />
      <button type="button" onClick={submit}>{addLabel}</button>
    </div>
    {entries.length > 0 && <ul>{entries.map((entry) => <li key={entry}><span>{entry}</span><button type="button" onClick={() => onRemove(entry)}>Remove</button></li>)}</ul>}
  </section>
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
    if (!visible) return
    const hideForScroll = () => {
      window.clearTimeout(delayRef.current)
      setVisible(false)
    }
    const scrollContainer = rowRef.current?.closest('.settings__content')
    scrollContainer?.addEventListener('scroll', hideForScroll, { passive: true })
    return () => scrollContainer?.removeEventListener('scroll', hideForScroll)
  }, [visible])

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
