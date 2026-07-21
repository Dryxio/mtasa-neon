-- Passive chat presentation for the Drive-Thru conformance mission. The
-- mission publishes observations here, but this file never emits a server
-- event or changes mission state.

DRIVETHRU_TRACE = DRIVETHRU_TRACE or {}

local state = {
    steps = {},
    byId = {},
    current = nil,
    chatEnabled = false,
    lastPublished = nil,
}

local colours = {
    gold = "#F6C24E",
    blue = "#74BFEA",
    muted = "#B0B0A6",
    white = "#EEEEEA",
    failed = "#C6413A",
}

local function publish(step, force, failed)
    if not state.chatEnabled or not step or (not force and state.lastPublished == step.id) then
        return
    end

    local commandColour = failed and colours.failed or colours.gold
    local prefix = failed and "FAILED // " or ""
    if step.originalTask then
        outputChatBox(("%s%s%s %s// %s"):format(commandColour, prefix, step.primitive, colours.muted,
                                                string.upper(step.category)), 255, 255, 255, true)
        outputChatBox(("%sGTA TASK: %s %s// %s%s"):format(colours.blue, step.originalTask, colours.muted, colours.white,
                                                          step.title), 255, 255, 255, true)
    else
        outputChatBox(("%s%s%s %s// %s %s%s"):format(commandColour, prefix, step.primitive, colours.muted,
                                                       string.upper(step.category), colours.white, step.title), 255, 255, 255, true)
    end
    state.lastPublished = step.id
end

function DRIVETHRU_TRACE.setSequence(sequence)
    if type(sequence) ~= "table" then
        return false
    end
    state.steps = {}
    state.byId = {}
    state.current = nil
    state.lastPublished = nil
    for index, source in ipairs(sequence) do
        if type(source) ~= "table" or type(source.title) ~= "string" then
            return false
        end
        local step = {
            id = source.id or index,
            title = source.title,
            category = tostring(source.category or "RUNTIME"),
            primitive = tostring(source.primitive or "SCRIPT COMMAND"),
            originalTask = source.originalTask and tostring(source.originalTask) or nil,
        }
        state.steps[#state.steps + 1] = step
        state.byId[tostring(step.id)] = step
    end
    return true
end

function DRIVETHRU_TRACE.setCurrent(reference)
    local step = state.byId[tostring(reference)]
    if not step then
        return false
    end
    state.current = step
    publish(step, false, false)
    return true
end

function DRIVETHRU_TRACE.fail(reference)
    local step = state.byId[tostring(reference)] or state.current
    if not step then
        return false
    end
    state.current = step
    publish(step, true, true)
    return true
end

function DRIVETHRU_TRACE.reset()
    state.steps = {}
    state.byId = {}
    state.current = nil
    state.lastPublished = nil
end

function DRIVETHRU_TRACE.toggleChat(forced)
    state.chatEnabled = type(forced) == "boolean" and forced or not state.chatEnabled
    if state.chatEnabled and state.current then
        publish(state.current, true, false)
    end
    return state.chatEnabled
end

local function toggleChat(_, argument)
    local forced
    if type(argument) == "string" and argument ~= "" then
        argument = string.lower(argument)
        if argument == "on" or argument == "1" or argument == "true" then
            forced = true
        elseif argument == "off" or argument == "0" or argument == "false" then
            forced = false
        else
            outputChatBox("[Story runtime] Usage: /drivethrutracechat [on|off].", 244, 180, 100)
            return
        end
    end

    local enabled = DRIVETHRU_TRACE.toggleChat(forced)
    outputChatBox(("[Story runtime] Drive-Thru chat trace %s. Toggle: /drivethrutracechat."):format(
                      enabled and "enabled" or "disabled"), 145, 220, 175)
end

addCommandHandler("drivethrutracechat", toggleChat)
