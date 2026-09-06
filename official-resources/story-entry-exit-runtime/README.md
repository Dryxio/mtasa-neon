# Story entry-exit runtime

This resource provides server-authoritative, resource-owned GTA entry-exit transitions without reenabling MTA's globally disabled `CEntryExitManager::Update`.

MTA patches the native manager to return immediately because the legacy path crashes during entry. This runtime therefore reproduces the mission-visible ENEX effect with exact IPL trigger data, a black camera transition, authoritative interior/position changes and deterministic cleanup. It does not claim to execute the original native manager.

## Server API

```lua
local handle, reason = exports["story-entry-exit-runtime"]:acquireStoryEntryExit(
    player,
    "cschp_ls",
    missionDimension,
    {fadeOut = 1.0, blackHold = 0.25, fadeIn = 1.0}
)
```

Exports:

| Function | Result |
| --- | --- |
| `acquireStoryEntryExit(player, site, dimension [, options])` | Returns a resource-owned handle or `false, reason`. |
| `releaseStoryEntryExit(handle)` | Stops detection, cancels an in-flight transition safely and destroys the handle. |
| `getStoryEntryExitState(handle)` | Returns the current owner-visible lease snapshot. |

The server event `onStoryEntryExitStateChange` is emitted from the handle. Relevant states are `active`, `fading_out`, `committed`, `entered`, `exited`, `failed` and `released`. `committed` is the authoritative area/position boundary observed by SCM-style mission logic; `entered` and `exited` confirm completion of the following fade-in.

Only one transition may run for a player at a time. Acquisition becomes active only after a client acknowledgement and fails after five seconds otherwise. The client detects the exact local trigger, but the server revalidates the player, dimension, interior, on-foot state and trigger position before freezing or moving the player. The black hold precedes the commit, matching GTA's loading boundary. Caller shutdown, player departure, timeout and runtime shutdown roll an unfinished transaction back to its source transform and restore the pre-transition frozen state. The runtime forces the camera visible only while it owns an unfinished fade.

## Stock definitions

The initial registry contains `cschp_ls`, the Los Santos Binco site used by `sweet2`:

| Endpoint | IPL trigger | Destination used by MTA |
| --- | --- | --- |
| Exterior | `2244.47, -1665.36, 14.4839`, area 0 | `2244.48, -1664.06, 15.4839`, heading 357 |
| Interior | `207.738, -111.42, 1004.27`, area 15 | `207.738, -109.02, 1005.27`, heading 0 |

The runtime applies GTA's verified `+1.0` entry-exit Z conversion to both trigger centres and destinations. The IPL sizes are full widths, so collision uses half-widths of `0.8/0.8` outside and `0.8/0.7` inside. New sites should be added only from installed IPL evidence and should retain their original ENEX name separately from the unambiguous site key.

## Validation

Static syntax:

```sh
luac -p official-resources/story-entry-exit-runtime/definitions.lua \
  official-resources/story-entry-exit-runtime/server.lua \
  official-resources/story-entry-exit-runtime/client.lua
xmllint --noout official-resources/story-entry-exit-runtime/meta.xml
```

Manual validation through a consuming mission must cover entry, exit, resource stop during both fades, caller stop while inside, attempted entry in a vehicle, wrong dimension rejection and repeated entry-exit cycles.

The isolated `story-entry-exit-test` harness provides `/enextest` and `/enexteststop` so those lifecycle checks can run without completing a full story mission.
