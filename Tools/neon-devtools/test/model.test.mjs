import assert from 'node:assert/strict'
import test from 'node:test'
import { countBySeverity, countOccurrences, filterEvents, normalizePreferences, sourceLabel, stepZoom } from '../src/model.mjs'

const events = [
  { side: 'server', severity: 'error', resource: 'inventory', file: 'server.lua', line: 12, message: 'nil value', category: 'script', context: 'event handler: inventory:take', correlationId: '', repeatCount: 3 },
  { side: 'client', severity: 'warning', resource: 'hud', file: 'client.lua', line: 5, message: 'bad argument', category: 'script', context: 'timer callback', correlationId: '', repeatCount: 1 },
]

test('filters combine side, severity, resource and full-text search', () => {
  const result = filterEvents(events, { query: 'NIL', side: 'server', severities: new Set(['error']), resource: 'inventory' })
  assert.deepEqual(result, [events[0]])
})

test('severity counters report distinct entries while occurrences remain separate', () => {
  assert.deepEqual(countBySeverity(events), { error: 1, warning: 1 })
})

test('source labels include actionable line numbers', () => {
  assert.equal(sourceLabel(events[0]), 'server.lua:12')
})

test('occurrence totals distinguish grouped entries from repeats', () => {
  assert.equal(countOccurrences(events), 4)
})

test('display preferences default safely and preserve supported values', () => {
  assert.deepEqual(normalizePreferences(), { density: 'comfortable', zoom: 1 })
  assert.deepEqual(normalizePreferences({ density: 'compact', zoom: 1.2 }), { density: 'compact', zoom: 1.2 })
  assert.deepEqual(normalizePreferences({ density: 'unknown', zoom: 99 }), { density: 'comfortable', zoom: 1 })
})

test('zoom stepping clamps to the supported range', () => {
  assert.equal(stepZoom(1, 1), 1.1)
  assert.equal(stepZoom(1, -1), 0.9)
  assert.equal(stepZoom(1.5, 1), 1.5)
  assert.equal(stepZoom(0.9, -1), 0.9)
})
