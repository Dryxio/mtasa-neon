import { countBySeverity, countOccurrences, filterEvents, normalizePreferences, sourceLabel, stepZoom } from './model.mjs'

const preferenceKey = 'neon-devtools-display-v1'
const densityMetrics = {
  comfortable: { fontUi: 14, fontMeta: 12, fontCode: 13, fontSmall: 10, fontLabel: 11, rowHeight: 44, controlHeight: 38, rowY: 9, rowX: 14 },
  compact: { fontUi: 13, fontMeta: 11, fontCode: 12, fontSmall: 10, fontLabel: 10, rowHeight: 36, controlHeight: 34, rowY: 7, rowX: 12 },
}

function loadPreferences() {
  try {
    return normalizePreferences(JSON.parse(localStorage.getItem(preferenceKey) || '{}'))
  } catch {
    return normalizePreferences()
  }
}

const state = {
  events: [],
  pending: null,
  paused: false,
  capturing: false,
  captureAvailable: false,
  captureEntries: 0,
  selected: null,
  filters: { query: '', side: 'all', resource: 'all', severities: new Set(['error', 'warning', 'info', 'debug', 'custom']) },
  preferences: loadPreferences(),
}

const $ = (selector) => document.querySelector(selector)
const trigger = (name, ...args) => globalThis.mta?.triggerEvent?.(name, ...args)
const escapeHtml = (value = '') => value.replace(/[&<>'"]/g, (char) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', "'": '&#39;', '"': '&quot;' })[char])
const eventKey = (event) => String(event.entryId)
const plural = (count, singular, multiple = `${singular}s`) => `${count.toLocaleString()} ${count === 1 ? singular : multiple}`
const allSeverities = new Set(['error', 'warning', 'info', 'debug', 'custom'])
const elapsed = (timestamp) => {
  const latest = state.events.at(-1)?.timestamp ?? timestamp
  const delta = Math.max(0, latest - timestamp)
  return delta < 1000 ? 'now' : delta < 60000 ? `${Math.floor(delta / 1000)}s ago` : `${Math.floor(delta / 60000)}m ago`
}

function savePreferences() {
  try {
    localStorage.setItem(preferenceKey, JSON.stringify(state.preferences))
  } catch {
    // Display preferences are optional when CEF storage is unavailable.
  }
}

function applyPreferences() {
  const { density, zoom } = state.preferences
  const metrics = densityMetrics[density]
  const root = document.documentElement
  const scaled = (value) => `${Math.round(value * zoom * 10) / 10}px`
  root.dataset.density = density
  root.style.setProperty('--font-ui', scaled(metrics.fontUi))
  root.style.setProperty('--font-meta', scaled(metrics.fontMeta))
  root.style.setProperty('--font-code', scaled(metrics.fontCode))
  root.style.setProperty('--font-small', scaled(metrics.fontSmall))
  root.style.setProperty('--font-label', scaled(metrics.fontLabel))
  root.style.setProperty('--row-height', scaled(metrics.rowHeight))
  root.style.setProperty('--control-height', scaled(metrics.controlHeight))
  root.style.setProperty('--row-padding', `${scaled(metrics.rowY)} ${scaled(metrics.rowX)}`)
  root.style.setProperty('--title-height', scaled(62))
  root.style.setProperty('--toolbar-height', scaled(64))
  root.style.setProperty('--severity-height', scaled(46))
  root.style.setProperty('--footer-height', scaled(60))
  root.style.setProperty('--columns-height', scaled(36))
  $('#density').value = density
  $('#zoomReset').textContent = `${Math.round(zoom * 100)}%`
}

function setDensity(density) {
  state.preferences = normalizePreferences({ density, zoom: state.preferences.zoom })
  applyPreferences()
  savePreferences()
}

function setZoom(zoom) {
  state.preferences = normalizePreferences({ density: state.preferences.density, zoom })
  applyPreferences()
  savePreferences()
}

function fullDiagnostic(event) {
  const context = event.context ? `\nContext: ${event.context}` : ''
  const correlation = event.correlationId ? `\nCorrelation: ${event.correlationId}` : ''
  return `[${event.side.toUpperCase()}] [${event.severity.toUpperCase()}] ${event.resource || '—'}\n${sourceLabel(event)}${context}\n${event.message}\nOccurrences: ${event.repeatCount}${correlation}`
}

function customColorStyle(event) {
  if (event.severity !== 'custom') return ''
  const color = [event.red, event.green, event.blue].map((value) => Math.max(0, Math.min(255, Number(value) || 0)))
  return ` style="--diagnostic-color:rgb(${color.join(',')})"`
}

