# Native GTA interfaces

Client resources can register their own mission text, display GTA mission clocks,
counters and gauges, compose script text/windows/cards, and open native list or
color-grid menus. All draws use GTA's routines. The API has no SCM pointers,
script offsets, purchases, rewards or game rules.

The implementation has headless tests and a VM compilation workflow. Visual
fidelity, input behavior and lifecycle in a running game must be checked with
`native-ui-test`; a successful compilation is not visual validation.

## API and ownership

```lua
handle = createNativeUI(kind, options)          -- or false, reason
true = updateNativeUI(handle, changedOptions)  -- or false, reason
true = destroyNativeUI(handle)                 -- or false, reason
state = getNativeUIState(handle)               -- or false, reason
true = releaseNativeUI()                       -- only the calling resource
true = showNativeText(textHandle, channel, durationMs [, style=1, number1=-1, number2=-1])
true = clearNativeText(channel)                 -- objective/dialogue/help/big/all
```

Handles are opaque positive integers, unique for the lifetime of the client
process. They are never recycled after destroy, resource restart or disconnect.
The caller must own a handle and every referenced text. Resources may use the
same text name independently. Creating a duplicate name within one resource
fails; update the existing handle instead. Handles cannot be used by an exported
function running in another resource to mutate the original owner's objects.

`updateNativeUI` merges the supplied fields with the current options. Text names
are immutable. Updating registered content cancels its active mission messages;
menus, counters and positioned draws resolve the new content on the next draw.
Destroying a text that another owned UI object references returns `text-in-use`.
Destroy its consumers first, or call `releaseNativeUI` to release the whole set.
Resource destruction (including restart/disconnect) releases the owned set.

Death, respawn and context changes do not implicitly destroy the interface.
The activity must call `releaseNativeUI`, destroy its handles, or set
`visible=false` according to its own lifecycle. No preference/bind is modified.

Every client has a total budget of 256 objects (including text), 1 clock,
4 counters, 2 menus including grids, and 96 drawing commands including cards,
windows and rectangles. Hidden objects still reserve capacity. Native slots
already occupied by GTA are refused at creation. A later native takeover suspends
the affected object and sets its state `available=false`; it does not overwrite
GTA. There is no capacity increase.

Wrong types, unknown option names, sparse arrays, invalid handles, non-finite
numbers and out-of-range values return `false, reason`. Common reasons include
`invalid-handle`, `invalid-text-handle`, `capacity`, `native-capacity`,
`channel-busy`, `native-channel-busy`, `legacy-gxt-channel-busy`,
`unsupported-text-or-token`, and `unsupported-native-hooks`.

## Registered text and mission messages

```lua
local objective = assert(createNativeUI('text', {
    name='delivery-objective',
    content='~y~Livraison~s~ : rejoignez le café. ~k~~PED_SPRINT~',
}))
assert(showNativeText(objective, 'objective', 8000))
```

A `text` uses `name` and `content`. Set `gxt=true` to copy an existing GXT key
(maximum 7 alphanumeric/underscore characters) into resource-owned storage.
The needed mission block must already be loaded. Later GXT reloads cannot
invalidate this copy. Existing `acquireMissionText` and `showMission*` APIs
remain available. The old GXT lease and the new mission message family are
mutually exclusive: release the old lease before showing custom messages and
clear custom messages before acquiring the old lease.

Custom content accepts printable ASCII and the European GTA atlas characters
`ÀÁÂÄÆÇÈÉÊËÌÍÎÏÒÓÔÖÙÚÛÜßàáâäæçèéêëìíîïòóôöùúûüÑñ¿`.
Use UTF-8 input; Neon converts to GTA atlas bytes. Other scripts, emoji, malformed
UTF-8 and ASCII `[`, `]`, `^` are rejected. `<` and `>` map to GTA's arrow slots.
This is not arbitrary Unicode support. Resources choose their translated text;
Neon does not infer language, translate strings or replace the font atlas.

The registration limit is 160 encoded bytes, including tokens. Newline becomes
`~n~`. Supported tokens are `~r~ ~g~ ~b~ ~w~ ~y~ ~p~ ~s~ ~h~ ~n~ ~z~`,
and `~1~` / `~2~` for explicit numbered values (at most six occurrences).
Two key tokens at most may use:

- `~k~~PED_FIREWEAPON~`, `~k~~PED_SPRINT~`, `~k~~PED_JUMPING~`;
- `~k~~VEHICLE_ENTER_EXIT~`, `~k~~VEHICLE_ACCELERATE~`, `~k~~VEHICLE_BRAKE~`;
- `~k~~PED_DUCK~`, `~k~~PED_LOCK_TARGET~`.

Keys are resolved through GTA's effective controller configuration. Message keys
are resolved when shown; positioned draws resolve them each frame. Oversized
expanded mission text is refused (120 bytes for big text, 300 otherwise).
Oversized dynamically rebound drawing text is suppressed rather than passed to
GTA's fixed buffer. This fallback should be checked with your actual binds.

`objective` and `dialogue` share the subtitle queue. An objective strips an
initial `~z~` and is not hidden by the spoken-subtitle preference. Dialogue
respects that preference. Help uses the native help presentation for the
requested duration. Big messages accept styles 1–7. Duration is 1–600000 ms.
One resource leases the message family while any of its messages remain active;
another resource receives a refusal. No global queue clear is used. Strings stay
alive until owned queue/history references have been detached.

## Clock and counters

```lua
local time = assert(createNativeUI('clock', {
    text=label, value=60000, countdown=true, paused=false, beepSeconds=12,
}))
local progress = assert(createNativeUI('counter', {
    text=label, style=1, value=25, maximum=100, color=1, flash=true,
}))
```

