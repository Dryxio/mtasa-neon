# Native GTA UI checkpoint test

Install this directory and `native-ui-test-peer` under the server resources
folder. Start both resources. Nothing is displayed automatically.

| Command | Exercise |
|---|---|
| `/nui selftest` | Invalid fields/types, NaN, unsupported text, duplicate names, referenced text, stale handles, 1/4/2 capacity, foreign ownership |
| `/nui texts` | 38-second objective/dialogue/help/big-text sequence, accents, numbers, effective binds |
| `/nui taxi` | TIME, FARES, numeric counter, fraction and gauge; authoritative server corrections |
| `/nui correction` | Late positive correction after zero; assert one completion event for the handle |
| `/nui pause` | Toggle the current clock's explicit pause |
| `/nui menu` | Columns, disabled row, alignment, native accept/cancel |
| `/nui grid` | Native 8×8 vehicle-color grid, including indices beyond the 12-row list capacity |
| `/nui blackjack` | Native Wager window, amount, scores, result, stock cards and independent substitutions |
| `/nui fade` | Text on both sides of a fade initiated by your activity |
| `/nui stress` | 100 message replacements; restart this resource while running |
| `/nui-peer` | Second resource attempts a 60-second message lease |
| `/nui-peer clear` | Release only the peer resource's interfaces |
| `/nui clear` | Release only this resource's interfaces |

## Manual matrix

1. Run selftest with the peer started. All assertions must report PASS. Repeat
   after stopping/restarting the main resource and after reconnecting.
2. Compare scenes in 4:3, 16:9 and ultralarge, with HUD Match Aspect Ratio on/off.
   Check whole compositions, not only individual text draw calls.
3. For texts: spoken subtitles off/on, accents in your selected European font
   language, custom rebound sprint and enter/exit keys, resource restart during
   each channel, switching back to existing acquireMissionText/showMission APIs.
4. For taxi: countdown through zero, pause/resume, low FPS, repeated server time
   corrections, release/reacquire, hidden HUD. No reward is attached to results.
5. For menus: disabled choices, long labels, all 64 grid positions, held accept
   and cancel while opening/reopening, two simultaneous menus, chat, console,
   MTA main menu, resource stop while focused. No movement/fire/enter should
   leak through a focused menu, and input must return immediately after hide.
6. For blackjack: title/amount spacing, font metrics, cards, layers and fades.
   Two right-side fractions must remain `1 / 10` and `2 / 20` respectively.
7. Run `/nui-peer`, then `/nui texts`: the first owner's message must survive the
   second owner's refusal. Stop the owner, then retry. Reverse the order too.
8. Check spawn/respawn and reconnect. Interfaces intentionally survive death
   until the activity releases them; resource restart/disconnect must clean them.

This resource contains runtime assertions and scenes for human review. Headless
model tests and compilation do not certify the above visual/input checks.
See [API](../../docs/client/native-ui.md).