async function copyText(value, successMessage) {
  try {
    if (!navigator.clipboard?.writeText) throw new Error('Clipboard API unavailable')
    await navigator.clipboard.writeText(value)
  } catch {
    const input = document.createElement('textarea')
    input.value = value
    input.style.position = 'fixed'
    input.style.opacity = '0'
    document.body.append(input)
    input.select()
    document.execCommand('copy')
    input.remove()
  }
  toast(successMessage)
}

function render() {
  const filtered = filterEvents(state.events, state.filters)
  const scoped = filterEvents(state.events, { ...state.filters, severities: allSeverities })
  const counts = countBySeverity(scoped)
  const occurrences = countOccurrences(filtered)
  const visibleCount = Math.min(filtered.length, 1000)
  document.querySelectorAll('[data-severity]').forEach((button) => { button.querySelector('b').textContent = counts[button.dataset.severity] ?? 0 })
  $('#resultCount').textContent = `${filtered.length > visibleCount ? `Showing newest ${visibleCount.toLocaleString()} of ${filtered.length.toLocaleString()} entries` : plural(filtered.length, 'entry', 'entries')} · ${plural(occurrences, 'occurrence')}`
  $('#empty').hidden = filtered.length > 0
  $('#emptyTitle').textContent = state.events.length ? 'No matching diagnostics' : 'No diagnostics yet'
  $('#emptyMessage').textContent = state.events.length ? 'Adjust your search or filters to show diagnostics.' : 'Real script errors, warnings and debug messages will appear here.'
  $('#rows').innerHTML = filtered.slice(-1000).reverse().map((event) => `
    <button class="row ${event.severity} ${state.selected === eventKey(event) ? 'selected' : ''}" data-entry-id="${eventKey(event)}">
      <time>${elapsed(event.timestamp)}</time>
      <span class="side ${event.side}">${event.side}</span>
      <span class="source"><strong>${escapeHtml(event.resource || '—')}</strong><small>${escapeHtml(sourceLabel(event))}</small></span>
      <span class="message"${customColorStyle(event)}><i></i><span>${escapeHtml(event.message)}</span>${event.repeatCount > 1 ? `<b>×${event.repeatCount}</b>` : ''}</span>
    </button>`).join('')
  renderDetails()
  $('#connection').className = `status ${state.paused ? 'paused' : 'live'}`
  const pendingOccurrences = state.pending ? Math.max(0, countOccurrences(state.pending.events) - countOccurrences(state.events)) : 0
  $('#connection').innerHTML = `<i></i> ${state.paused ? `VIEW FROZEN${pendingOccurrences ? ` · ${pendingOccurrences.toLocaleString()} NEW` : ''}` : 'VIEW ACTIVE'}`
  $('#pause').textContent = state.paused ? 'Resume view' : 'Freeze view'
  $('#capture').classList.toggle('active', state.capturing)
  $('#capture').innerHTML = `<i></i> ${state.capturing ? 'Stop capture' : 'Capture diagnostics'}`
  $('#captureHint').textContent = state.capturing
    ? 'Capturing grouped diagnostics for up to 2 minutes'
    : state.captureAvailable
      ? `${plural(state.captureEntries, 'captured entry', 'captured entries')} ready to export`
      : 'Includes the previous 30 seconds'
  $('#discardCapture').hidden = state.capturing || !state.captureAvailable
  $('#exportTxt').textContent = state.captureAvailable ? 'Export capture TXT' : 'Export history TXT'
  $('#exportJson').textContent = state.captureAvailable ? 'Export capture JSON' : 'Export history JSON'
}

function renderDetails() {
  const event = state.events.find((candidate) => eventKey(candidate) === state.selected)
  if (!event) {
    $('#details').innerHTML = '<div class="details-empty"><span>↖</span><p>Select an event to inspect its diagnostic details.</p></div>'
    return
  }
  const optionalContext = event.context ? `<dt>Context</dt><dd>${escapeHtml(event.context)}</dd>` : ''
  const optionalCategory = event.category && event.category !== 'script' ? `<dt>Category</dt><dd>${escapeHtml(event.category)}</dd>` : ''
  const optionalCorrelation = event.correlationId ? `<dt>Correlation</dt><dd>${escapeHtml(event.correlationId)}</dd>` : ''
  const seen = event.repeatCount > 1
    ? `<dt>First seen</dt><dd>${elapsed(event.firstSeen ?? event.timestamp)}</dd><dt>Last seen</dt><dd>${elapsed(event.lastSeen ?? event.timestamp)}</dd>`
    : `<dt>Seen</dt><dd>${elapsed(event.timestamp)}</dd>`
  const copyLocation = event.file ? '<button id="copyLocation">Copy location</button>' : ''
  $('#details').innerHTML = `
    <div class="details-head"><span class="severity ${event.severity}">${event.severity}</span><button id="copyEvent">Copy diagnostic</button></div>
    <h2>${escapeHtml(event.message)}</h2>
    <dl>
      <dt>Side</dt><dd>${event.side}</dd><dt>Resource</dt><dd>${escapeHtml(event.resource || '—')}</dd>
      <dt>Source</dt><dd>${escapeHtml(sourceLabel(event))}</dd>${optionalContext}${optionalCategory}
      <dt>Occurrences</dt><dd>${event.repeatCount}</dd>${seen}${optionalCorrelation}
    </dl>
    <div class="details-actions">${copyLocation}</div>`
  $('#copyEvent').onclick = () => copyText(fullDiagnostic(event), 'Diagnostic copied')
  if (event.file) $('#copyLocation').onclick = () => copyText(sourceLabel(event), 'Source location copied')
}