Clock `value` is milliseconds, 0–5999999. It counts down by default; use
`countdown=false` for elapsed time. `paused` is explicit. Wall-clock elapsed time
is applied between pulses, including slow frames; a zero countdown latches
`finished`. Updating it to a positive value resumes its display, but the
`finished` event is emitted at most once per handle. Create a new clock for a
new activity that needs another completion notification. Countdown beeps use the
native frontend sound, at most once per crossed pulse, below `beepSeconds`
(0–60; zero disables them). GTA widescreen suppresses the beep as in the solo.
HUD visibility affects drawing, not the clock. Menu/chat visibility does not
implicitly pause the clock.

Counters use `style=0` for a number, `1` for a percentage gauge normalized from
`value/maximum`, and `2` for `value / maximum`. Values are nonnegative and fit a
signed 32-bit integer; maximum must be positive. A gauge rejects value above
maximum. `color` is a native HUD color ID, 0–14; `flash` enables the native
first-display effect. Clock/counter `text` is an optional label handle.
Their positions and flashing follow GTA's mission HUD, including its native
layout quirks; x/y do not reposition this HUD family.

Send initial values and corrections from the server using ordinary MTA events.
Sequence/revision checks belong to the activity. A client `finished` event is a
presentation notification, never evidence for payment or mission success.

## Menus and input

```lua
local menu = assert(createNativeUI('menu', {
    text=title, x=80, y=120, columns=2, width=150,
    headers={serviceHeader, priceHeader},
    cells={repairLabel, repairPrice, paintLabel, paintPrice}, -- row-major
    enabled={true, false}, selected=1,
    widths={230, 100}, alignments={1, 2},
}))
local grid = assert(createNativeUI('grid', {
    x=180, y=120, width=220, columns=8, selected=1,
}))
```

Lists have 1–4 columns and 1–12 rows. `cells` is a dense row-major text-handle
array. Optional `enabled` defaults to all true. At least one row must be enabled;
the selected row must be enabled. `headers`, `widths`, `alignments` are optional
per-column arrays. Total column width cannot exceed 640. Alignment uses GTA's
values: 0 center, 1 left, 2 right. Grids have 1–8 columns and columns² cells, up
to 64 palette colors. The returned color is the GTA vehicle color ID, not RGBA.

The oldest visible available owned menu has input focus. The second can remain
visible. Selection is one-based in Lua. Navigation uses native pad processing;
accept uses the effective sprint/accelerate button, cancel uses enter/exit.
A new focus must observe release before a held accept/cancel can act. On accept
or cancel the menu hides and releases input consumption, retaining its handle
and capacity until destroy. Use `visible=true` to reopen it. Updating a menu discards pending results and
requires a new input edge.

The grid's retail accept branch indexes its 12-row enabled array with a 0–63
color index. Neon keeps native grid navigation/rendering and validates grid
acceptance separately to avoid that unsafe branch. No saved controls are
changed: only the effective local player input frame is consumed. MTA chat,
console and main menu suspend native-menu input.

`getNativeUIState` returns `kind`, `value`, `visible`, `paused`, `finished`,
`selected`, `accepted`, `cancelled`, `color`, `available`. Reading consumes the accepted and
cancelled polling flags. Zero means no row/result; color -1 means no grid color.

```lua
addEventHandler('onClientNativeUI', resourceRoot,
    function(handle, action, selection, color)
        -- action: accepted, cancelled, finished
        -- Validate against the activity's current handle before responding.
    end)
```

The event is local and emitted outside native input/render calls. Events whose
handle/resource was released are discarded. Polling and events are alternative
ways to observe a result; polling does not remove the queued event.

## Positioned composition

Kinds `drawText`, `window`, `rectangle`, and `card` share x/y/width/height,
`visible`, and `beforeFade` (default true). Coordinates are script-space
640×448. GTA's current HUD X/Y multipliers perform conversion, including MTA's
HUD aspect setting. There is no additional DX normalization/safe-area pass.

`drawText` requires `text` and supports `scaleX`, `scaleY`, `color`, `font`
(0 gothic, 1 subtitles, 2 menu), `alignment`, `width`, `shadow`, `outline`,
`dropColor`, `proportional`, `number1`, `number2`. Scale is 0.01–4; shadow and
outline are 0–4. Script text applies the original vertical scale division by
2. Width means wrapping position for left/right alignment and center width for
centered text. Colors are **0xRRGGBBAA**, unlike MTA DX's ARGB convention.

A `window` requires a title `text`; its native title font and inset remain
GTA's `DrawWindow` defaults. `background` is RGBA (default 0x000000BE).
A `rectangle` draws that solid background through `CSprite2d::DrawRect`.
A `card` draws one of GTA's 53 `LD_CARD.txd` sprites, selected by `card=1..53`:
13 clubs, 13 diamonds, 13 spades, 13 hearts, then the back. Texture files are
read privately from the installed game; no GTA assets are shipped in the test
resource. Failure to load that dictionary returns a texture error.

Each before/after-fade pass draws owned windows/rectangles/cards in creation
order, then script text in creation order, then menus. GTA's existing script
pass runs first. These are bounded composition primitives, not a general
RenderWare renderer. Native font state, script arrays and menu-global pointers
are restored after each call. The test scene deliberately combines Wager,
values, scores and cards so coordinate errors are visible together.

## Test resource

Start `native-ui-test` and `native-ui-test-peer`; use `/nui help`. The resource
README contains the manual matrix and its automatic assertions. No scene starts
automatically, no graphical client is launched by the test runner, and no test
modifies the player's saved preferences.
