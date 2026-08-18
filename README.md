<p align="center">
  <a href="https://mtasa-neon-wiki.vercel.app/neon"><img src="docs/media/neon-readme-banner.png" alt="MTA:SA Neon Engine: native AI, extended worlds, and new Lua APIs" width="100%"></a>
</p>

<p align="center">
  <a href="https://mtasa-neon-wiki.vercel.app/neon"><img alt="Open the Neon documentation and Lua API" src="https://img.shields.io/badge/DOCUMENTATION_%26_LUA_API-OPEN_THE_NEON_WIKI-F28C18?style=for-the-badge&amp;labelColor=0A0908" height="40"></a>
</p>

<p align="center">
  <a href="https://github.com/Dryxio/mtasa-neon/releases/latest/download/MTA-Neon-Setup.exe"><img alt="Download the latest MTA Neon client for Windows" src="https://img.shields.io/badge/WINDOWS_CLIENT-DOWNLOAD_INSTALLER-F28C18?style=for-the-badge&amp;logo=windows&amp;logoColor=F28C18&amp;labelColor=0A0908" height="40"></a>
  <a href="https://github.com/Dryxio/mtasa-neon/releases/latest/download/MTA-Neon-Server-Windows-x64.zip"><img alt="Download the latest MTA Neon server for Windows x64" src="https://img.shields.io/badge/WINDOWS_SERVER-DOWNLOAD_X64_ZIP-F28C18?style=for-the-badge&amp;logo=windows&amp;logoColor=F28C18&amp;labelColor=0A0908" height="40"></a>
</p>

<p align="center"><strong>Players only need the Windows client installer. Server owners should use the separate server package. Both links track the latest explicitly published Neon release.</strong></p>

<p align="center"><strong>New to Neon? Start with the documentation, feature guides, and complete Lua API.</strong></p>

<p align="center">
  <a href="https://discord.com/invite/mgFRd2AzF8"><img alt="Join the Neon Discord" src="https://img.shields.io/badge/DISCORD-JOIN_NEON-F28C18?style=for-the-badge&amp;logo=discord&amp;logoColor=F28C18&amp;labelColor=0A0908"></a>
  <a href="https://github.com/multitheftauto/mtasa-blue"><img alt="View the MTA:SA upstream project" src="https://img.shields.io/badge/UPSTREAM-MTA%3ABLUE-F28C18?style=for-the-badge&amp;logo=github&amp;logoColor=F28C18&amp;labelColor=0A0908"></a>
</p>

<p align="center"><strong>An independent MTA:BLUE-derived playground for deeper GTA:SA engine work.</strong></p>

