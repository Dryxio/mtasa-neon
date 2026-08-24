local vehicle = dofile("test-resources/native-vehicle-traffic/telemetry.lua")
local ped = dofile("test-resources/native-ped-traffic/telemetry.lua")

local assertions = 0
local function expect(label, condition)
    assertions = assertions + 1
    if not condition then error("FAIL " .. label, 2) end
end

local forward = assert(vehicle.classifyMotion({rz = 0, vx = 0, vy = 0.2}))
local reverse = assert(vehicle.classifyMotion({rz = 0, vx = 0, vy = -0.2}))
local east = assert(vehicle.classifyMotion({rz = 270, vx = 0.2, vy = 0}))
local stopped = assert(vehicle.classifyMotion({rz = 90, vx = 0.001, vy = 0}))
expect("north-facing forward", forward.state == "forward" and forward.alignment > 0.99)
expect("north-facing reverse", reverse.state == "reverse" and reverse.alignment < -0.99)
expect("east-facing forward", east.state == "forward" and east.alignment > 0.99)
expect("stationary threshold", stopped.state == "stationary")

local state, signal = vehicle.updateMotion(nil, {rz = 0, vx = 0, vy = -0.2, x = 0, y = 0}, 1000)
expect("reverse not immediate", signal == false)
state, signal = vehicle.updateMotion(state, {rz = 0, vx = 0, vy = -0.2, x = 0, y = -1}, 3000)
expect("sustained reverse alert", signal and signal.kind == "reverse-sustained" and signal.duration == 2000)
state, signal = vehicle.updateMotion(state, {rz = 0, vx = 0, vy = -0.2, x = 0, y = -2}, 5000)
expect("reverse alert rate limit", signal == false)
state, signal = vehicle.updateMotion(state, {rz = 0, vx = 0, vy = 0.2, x = 0, y = -1}, 5500)
expect("reverse recovery", signal and signal.kind == "reverse-recovered")
expect("distance accumulated", state.distance == 3)

local window = vehicle.newCounterWindow()
vehicle.record(window, "candidate-retry", "no-path")
vehicle.record(window, "candidate-retry", "no-path")
vehicle.record(window, "spawn")
local drained = vehicle.drain(window)
expect("window events", drained.events["candidate-retry"] == 2 and drained.events.spawn == 1)
expect("window reasons", drained.reasons["no-path"] == 2)
expect("window reset", next(window.events) == nil and next(window.reasons) == nil)

local base = {
    enabled = true, eligible = true, worldReady = true, profilePresent = true, profileRevisionOk = true, profileFresh = true,
    globalLive = 4, globalCap = 90, pedPoolLive = 10, pedPoolCap = 106, live = 2, target = 8,
}
expect("ped candidate ready", ped.classifyPlayer(base) == "candidate-ready")
base.profilePresent = false
expect("ped missing profile", ped.classifyPlayer(base) == "profile-missing")
base.profilePresent, base.profileFresh = true, false
expect("ped stale profile", ped.classifyPlayer(base) == "profile-stale")
base.profileFresh, base.globalLive = true, 90
expect("ped global cap", ped.classifyPlayer(base) == "global-cap")

local delta, snapshot = ped.deltaMap({no_path = 5, visible = 2}, {no_path = 3, visible = 2})
expect("ped reason delta", delta.no_path == 2 and delta.visible == nil and snapshot.visible == 2)

print(("PASS traffic telemetry headless harness (%d assertions)"):format(assertions))