function receive(snapshot) {
  if (state.paused) {
    state.pending = snapshot
    state.capturing = snapshot.capturing
    state.captureAvailable = snapshot.captureAvailable
    state.captureEntries = snapshot.captureEntries
    render()
  }
  else {
    state.events = snapshot.events
    state.capturing = snapshot.capturing
    state.captureAvailable = Boolean(snapshot.captureAvailable)
    state.captureEntries = snapshot.captureEntries ?? 0
    $('#dropped').textContent = snapshot.dropped ? `${snapshot.dropped} old events dropped` : ''
    refreshResources()
    render()
  }
}

function refreshResources() {
  const select = $('#resource')
  const current = state.filters.resource
  const resources = [...new Set(state.events.map((event) => event.resource).filter(Boolean))].sort()
  select.innerHTML = '<option value="all">All resources</option>' + resources.map((resource) => `<option value="${escapeHtml(resource)}">${escapeHtml(resource)}</option>`).join('')
  select.value = resources.includes(current) ? current : 'all'
  state.filters.resource = select.value
}

function toast(message) {
  $('#toast').textContent = message
  $('#toast').classList.add('visible')
  setTimeout(() => $('#toast').classList.remove('visible'), 2400)
}

globalThis.__neonDevTools = { receive, exported: (path) => toast(`Saved to ${path}`) }
$('#search').oninput = (event) => { state.filters.query = event.target.value; render() }
$('#side').onchange = (event) => { state.filters.side = event.target.value; render() }
$('#resource').onchange = (event) => { state.filters.resource = event.target.value; render() }
$('#density').onchange = (event) => setDensity(event.target.value)
$('#zoomOut').onclick = () => setZoom(stepZoom(state.preferences.zoom, -1))
$('#zoomReset').onclick = () => setZoom(1)
$('#zoomIn').onclick = () => setZoom(stepZoom(state.preferences.zoom, 1))
$('#severityBar').onclick = (event) => {
  const button = event.target.closest('[data-severity]')
  if (!button) return
  const severity = button.dataset.severity
  state.filters.severities.has(severity) ? state.filters.severities.delete(severity) : state.filters.severities.add(severity)
  button.classList.toggle('active', state.filters.severities.has(severity))
  render()
}
$('#rows').onclick = (event) => { const row = event.target.closest('[data-entry-id]'); if (row) { state.selected = row.dataset.entryId; render() } }
$('#pause').onclick = () => { state.paused = !state.paused; if (!state.paused && state.pending) { const pending = state.pending; state.pending = null; receive(pending) } else render() }
$('#clear').onclick = () => trigger('devtools:clear')
$('#close').onclick = () => trigger('devtools:close')
$('#capture').onclick = () => trigger(state.capturing ? 'devtools:capture-stop' : 'devtools:capture-start')
$('#discardCapture').onclick = () => trigger('devtools:capture-discard')
$('#exportTxt').onclick = () => trigger('devtools:export', 'txt')
$('#exportJson').onclick = () => trigger('devtools:export', 'json')
addEventListener('keydown', (event) => {
  if (event.key === 'Escape') trigger('devtools:close')
  if (event.ctrlKey && event.key.toLowerCase() === 'f') { event.preventDefault(); $('#search').focus() }
  if (event.ctrlKey && (event.key === '+' || event.key === '=')) { event.preventDefault(); setZoom(stepZoom(state.preferences.zoom, 1)) }
  if (event.ctrlKey && event.key === '-') { event.preventDefault(); setZoom(stepZoom(state.preferences.zoom, -1)) }
  if (event.ctrlKey && event.key === '0') { event.preventDefault(); setZoom(1) }
})

applyPreferences()
trigger('devtools:ready')
