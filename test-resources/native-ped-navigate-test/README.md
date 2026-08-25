# Native ped and vehicle navigation visual test

This small resource is the manual visual check for `setPedNavigateTo` and
`setPedDriveTo`.

## Pedestrian navigation

1. Run `/navpet` to create a server ped owned by your client.
2. Walk to any reachable destination, including around a building or fence.
3. Run `/navpetgo [walk|run|sprint]` to save your current position as the
   destination. GTA must execute `TASK_COMPLEX_FOLLOW_NODE_ROUTE` and choose
   the pedestrian path nodes itself.
4. On a second client, verify that the ped follows the same synchronized
   movement and locomotion presentation without owning a native navigation
   task.
5. Use `/navpetstop` or `/navpetcleanup` to test cancellation and cleanup.

The resource reports native acceptance and terminal distance. It deliberately
does not implement steering, waypoint generation or observer-side AI in Lua.

## Vehicle navigation

1. Run `/navcar` on foot to create a Sultan with an NPC driver in front of you.
2. Move to the desired destination.
3. Run `/navcargo [speed]`, for example `/navcargo 30` or `/navcargo 80`.
   GTA uses `TASK_COMPLEX_CAR_DRIVE_TO_POINT` in `normal` mode with the
   `stop_for_cars` driving style.
4. Use `/navcarstop` to cancel the task or `/navcarcleanup` to destroy the
   driver and vehicle.

Only the owning client runs the native road AI. The server vehicle remains an
ordinary synchronized MTA element for every observer.
