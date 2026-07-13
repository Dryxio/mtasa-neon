# Native CULL mirror maze

This opt-in resource builds a deterministic 5x5 maze and gives every cell its
own native GTA mirror CULL zone. The zones do not overlap because GTA selects
the first mirror zone containing the camera. Each cell reflects the camera
across one of its closed walls using the native plane equation
`normal dot position = mirrorV`.

Commands:

- `/mirrormaze` enters the maze and remembers the player's previous position.
- `/mirrorleave` returns to the remembered position.
- `/mirrorreset` returns to the maze entrance.
- `/mirrordebug [on|off]` draws the native activation volumes, reflection
  planes, active cell, and a minimap.
- `/mirrormirrors [on|off]` enables or disables all custom mirror zones.
- `/mirrorstats` reports creation failures and current state.
- `/mirrorhelp` lists the commands in-game.

This demonstrates real GTA mirror-camera rendering. It does not provide
recursive mirror-in-mirror reflections; the game uses one matching mirror zone
at a time.