MTA:SA Neon is an experimental fork of [Multi Theft Auto: San Andreas](https://github.com/multitheftauto/mtasa-blue) that opens up parts of the GTA:SA engine that were never exposed to scripts: a bigger world, higher engine limits, and GTA's own native systems turned into Lua primitives.

Its flagship system brings **GTA:SA's single-player NPCs and traffic into a shared multiplayer world**, running the original AI on one authoritative client.

## See it

Neon renders all of this itself. Nothing to install and no ASI files: every option is a toggle in the settings menu, and servers can drive them too.

**Draw distance**

![Rooftop view over Los Santos: distant buildings are flat low-detail blobs in MTA:SA and resolve into detailed geometry in Neon](docs/media/compare-draw-distance.jpg)

**Distant lights at night**

![Night view over the city: fogged out beyond a short distance in MTA:SA, full skyline with hundreds of distant lights in Neon](docs/media/compare-project2dfx.jpg)

**PS2-style color**

![Street-level view in Ganton: neutral colors in MTA:SA, warmer and more saturated PS2-style grading in Neon](docs/media/compare-skygfx.jpg)

Neon also ships its own GTA:SA-inspired menu and server browser, with an optional Discord identity.

![Neon main menu in the GTA:SA visual style, with browse servers, quick connect, map editor, settings, about and quit entries, and a Discord connected badge](docs/media/neon-main-menu.jpg)

![Neon server browser showing community servers with flags, ping and player counts, and a details panel for the selected server](docs/media/neon-server-browser.jpg)

## GTA systems, now scriptable

Eight systems that used to be locked inside the engine. Each clip is a real in-game recording.

| | |
| :--: | :--: |
| [![Native CULL-zone editing](docs/media/native-cull-zone-demo.png)](https://www.youtube.com/watch?v=17QrE21uDgM)<br>**[Native CULL zones](https://mtasa-neon-wiki.vercel.app/neon/rendering-and-limits)**<br>Edit GTA's culling from Lua | [![Scripting GTA's own physics objects](docs/media/world-object-scripting-demo.png)](https://mtasa-neon-wiki.vercel.app/neon/world-objects)<br>**[Dynamic world objects](https://mtasa-neon-wiki.vercel.app/neon/world-objects)**<br>Track, move, damage and break San Andreas' own props |
| [![Collision generated at runtime](docs/media/runtime-collision-demo.png)](https://mtasa-neon-wiki.vercel.app/neon/runtime-collision)<br>**[Runtime collision](https://mtasa-neon-wiki.vercel.app/neon/runtime-collision)**<br>Build collision shapes from Lua, no `.col` file | [![Custom foliage](docs/media/foliage-demo.png)](https://mtasa-neon-wiki.vercel.app/neon/foliage)<br>**[Custom foliage](https://mtasa-neon-wiki.vercel.app/neon/foliage)**<br>Grow GTA's own vegetation anywhere |
| [![Managed fire following a car](docs/media/fire-demo.png)](https://mtasa-neon-wiki.vercel.app/neon/fire)<br>**[Managed fire](https://mtasa-neon-wiki.vercel.app/neon/fire)**<br>Synchronized fires that keep their identity and follow a target | [![A retextured SA-MP interior](docs/media/samp-map-demo.png)](https://mtasa-neon-wiki.vercel.app/neon/samp-maps)<br>**[SA-MP maps](https://mtasa-neon-wiki.vercel.app/neon/samp-maps)**<br>Load Pawn exports directly, retextured material slots included |
| [![A Lua-controlled flock of birds](docs/media/bird-demo.png)](https://mtasa-neon-wiki.vercel.app/neon/birds)<br>**[Scriptable birds](https://mtasa-neon-wiki.vercel.app/neon/birds)**<br>Steerable flocks with their own renderer, past GTA's six slots | [![A runway of GTA lamp posts running a colour wave](docs/media/2dfx-demo.png)](https://mtasa-neon-wiki.vercel.app/neon/model-2dfx)<br>**[Model 2DFX effects](https://mtasa-neon-wiki.vercel.app/neon/model-2dfx)**<br>Drive the lights baked into GTA models, no custom assets |

```lua
-- Build a wall's collision from a Lua table, then change it live
local col = engineLoadCOL({
    boxes = { { position = { 0, 0, 0 }, size = { 6, 0.5, 2 }, material = 1 } },
})
engineReplaceCOL(col, modelId)

-- Grow GTA's own grass inside a triangle
local patch = createFoliage(v1, v2, v3, 10, 1.5)
patch.density = 2.0

-- Light a synchronized fire, then move it onto a car while it burns
local fire = createFire(x, y, z, { duration = 10000, strength = 1.5 })
setFireTarget(fire, theVehicle)
```

## Play

Download the Windows client installer, run it, and join a server from the Neon browser. Neon checks for newer signed releases on launch and offers them as optional updates.

Server owners use the separate server package. Both downloads are linked at the top of this page.

## What changes versus MTA:SA

| Area | MTA:SA | MTA:SA Neon |
| --- | ---: | ---: |
| Ambient NPCs and traffic | Local only, disabled by MTA | Server-owned peds running GTA's native AI, with one syncer and handoff |
| World size | Approximately -3,000 to +3,000 | -10,000 to +9,999, with matching radar and F11 map |
| GTA corona pool | 64 | 4,096 |
| GTA 3D marker pool | 32 | 4,096 |
| Visible entity pointers | 1,000 | 8,192 |
| Collision authoring | A packaged `.col` file | Generated from a Lua table and rebuildable live |
| Vegetation placement | Whatever the map contains | Resource-owned foliage driving GTA's plant manager |
| Fire | Client-only, returns a boolean, no handle | Synchronized `fire` elements you can retarget and change while burning |
| Ambient birds | Six native slots, no script access | Resource-owned `bird` elements you steer, restyle and shoot; 128 verified at once |
| Model 2DFX effects | Baked into models, not exposed | Read, edit, add and remove them from Lua, with per-resource rollback |
| Distant lights and draw distance | Not integrated | Built in, 300 to 5,000 units, off by default |

That is the short list. The [full comparison table](https://mtasa-neon-wiki.vercel.app/neon/features) covers every pool, boundary and subsystem.

## For scripters

Neon adds **239 documented Lua functions** on top of MTA's API, plus new elements and events. A few, to give the shape of it:

```lua
engineSetRadarMapTile(column, row, txd)        -- resource-owned extended radar tiles
engineCreateCullZone(...)                      -- native CULL zones from Lua
createFoliage(v1, v2, v3, surface, density)    -- GTA's native vegetation
engineSetCOLData(col, collisionTable)          -- rebuild collision in place
getAmbientPedSpawnCandidate(origin, "cop")     -- ask GTA where a ped belongs
setFireTarget(fire, vehicle)                   -- a burning fire follows an element
createBird(x, y, z, { preset = "desert" })     -- flocks with their own renderer
setModel2DFXProperty(1226, 0, "color", c)      -- recolour every lamp post of a model
```

The **[Neon Lua API](https://mtasa-neon-wiki.vercel.app/neon/functions)** is the complete reference, with signatures, lifecycle rules, source commits and test evidence for every entry.

## Documentation

| | |
| --- | --- |
| [Neon wiki](https://mtasa-neon-wiki.vercel.app/neon) | Everything: guides, Lua API, evidence |
| [Synchronized NPCs](https://mtasa-neon-wiki.vercel.app/neon/synchronized-ai) | How shared native AI works |
| [Extended world](https://mtasa-neon-wiki.vercel.app/neon/extended-world) | Boundaries, radar, water, seabed |
| [Tooling and verification](https://mtasa-neon-wiki.vercel.app/neon/tooling-and-verification) | What is actually proven, and what is not |
| [BUILDING.md](./BUILDING.md) | Compiling the client and server |

Deeper technical notes live in [LIMIT_PATCHING.md](./LIMIT_PATCHING.md), [EXTENDED_RADAR.md](./EXTENDED_RADAR.md), [STORY_RUNTIME.md](./STORY_RUNTIME.md), [ENTITY_PERFORMANCE.md](./ENTITY_PERFORMANCE.md) and [MULTI_CLIENT.md](./MULTI_CLIENT.md).

## Upstream relationship

Neon is built on [Multi Theft Auto: San Andreas](https://github.com/multitheftauto/mtasa-blue) and preserves its complete history and GPLv3 licensing. Neon-specific experiments and builds are maintained independently; use the upstream project for official MTA:SA downloads, documentation, and support.

Neon is not affiliated with or endorsed by the Multi Theft Auto team.

## License

Unless otherwise specified, all source code hosted on this repository is licensed under the GPLv3 license. See the [LICENSE](./LICENSE) file for more details.

Grand Theft Auto and all related trademarks are © Rockstar North 1997–2026.
