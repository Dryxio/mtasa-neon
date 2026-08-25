export const defaultFilters = Object.freeze({
  query: '',
  side: 'all',
  severities: new Set(['debug', 'error', 'warning', 'info', 'custom']),
  resource: 'all',
})

export const zoomLevels = Object.freeze([0.9, 1, 1.1, 1.2, 1.3, 1.5])

export function normalizePreferences(preferences = {}) {
  const density = preferences.density === 'compact' ? 'compact' : 'comfortable'
  const requestedZoom = Number(preferences.zoom)
  const zoom = zoomLevels.includes(requestedZoom) ? requestedZoom : 1
  return { density, zoom }
}

export function stepZoom(current, direction) {
  const normalized = normalizePreferences({ zoom: current }).zoom
  const index = zoomLevels.indexOf(normalized)
  return zoomLevels[Math.max(0, Math.min(zoomLevels.length - 1, index + Math.sign(direction)))]
}

export function countOccurrences(events) {
  return events.reduce((total, event) => total + event.repeatCount, 0)
}

export function filterEvents(events, filters) {
  const query = filters.query.trim().toLocaleLowerCase()
  return events.filter((event) => {
    if (filters.side !== 'all' && event.side !== filters.side) return false
    if (!filters.severities.has(event.severity)) return false
    if (filters.resource !== 'all' && event.resource !== filters.resource) return false
    if (!query) return true
    const haystack = `${event.message} ${event.file} ${event.resource} ${event.category} ${event.context} ${event.correlationId}`.toLocaleLowerCase()
    return haystack.includes(query)
  })
}

export function countBySeverity(events) {
  return events.reduce((counts, event) => {
    counts[event.severity] = (counts[event.severity] ?? 0) + 1
    return counts
  }, {})
}

export function sourceLabel(event) {
  if (!event.file) return 'Source unavailable'
  return event.line ? `${event.file}:${event.line}` : event.file
}
