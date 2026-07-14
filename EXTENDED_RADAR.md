# Extended native radar tiles

MTA Neon keeps GTA San Andreas' native minimap renderer and lets client
resources register TXD tiles for the extended `[-10000, 10000)` world. No file
is added to or replaced in `models/gta3.img`.

## Lua API

```lua
local txd = engineLoadTXD("radar/my_tile.txd")
local registered = engineSetRadarMapTile(column, row, txd)

local stats = engineGetRadarMapStats()
engineResetRadarMapTile(column, row)
```

The logical grid is 40 columns by 40 rows. Each tile covers 500 world units:

```text
column = floor((worldX + 10000) / 500)
row    = ceil((9500 - worldY) / 500)
```

Columns increase west-to-east and rows increase north-to-south. Valid indices
are `0..39`. The stock 12x12 San Andreas area occupies columns `14..25` and
rows `14..25`; those 144 cells are deliberately protected and continue through
GTA's original code path.

`engineSetRadarMapTile` returns `false` for an invalid or stock cell, an invalid
TXD, a TXD created by another resource, an unsupported executable, or a cell
owned by another resource. Repeating the call from the owning resource replaces
that cell. Destroying the source TXD automatically unregisters every cell that
uses it. Stopping a resource destroys its TXD elements and therefore also cleans
up its registrations.

`engineGetRadarMapStats()` returns:

- `hooksInstalled`: whether every executable call site passed byte validation;
- `registeredTiles`: number of resource-owned cells;
- `loadedTiles`: number of decoded TXDs currently retained near the player;
- `failedTiles`: number of registered TXDs that failed deferred decoding;
- `sourceBytes`: compressed TXD bytes held by the registry.

## Runtime design

The implementation does not resize or relocate GTA's `gRadarTextures[144]`.
Eleven call sites in the US 1.0 executable are validated against their complete
five-byte `CALL` instructions before any patch is written. Nine minimap draw
calls and two radar-streaming calls are then redirected. If one byte differs,
the feature stays disabled and the game remains unmodified.

Stock cells delegate to `CRadar::DrawRadarSection` unchanged. Extended cells
reuse GTA's corner generation, polygon clipping, radar-to-screen transform,
sprite vertex builder, and RenderWare draw primitive. A missing tile is drawn
with GTA's native ocean color.

Registered TXDs remain compressed until they enter a 3x3 neighborhood around
the current radar cell. Decoded textures are retained in a 5x5 neighborhood to
avoid churn at tile boundaries.

Radar registration is independent from 3D map residency. A city resource that
must stay visible beside other cities in F11 should register its radar tiles in
`onClientResourceStart`, keep them registered while its models are released,
and reset them only in `onClientResourceStop`. The bundled VC, LC, Bullworth,
and Carcer resources follow this lifecycle, so their catalogs coexist without
keeping all four 3D worlds resident.

## F11 map

The built-in MTA F11 map consumes the same tile registry. Its world bounds are
computed from the union of the stock San Andreas block and the cells currently
registered by resources. The shorter axis is expanded to keep a square world
scale, so map imagery, radar areas, blips, the player marker, following mode,
free movement, and zoom all use the same transform without stretching. With no
custom tile registered, the bounds remain the original `[-3000, 3000]`; the
unused extended ocean is not shown.

F11 builds a single 1024x1024 or 2048x2048 GPU atlas with GTA's original
`radar00.txd` through `radar143.txd` in the stock cells, server TXDs in their
logical cells, and ocean in gaps. It reads the stock tiles through GTA's normal
`gta3.img` streamer; it neither copies them into MTA nor changes the archive.
The packaged MTA map image is used only as a temporary/failure fallback under
the native stock tiles.

Atlas generation is progressive: at most 16 cells are attempted per frame.
Stock requests are grouped into one streaming pass per batch. A tile is loaded
only long enough to draw it into the atlas, then released unless the rotating
minimap cache already owns it. The completed atlas is reused until a resource
registers, replaces, or removes a cell, the selected F11 image resolution
changes, or Direct3D resets the render target. Atlas uploads go through MTA's
existing 2D sprite renderer and unwrap GTA's proxy textures before they reach
the real Direct3D device, preserving D3D9 and D3D9On12 compatibility.

The v1 registry is global and therefore shared by all dimensions/interiors.
Per-dimension tile sets remain follow-up work.

## Test resource

See `test-resources/extended-radar-test/README.md`. Its generator extracts nine
stock radar TXDs from a local GTA installation, maps them around `(9000, 0)`,
and exercises registration, cache diagnostics, missing-tile fallback, protected
stock cells, TXD-destruction cleanup, and dynamic F11 bounds.
