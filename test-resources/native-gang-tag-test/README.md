# Native gang-tag test

This resource isolates Neon's resource-owned gang-tag service from Tagging Up
Turf. It verifies that GTA's spray-can shot path selects the registered object,
advances its logical alpha in exact steps of 8, reaches 255, and emits the
matching client event. The server validates and mirrors each native step but
does not infer hits from Lua input, aim, distance, or timers.

## API under test

```text
acquireObjectGangTag(object [, progress = 0])
setObjectGangTagProgress(object, progress)
getObjectGangTagProgress(object)
releaseObjectGangTag(object)

onClientObjectGangTagProgress(previousProgress, currentProgress, creator)
```

Only one resource can own an object at a time. Ownership and progress survive
stream-out and native object recreation. Explicit release and resource stop
remove the spray registration and restore the default renderer.

## Manual protocol

1. Start the resource and run `/nativegangtag`.
2. Spray the nearby tag until the green `PASS` message appears.
3. Run `/nativegangtagcleanup` and restart the resource once to verify cleanup.

Codex must build and deploy this harness but must not execute the in-game
protocol. Gameplay verification belongs to the user.

## Executable gate

The compact US executable with SHA-256
`72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b` was
checked through the repository's Ghidra workflow and direct disassembly.

| Behavior | Target executable | Neon implementation |
| --- | --- | --- |
| Spray call | `CShotInfo::Update` calls `0x565B70` at `0x73A0FF` with origin, output direction, range and `true` | The call-site wrapper preserves those arguments and reads the verified creator field from `[ESI+8]` |
| Selection | `CWorld::SprayPaintWorld` considers at most 15 nearby entities | Neon considers at most 15 nearby resource-owned tag objects |
| Progress | Alpha advances by 8 and caps at 255 | The native registry uses the same byte rule |
| Surface response | The tag entity's forward vector becomes the spray direction | Neon uses the registered object's native matrix forward vector |
| Completion | Return value 2 is produced on the transition to 255 | Neon returns 2 on the same transition, preserving GTA's executed flag and completion audio path |

MTA's global `CTagManager` remains disabled. Re-enabling it would be unsafe and
would not include MTA-created resource objects. Neon extends only explicitly
owned objects while leaving the default multiplayer world unchanged.
