local enabled = false
local horizontalStep, verticalStep = 0.15, 1.0
local accepted, rejected = 0, 0
local lastResult = "idle"

local function say(text)
    outputChatBox("[aim-test] " .. text, 120, 220, 255)
    outputDebugString("[aim-test] " .. text)
end

local function available()
    return type(getCameraAimDirection) == "function" and type(setCameraAimDirection) == "function"
end

addCommandHandler("aimrecoil", function(_, pitch, yaw)
    if not available() then
        say("This client does not expose the camera aim API.")
        return
    end
    if pitch then
        local p, y = tonumber(pitch), tonumber(yaw or "0.15")
        if not p or not y or p ~= p or y ~= y or math.abs(p) > 10 or math.abs(y) > 10 then
            say("Usage: /aimrecoil [pitchDegrees yawDegrees], each between -10 and 10")
            return
        end
        verticalStep, horizontalStep = p, y
        enabled = true
    else
        enabled = not enabled
    end
    say((enabled and "ON" or "OFF") .. ": pitch=" .. verticalStep .. ", yaw=" .. horizontalStep)
end)

addEventHandler("onClientPlayerWeaponFire", localPlayer, function()
    if not enabled then return end
    local h, v = getCameraAimDirection()
    if h == false then
        rejected = rejected + 1
        lastResult = "unsupported state/transition"
        return
    end
    if setCameraAimDirection(h + horizontalStep, v + verticalStep) then
        -- Global visual shake is independent of the setter's angular bump reset.
        resetShakeCamera()
        accepted = accepted + 1
        lastResult = "accepted"
    else
        rejected = rejected + 1
        lastResult = "setter rejected"
    end
end)

-- Run while holding aim. This checks rejected writes and immediate readback;
-- it deliberately does not claim to validate next-frame persistence or hits.
local function checkAim()
    if not available() then say("API unavailable") return end
    local h, v = getCameraAimDirection()
    if h == false then
        say("Unavailable-state setter rejection: " .. (setCameraAimDirection(0, 0) == false and "PASS" or "FAIL"))
        return
    end
    local function near(a, b) return math.abs(a - b) < 0.002 end
    local passed = true
    for _, bad in ipairs({0 / 0, math.huge, -math.huge, 1e30}) do
        local result = setCameraAimDirection(bad, v)
        local h2, v2 = getCameraAimDirection()
        passed = passed and result == false and h2 ~= false and near(h, h2) and near(v, v2)
        result = setCameraAimDirection(h, bad)
        h2, v2 = getCameraAimDirection()
        passed = passed and result == false and h2 ~= false and near(h, h2) and near(v, v2)
    end
    passed = setCameraAimDirection(h, v) and passed
    say("Invalid-input/no-change checks: " .. (passed and "PASS" or "FAIL"))
end
addCommandHandler("aimcheck", checkAim)
-- A key works while holding aim; opening chat to enter a command can itself
-- leave the native aim mode before its callback runs.
bindKey("F7", "down", checkAim)

addEventHandler("onClientRender", root, function()
    if not available() then return end
    local h, v = getCameraAimDirection()
    local angles = h == false and "unavailable" or string.format("yaw %.3f / pitch %.3f", h, v)
    dxDrawText(string.format("Aim recoil: %s | %s\naccepted %d / rejected %d | %s\n/aimrecoil [pitch yaw] | F7: checks",
        enabled and "ON" or "OFF", angles, accepted, rejected, lastResult), 25, 240, 900, 320,
        tocolor(255, 255, 255), 1, "default-bold")
end)

addEventHandler("onClientResourceStart", resourceRoot, function()
    say(available() and "Ready: /aimrecoil 1 0.15. Aim and fire at a wall." or "API unavailable: install the issue-121 client build.")
end)
