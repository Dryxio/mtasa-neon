-- Passive presentation layer for the story runtime. Mission code publishes
-- execution facts here, but neither the HUD nor chat feed can affect gameplay.

TAGUP_TRACE = TAGUP_TRACE or {}

local DESIGN_WIDTH = 1920
local DESIGN_HEIGHT = 1080
local TOGGLE_KEY = "F7"
local HISTORY_COUNT = 2

-- This is deliberately styled as a GTA:SA HUD feed instead of a Neon tool
-- panel. Bank Gothic is one of MTA's bundled San Andreas fonts, so the overlay
-- does not need to borrow assets from another resource.
local palette = {
    backdrop = {0, 0, 0, 142},
    divider = {255, 255, 255, 38},
    gold = {246, 194, 78, 255},
    white = {238, 238, 230, 255},
    muted = {176, 176, 166, 255},
    live = {92, 158, 75, 255},
    failed = {198, 65, 58, 255},
    skipped = {215, 159, 65, 255},
}

-- Chat uses one extra hue that the HUD does not need: blue identifies the
-- concrete GTA task class, keeping it visually distinct from the gold SCM
-- command and from the white presentation copy.
local chatColors = {
    task = "#74BFEA",
    muted = "#B0B0A6",
    white = "#EEEEEA",
}

local state = {
    visible = false,
    visibility = 0,
    steps = {},
    history = {},
    currentIndex = nil,
    chatEnabled = false,
    chatLastIndex = nil,
    title = "STORY RUNTIME",
    subtitle = "AWAITING EXECUTION",
    liveSequence = false,
    transitionTick = 0,
    lastFrameTick = getTickCount(),
}

local statusAliases = {
    active = "active",
    current = "active",
    running = "active",
    in_progress = "active",
    done = "done",
    complete = "done",
    completed = "done",
    passed = "done",
    queued = "queued",
    pending = "queued",
    future = "queued",
    failed = "failed",
    error = "failed",
    skipped = "skipped",
}

local fonts = {
    category = {value = "bankgothic", scale = 0.52},
    primitive = {value = "pricedown", scale = 1.50},
    caption = {value = "bankgothic", scale = 0.62},
    history = {value = "pricedown", scale = 0.84},
    historyMeta = {value = "bankgothic", scale = 0.44},
}

local function clamp(value, minimum, maximum)
    return math.max(minimum, math.min(maximum, value))
end

local function color(entry, opacity)
    return tocolor(entry[1], entry[2], entry[3], math.floor(entry[4] * clamp(opacity or 1, 0, 1) + 0.5))
end

local function easeOutCubic(value)
    local inverse = 1 - clamp(value, 0, 1)
    return 1 - inverse * inverse * inverse
end

local function normalizeStatus(status)
    if type(status) ~= "string" then
        return nil
    end
    return statusAliases[string.lower(status)]
end

local function normalizeProgress(progress)
    if type(progress) ~= "number" or progress ~= progress then
        return nil
    end
    if progress > 1 and progress <= 100 then
        progress = progress / 100
    end
    return clamp(progress, 0, 1)
end

local function resolveStep(reference)
    if type(reference) == "number" and reference >= 1 and reference <= #state.steps and reference % 1 == 0 then
        return reference
    end
    for index, step in ipairs(state.steps) do
        if step.id == reference or tostring(step.id) == tostring(reference) then
            return index
        end
    end
    return nil
end

local function publishChatStep(index, force, statusLabel)
    if not state.chatEnabled or not state.steps[index] or (not force and state.chatLastIndex == index) then
        return
    end

    local step = state.steps[index]
    local primitiveColor = "#F6C24E"
    if statusLabel == "FAILED" then
        primitiveColor = "#C6413A"
    elseif statusLabel == "SKIPPED" or statusLabel == "DEBUG SKIP" then
        primitiveColor = "#D79F41"
    end
    local status = statusLabel and (statusLabel .. " // ") or ""
    if step.originalTask then
        outputChatBox(("%s%s%s %s// %s"):format(primitiveColor, status, step.primitive, chatColors.muted,
                                                string.upper(step.category)), 255, 255, 255, true)
        outputChatBox(("%sGTA TASK: %s %s// %s%s"):format(chatColors.task, step.originalTask, chatColors.muted, chatColors.white,
                                                          step.title), 255, 255, 255, true)
    else
        outputChatBox(("%s%s%s %s// %s %s%s"):format(primitiveColor, status, step.primitive, chatColors.muted,
                                                       string.upper(step.category), chatColors.white, step.title), 255, 255, 255, true)
    end
    state.chatLastIndex = index
