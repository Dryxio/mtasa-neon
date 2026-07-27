local screenWidth, screenHeight = guiGetScreenSize()
local visible = false
local displayedFps = 0
local sampledFrames = 0
local sampledTime = 0

local SAMPLE_INTERVAL_MS = 500
local TEXT_SCALE = 2.5
local RIGHT_MARGIN = 28
local BOTTOM_MARGIN = 24
local PADDING_X = 14
local PADDING_Y = 8

local function resetSample()
    displayedFps = 0
    sampledFrames = 0
    sampledTime = 0
end

local function updateFps(frameTime)
    if not visible or not frameTime or frameTime <= 0 then
        return
    end

    sampledFrames = sampledFrames + 1
    sampledTime = sampledTime + frameTime

    if sampledTime >= SAMPLE_INTERVAL_MS then
        displayedFps = math.floor(sampledFrames * 1000 / sampledTime + 0.5)
        sampledFrames = 0
        sampledTime = 0
    end
end

local function drawFps()
    if not visible then
        return
    end

    local text = ("%d FPS"):format(displayedFps)
    local textWidth = dxGetTextWidth(text, TEXT_SCALE, "default-bold")
    local textHeight = dxGetFontHeight(TEXT_SCALE, "default-bold")
    local right = screenWidth - RIGHT_MARGIN
    local bottom = screenHeight - BOTTOM_MARGIN
    local left = right - textWidth
    local top = bottom - textHeight

    dxDrawRectangle(left - PADDING_X, top - PADDING_Y, textWidth + PADDING_X * 2, textHeight + PADDING_Y * 2, tocolor(0, 0, 0, 150), true)
    dxDrawText(text, left + 2, top + 2, right + 2, bottom + 2, tocolor(0, 0, 0, 230), TEXT_SCALE, "default-bold", "right", "bottom", false, false,
               true)
    dxDrawText(text, left, top, right, bottom, tocolor(255, 230, 80, 255), TEXT_SCALE, "default-bold", "right", "bottom", false, false, true)
end

local function toggleFpsCounter()
    visible = not visible
    resetSample()
    outputChatBox(("[FPS] Compteur %s. Tape /fps pour le %s."):format(visible and "activé" or "désactivé", visible and "masquer" or "afficher"),
                  visible and 120 or 210, visible and 255 or 210, visible and 120 or 210)
end

addEventHandler("onClientPreRender", root, updateFps)
addEventHandler("onClientRender", root, drawFps)
addCommandHandler("fps", toggleFpsCounter)
