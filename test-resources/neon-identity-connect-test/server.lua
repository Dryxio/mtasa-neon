local expectedBySerial = {}
local rejectNextConnection = false
local rejectedNickname = false

local function isIdentityValue(value)
    return value == false or type(value) == "string"
end

addEventHandler("onPlayerConnect", root, function(
    nickname, _, _, serial, _, _, neonID, discordID
)
    if not isIdentityValue(neonID) or not isIdentityValue(discordID) then
        outputServerLog((
            "[Neon Identity connect test] FAIL invalid event types neon=%s discord=%s"
        ):format(type(neonID), type(discordID)))
        return
    end

    outputServerLog((
        "[Neon Identity connect test] PASS onPlayerConnect received neon=%s discord=%s"
    ):format(type(neonID), type(discordID)))

    expectedBySerial[serial] = {
        neonID = neonID,
        discordID = discordID,
    }

    if rejectNextConnection then
        rejectNextConnection = false
        rejectedNickname = nickname
        expectedBySerial[serial] = nil
        cancelEvent(true, "Neon Identity connection test rejection.")
        outputServerLog("[Neon Identity connect test] Rejected the connection before onPlayerJoin")

        setTimer(function()
            if rejectedNickname == nickname then
                rejectedNickname = false
                outputServerLog("[Neon Identity connect test] PASS rejected connection did not reach onPlayerJoin")
            end
        end, 3000, 1)
    end
end)

addEventHandler("onPlayerJoin", root, function()
    local nickname = getPlayerName(source)
    if rejectedNickname == nickname then
        rejectedNickname = false
        outputServerLog("[Neon Identity connect test] FAIL rejected connection reached onPlayerJoin")
    end

    local serial = getPlayerSerial(source)
    local expected = expectedBySerial[serial]
    expectedBySerial[serial] = nil
    if not expected then
        outputServerLog("[Neon Identity connect test] FAIL onPlayerJoin has no matching connection observation")
        return
    end

    if getPlayerNeonID(source) ~= expected.neonID or getPlayerDiscordID(source) ~= expected.discordID then
        outputServerLog("[Neon Identity connect test] FAIL event arguments differ from the player getters")
        return
    end

    outputServerLog("[Neon Identity connect test] PASS event arguments match the player getters")
end)

local function outputCommandMessage(player, message)
    if isElement(player) then
        outputChatBox(message, player, 100, 255, 100)
    else
        outputServerLog(message)
    end
end

addCommandHandler("neonconnecttest", function(player, _, action)
    if action == "reject-next" then
        rejectNextConnection = true
        outputCommandMessage(player, "[Neon Identity connect test] The next connection will be rejected")
        return
    end

    outputCommandMessage(player, (
        "[Neon Identity connect test] reject-next=%s; usage: neonconnecttest reject-next"
    ):format(tostring(rejectNextConnection)))
end)

addEventHandler("onResourceStart", resourceRoot, function()
    outputServerLog("[Neon Identity connect test] Ready; use 'neonconnecttest reject-next' to test cancellation")
end)