end

local function setTransition()
    state.transitionTick = getTickCount()
end

local function pushHistory(index, status)
    local step = state.steps[index]
    if not step then
        return
    end

    local latest = state.history[#state.history]
    if latest and latest.index == index and latest.status == status then
        return
    end

    state.history[#state.history + 1] = {
        index = index,
        primitive = step.primitive,
        category = step.category,
        status = status,
    }
    while #state.history > HISTORY_COUNT do
        table.remove(state.history, 1)
    end
end

local function deriveSequenceStatuses(currentIndex)
    for index, step in ipairs(state.steps) do
        if index < currentIndex then
            if step.status ~= "failed" and step.status ~= "skipped" then
                step.status = "done"
                step.progress = 1
            end
        elseif index == currentIndex then
            step.status = "active"
        elseif step.status ~= "failed" and step.status ~= "skipped" and step.status ~= "done" then
            step.status = "queued"
        end
    end
end

function TAGUP_TRACE.setSequence(sequence, options)
    if type(sequence) ~= "table" then
        return false
    end

    local steps = {}
    local initialCurrentIndex
    for index, source in ipairs(sequence) do
        local step
        if type(source) == "string" then
            step = {id = index, title = source}
        elseif type(source) == "table" and type(source.title or source.label or source.name) == "string" then
            step = {
                id = source.id or index,
                title = source.title or source.label or source.name,
                explanation = source.explanation or source.description,
                detail = source.detail,
                category = source.category or source.kind or "RUNTIME",
                primitive = source.primitive or source.opcode,
                originalTask = source.originalTask or source.gtaTask,
                status = normalizeStatus(source.status),
                progress = normalizeProgress(source.progress),
            }
        else
            return false
        end

        step.status = step.status or "queued"
        step.progress = step.progress or (step.status == "done" and 1 or 0)
        step.explanation = step.explanation and tostring(step.explanation) or "A runtime operation is being observed without influencing mission state."
        step.detail = step.detail and tostring(step.detail) or nil
        step.category = tostring(step.category or "RUNTIME")
        step.primitive = step.primitive and tostring(step.primitive) or "SCRIPT COMMAND"
        step.originalTask = step.originalTask and tostring(step.originalTask) or nil
        if step.status == "active" and not initialCurrentIndex then
            initialCurrentIndex = index
        end
        step.displayProgress = step.progress
        step.changedTick = getTickCount()
        steps[#steps + 1] = step
    end

    options = type(options) == "table" and options or {}
    state.steps = steps
    state.history = {}
    state.chatLastIndex = nil
    state.currentIndex = initialCurrentIndex
    state.title = type(options.title) == "string" and options.title or "STORY RUNTIME"
    state.subtitle = type(options.subtitle) == "string" and options.subtitle or "LIVE EXECUTION"
    state.liveSequence = options.live == true
    setTransition()
    if initialCurrentIndex then
        publishChatStep(initialCurrentIndex, false)
    end
    return true
end

function TAGUP_TRACE.setCurrent(reference, detail)
    local index = resolveStep(reference)
    if not index then
        return false
    end

    local previousIndex = state.currentIndex
    if previousIndex and previousIndex ~= index then
        local previous = state.steps[previousIndex]
        if previous.status == "active" then
            previous.status = "done"
            previous.progress = 1
            pushHistory(previousIndex, "done")
        end
    end

    state.currentIndex = index
    deriveSequenceStatuses(index)
    if detail ~= nil then
        state.steps[index].detail = tostring(detail)
    end
    state.steps[index].changedTick = getTickCount()
    setTransition()
    publishChatStep(index, false)
    return true
end

-- Debug skips remain visibly different from execution. They only alter the
-- presentation cursor and cannot request a server stage change.
function TAGUP_TRACE.skipTo(reference, detail)
    local index = resolveStep(reference)
    if not index or (state.currentIndex and index < state.currentIndex) then
        return false
    end

    local firstSkipped = state.currentIndex or 1
    for stepIndex = firstSkipped, index - 1 do
        local step = state.steps[stepIndex]
        if step.status ~= "done" and step.status ~= "failed" then
            step.status = "skipped"
            pushHistory(stepIndex, "skipped")
        end
    end
    for stepIndex = index + 1, #state.steps do
        local step = state.steps[stepIndex]
        if step.status ~= "failed" and step.status ~= "skipped" and step.status ~= "done" then
            step.status = "queued"
        end
    end

    state.currentIndex = index
    state.steps[index].status = "active"
    if detail ~= nil then
        state.steps[index].detail = tostring(detail)
    end
    state.steps[index].changedTick = getTickCount()
    setTransition()
    publishChatStep(index, true, "DEBUG SKIP")
    return true
end

function TAGUP_TRACE.setStatus(reference, status, detail)
    local index = resolveStep(reference)
    local normalized = normalizeStatus(status)
    if not index or not normalized then
        return false
    end

    local step = state.steps[index]
    step.status = normalized
    if normalized == "done" then
        step.progress = 1
        pushHistory(index, "done")
    elseif normalized == "active" then
        state.currentIndex = index
        setTransition()
        publishChatStep(index, false)
    elseif normalized == "failed" or normalized == "skipped" then
        pushHistory(index, normalized)
        if state.currentIndex == index then
            publishChatStep(index, true, normalized == "failed" and "FAILED" or "SKIPPED")
        end
    end
    if detail ~= nil then
        step.detail = tostring(detail)
    end
    step.changedTick = getTickCount()
    return true
end

-- A terminal failure cancels every still-pending operation. Leaving them as
-- queued would make a stopped runtime look as if it were about to resume.
function TAGUP_TRACE.fail(reference, detail)
    local index = resolveStep(reference)
    if not index then
        return false
    end

    for stepIndex, step in ipairs(state.steps) do
        if stepIndex == index then
            step.status = "failed"
        elseif step.status == "active" or step.status == "queued" then
            step.status = "skipped"
        end
    end
    state.currentIndex = index
    pushHistory(index, "failed")
    if detail ~= nil then
        state.steps[index].detail = tostring(detail)
    end
    state.steps[index].changedTick = getTickCount()
    setTransition()
    publishChatStep(index, true, "FAILED")
    return true
end

function TAGUP_TRACE.setProgress(reference, progress, detail)
    local index = resolveStep(reference)
    local normalized = normalizeProgress(progress)
    if not index or normalized == nil then
        return false
    end

    local step = state.steps[index]
    step.progress = normalized
    if detail ~= nil then
        step.detail = tostring(detail)
    end
    step.changedTick = getTickCount()
    return true
end

function TAGUP_TRACE.reset()
    state.steps = {}
    state.history = {}
    state.currentIndex = nil
    state.chatLastIndex = nil
    state.title = "STORY RUNTIME"
    state.subtitle = "AWAITING EXECUTION"
    state.liveSequence = false
    setTransition()
    return true
end

function TAGUP_TRACE.toggle(forceVisible)
    if type(forceVisible) == "boolean" then
        state.visible = forceVisible
    else
        state.visible = not state.visible
    end
    return state.visible
end

function TAGUP_TRACE.isVisible()
    return state.visible
end

function TAGUP_TRACE.toggleChat(forceEnabled)
    if type(forceEnabled) == "boolean" then
        state.chatEnabled = forceEnabled
    else
        state.chatEnabled = not state.chatEnabled
    end
    return state.chatEnabled
end

function TAGUP_TRACE.isChatEnabled()
    return state.chatEnabled
end

-- The preview exercises the same rich presentation data as a live mission but
-- remains entirely local, making visual QA safe between gameplay runs.
function TAGUP_TRACE.preview()
    if state.liveSequence then
        return false
    end
    TAGUP_TRACE.setSequence({
        {
            id = "cutscene",
            title = "GTA plays the original SWEET1A cutscene",
            category = "NATIVE CUTSCENE",
            primitive = "DAT / CUT / IFP",
            explanation = "Neon leases GTA's camera and synchronizes native loading, playback and cleanup across the party.",
            detail = "Track 739 finished naturally",
            status = "done",
        },
        {
            id = "sequence",
            title = "Sweet performs one composed task sequence",
            category = "NATIVE TASK",
            primitive = "0615 / 0618 / 0646 · PERFORM_SEQUENCE_TASK",
            originalTask = "CTaskComplexSequence -> CTaskComplexUseSequence",
            explanation = "GTA owns the leave-car, walk and shoot hierarchy while Lua only observes the active child.",
            detail = "GET_SEQUENCE_PROGRESS = 2 · shooting",
            status = "active",
            progress = 0.62,
        },
        {
            id = "tag",
            title = "Spray hits advance the gang tag",
            category = "NATIVE GAMEPLAY",
            primitive = "0702 / CShotInfo",
            explanation = "Real spray-can impacts update GTA's eight-step alpha path before the server validates progress.",
            detail = "Queued after demonstration",
        },
        {
            id = "recording",
            title = "The Greenwood follows recording 207",
            category = "NATIVE RECORDING",
            primitive = "07C0 / 05EB / 060E",
            explanation = "A resource-owned native RRR slot drives the car while ordinary MTA replication stays active.",
            detail = "Queued for the rooftop transition",
        },
    }, {title = "TAGGING UP TURF", subtitle = "PRESENTATION PREVIEW"})
    pushHistory(1, "done")
    TAGUP_TRACE.setCurrent("sequence")
    TAGUP_TRACE.setProgress("sequence", 0.62, "GET_SEQUENCE_PROGRESS = 2 · shooting")
    TAGUP_TRACE.toggle(true)
    return true
end

local function drawText(text, left, top, right, bottom, textColor, scale, font, alignX, alignY, opacity, wordBreak, outline)
    text = tostring(text or "")
    outline = math.max(1, outline or 1)
    local shadow = color({0, 0, 0, 255}, opacity)
    for offsetX = -outline, outline, outline do
        for offsetY = -outline, outline, outline do
            if offsetX ~= 0 or offsetY ~= 0 then
                dxDrawText(text, left + offsetX, top + offsetY, right + offsetX, bottom + offsetY, shadow, scale, font, alignX, alignY, true,
                           wordBreak == true, false, false)
            end
        end
    end
    dxDrawText(text, left, top, right, bottom, textColor, scale, font, alignX, alignY, true, wordBreak == true, false, false)
end

local function drawEmptyState(x, y, width, scale, opacity)
    local textScale = clamp(scale, 0.8, 1.4)
    dxDrawRectangle(x, y, width, 78 * scale, color(palette.backdrop, opacity), false)
    drawText("NATIVE RUNTIME", x + 16 * scale, y + 7 * scale, x + width - 16 * scale, y + 29 * scale, color(palette.muted, opacity),
             fonts.category.scale * textScale,
             fonts.category.value, "right", "center", opacity, false, 2 * textScale)
    drawText("AWAITING SCRIPT COMMAND", x + 16 * scale, y + 27 * scale, x + width - 16 * scale, y + 67 * scale, color(palette.gold, opacity),
             fonts.primitive.scale * textScale, fonts.primitive.value, "right", "center", opacity, false, 2 * textScale)
end

local function historyColor(status)
    if status == "failed" then
        return palette.failed
    elseif status == "skipped" then
        return palette.skipped
    end
    return palette.live
end

local function historyStatusLabel(status)
    if status == "failed" then
        return "FAILED"
    elseif status == "skipped" then
        return "SKIPPED"
    end
    return "DONE"
end

local function drawHistory(x, y, width, scale, opacity, textScale)
    local rowHeight = 48 * scale
    local rowOpacities = {0.94, 0.70}

    for row = 1, math.min(HISTORY_COUNT, #state.history) do
        local entry = state.history[#state.history - row + 1]
        local rowY = y + (row - 1) * rowHeight
        local rowOpacity = opacity * rowOpacities[row]
        local statusVisual = historyColor(entry.status)

        if row > 1 then
            dxDrawRectangle(x, rowY, width, 1 * scale, color(palette.divider, opacity), false)
        end
        drawText(entry.primitive, x, rowY + 2 * scale, x + width, rowY + 28 * scale, color(palette.white, rowOpacity),
                 fonts.history.scale * textScale, fonts.history.value, "right", "center", rowOpacity, false, 1.5 * textScale)
        drawText(historyStatusLabel(entry.status) .. "  //  " .. string.upper(entry.category), x, rowY + 27 * scale, x + width,
                 rowY + 45 * scale, color(statusVisual, rowOpacity), fonts.historyMeta.scale * textScale, fonts.historyMeta.value, "right", "center",
                 rowOpacity, false, 1.5 * textScale)
    end
end

local function renderTrace()
    local now = getTickCount()
    local delta = clamp(now - state.lastFrameTick, 0, 100)
    state.lastFrameTick = now

    local targetVisibility = state.visible and 1 or 0
    local visibilityStep = delta / 180
    if state.visibility < targetVisibility then
        state.visibility = math.min(targetVisibility, state.visibility + visibilityStep)
    elseif state.visibility > targetVisibility then
        state.visibility = math.max(targetVisibility, state.visibility - visibilityStep)
    end
    if state.visibility <= 0 then
        return
    end
    if isMTAWindowActive() or isTransferBoxActive() or isPlayerMapVisible() then
        return
    end

    local screenWidth, screenHeight = guiGetScreenSize()
    local scale = clamp(math.min(screenWidth / DESIGN_WIDTH, screenHeight / DESIGN_HEIGHT), 0.65, 1.4)
    local textScale = clamp(scale, 0.8, 1.4)
    local opacity = easeOutCubic(state.visibility)
    local width = 680 * scale
    local x = screenWidth - width - 48 * scale
    local y = 350 * scale

    if #state.steps == 0 or not state.currentIndex or not state.steps[state.currentIndex] then
        drawEmptyState(x, y, width, scale, opacity)
        return
    end

    local current = state.steps[state.currentIndex]
    local historyRows = math.min(HISTORY_COUNT, #state.history)
    local boxHeight = (historyRows > 0 and 145 + historyRows * 48 or 128) * scale
    local contentX = x + 18 * scale
    local contentRight = x + width - 18 * scale
    local transition = easeOutCubic(clamp((now - state.transitionTick) / 260, 0, 1))
    local contentOffset = (1 - transition) * 18 * scale
    local statusVisual = current.status == "failed" and palette.failed or palette.gold

    dxDrawRectangle(x, y, width, boxHeight, color(palette.backdrop, opacity), false)

    drawText(string.upper(current.category), contentX + contentOffset, y + 9 * scale, contentRight + contentOffset, y + 31 * scale,
             color(palette.muted, opacity),
             fonts.category.scale * textScale, fonts.category.value, "right", "center", opacity, false, 2 * textScale)
    drawText(current.primitive, contentX + contentOffset, y + 30 * scale, contentRight + contentOffset, y + 80 * scale, color(statusVisual, opacity),
             fonts.primitive.scale * textScale, fonts.primitive.value, "right", "center", opacity, false, 2.5 * textScale)
    drawText(current.title, contentX + contentOffset, y + 80 * scale, contentRight + contentOffset, y + 116 * scale, color(palette.white, opacity),
             fonts.caption.scale * textScale, fonts.caption.value, "right", "center", opacity, true, 2 * textScale)

    if historyRows > 0 then
        dxDrawRectangle(contentX, y + 126 * scale, contentRight - contentX, 1 * scale, color(palette.divider, opacity), false)
        drawHistory(contentX, y + 137 * scale, contentRight - contentX, scale, opacity, textScale)
    end
end

local function toggleFromInput(sourceName, argument)
    if sourceName ~= "taguptrace" and
        (isChatBoxInputActive() or isConsoleActive() or isMainMenuActive() or isTransferBoxActive() or isCursorShowing() or guiGetInputEnabled() or
            isPlayerMapVisible()) then
        return
    end

    local forced
    if sourceName == "taguptrace" and type(argument) == "string" and argument ~= "" then
        argument = string.lower(argument)
        if argument == "on" or argument == "1" or argument == "true" then
            forced = true
        elseif argument == "off" or argument == "0" or argument == "false" then
            forced = false
        else
            outputChatBox("[Story runtime] Usage: /taguptrace [on|off].", 244, 180, 100)
            return
        end
    end

    local visible = TAGUP_TRACE.toggle(forced)
    outputChatBox(("[Story runtime] Monitor %s. Toggle: /taguptrace or %s."):format(visible and "visible" or "hidden", TOGGLE_KEY), 145, 220, 175)
end

local function toggleChatFromInput(_, argument)
    local forced
    if type(argument) == "string" and argument ~= "" then
        argument = string.lower(argument)
        if argument == "on" or argument == "1" or argument == "true" then
            forced = true
        elseif argument == "off" or argument == "0" or argument == "false" then
            forced = false
        else
            outputChatBox("[Story runtime] Usage: /taguptracechat [on|off].", 244, 180, 100)
            return
        end
    end

    local enabled = TAGUP_TRACE.toggleChat(forced)
    outputChatBox(("[Story runtime] Chat trace %s. Toggle: /taguptracechat."):format(enabled and "enabled" or "disabled"), 145, 220, 175)
    if enabled and state.currentIndex then
        publishChatStep(state.currentIndex, true)
    end
end

addCommandHandler("taguptrace", toggleFromInput)
addCommandHandler("taguptracechat", toggleChatFromInput)
addCommandHandler("taguptracepreview", function()
    if TAGUP_TRACE.preview() then
        outputChatBox("[Story runtime] Local presentation preview loaded; mission state is untouched.", 145, 220, 175)
    else
        outputChatBox("[Story runtime] Preview unavailable while a live mission trace is active.", 244, 180, 100)
    end
end)
bindKey(TOGGLE_KEY, "down", toggleFromInput)
addEventHandler("onClientRender", root, renderTrace, true, "high+10")
