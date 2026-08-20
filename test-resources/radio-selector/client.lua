local FIRST_STATION = 1
local STATION_COUNT = 12
local PANEL_OPEN_DURATION = 0.08
local PANEL_CLOSE_DURATION = 0.055
local BLUR_OPEN_DURATION = 0.11
local BLUR_CLOSE_DURATION = 0.34
local IDLE_DURATION_MS = 2050
local CAROUSEL_RESPONSE = 18.0
local BLUR_DOWNSAMPLE = 2

local selector = {
    selectedChannel = FIRST_STATION,
    lastAudibleChannel = 1,
    muted = false,
    muteAmount = 0,
    onDemandAmount = 0,
    carouselPosition = FIRST_STATION,
    carouselTarget = FIRST_STATION,
    panelAmount = 0,
    blurAmount = 0,
    openTarget = 0,
    lastInteraction = 0,
    lastFrame = getTickCount(),
    forcePreview = false,
    nativeRadioHudWasVisible = true,
    pendingNativeNavigation = false,
    applyingRequestedChannel = false,
    skyGfxColorFilterSuspended = false,
    skipFilteredFrame = false,
}

local graphics = {
    width = 0,
    height = 0,
    screenSource = nil,
    horizontalTarget = nil,
    verticalTarget = nil,
    blurShader = nil,
    logoShader = nil,
    targetWidth = 0,
    targetHeight = 0,
    logos = {},
}

local function clamp(value, minimum, maximum)
    return math.max(minimum, math.min(maximum, value))
end

local function modulo(value, divisor)
    return ((value % divisor) + divisor) % divisor
end

local function round(value)
    return math.floor(value + 0.5)
end

local function approach(value, target, change)
    if value < target then
        return math.min(target, value + change)
    end
    if value > target then
        return math.max(target, value - change)
    end
    return value
end

local function destroyElementIfValid(element)
    if isElement(element) then
        destroyElement(element)
    end
end

local function destroyRenderTargets()
    destroyElementIfValid(graphics.screenSource)
    destroyElementIfValid(graphics.horizontalTarget)
    destroyElementIfValid(graphics.verticalTarget)
    destroyElementIfValid(graphics.blurShader)
    graphics.screenSource = nil
    graphics.horizontalTarget = nil
    graphics.verticalTarget = nil
    graphics.blurShader = nil
end

local function destroyGraphics()
    destroyRenderTargets()
    destroyElementIfValid(graphics.logoShader)
    graphics.logoShader = nil
    for _, logo in pairs(graphics.logos) do
        destroyElementIfValid(logo)
    end
    graphics.logos = {}
end

local function createRenderTargets(width, height)
    destroyRenderTargets()

    graphics.width = width
    graphics.height = height
    graphics.targetWidth = math.max(1, math.floor(width / BLUR_DOWNSAMPLE))
    graphics.targetHeight = math.max(1, math.floor(height / BLUR_DOWNSAMPLE))
    graphics.screenSource = dxCreateScreenSource(width, height)
    graphics.horizontalTarget = dxCreateRenderTarget(graphics.targetWidth, graphics.targetHeight, true)
    graphics.verticalTarget = dxCreateRenderTarget(graphics.targetWidth, graphics.targetHeight, true)
    graphics.blurShader = dxCreateShader("blur.fx", 0, 0, false, "all")

    if not isElement(graphics.screenSource) or not isElement(graphics.horizontalTarget) or not isElement(graphics.verticalTarget) or not isElement(graphics.blurShader) then
        outputDebugString("[radio-selector] Blur unavailable; using the tinted fallback.", 2)
        destroyRenderTargets()
        graphics.width = width
        graphics.height = height
        graphics.targetWidth = math.max(1, math.floor(width / BLUR_DOWNSAMPLE))
        graphics.targetHeight = math.max(1, math.floor(height / BLUR_DOWNSAMPLE))
        return false
    end

    return true
end

local function ensureGraphics()
    local width, height = guiGetScreenSize()
    if width ~= graphics.width or height ~= graphics.height then
        createRenderTargets(width, height)
    end

    if not isElement(graphics.logoShader) then
        graphics.logoShader = dxCreateShader("logo.fx", 0, 0, false, "all")
    end

    if next(graphics.logos) == nil then
        for channel = FIRST_STATION, STATION_COUNT do
            local station = RADIO_SELECTOR_STATIONS[channel]
            local logo = station.icon and dxCreateTexture(station.icon, "argb", true, "clamp") or false
            graphics.logos[channel] = isElement(logo) and logo or createRadioSelectorLogo(channel)
        end
    end

    return width, height
end

local function isStationChannel(channel)
    return type(channel) == "number" and channel >= FIRST_STATION and channel <= STATION_COUNT
end

local function carouselIndexToChannel(index)
    return modulo(index - FIRST_STATION, STATION_COUNT) + FIRST_STATION
end

local function stepStationChannel(channel, direction)
    return carouselIndexToChannel(channel + direction)
end

local function setCarouselChannel(channel, snap)
    channel = math.floor(tonumber(channel) or 0)
    if not isStationChannel(channel) then
        return false
    end

    local previousChannel = selector.selectedChannel
    local halfStationCount = math.floor(STATION_COUNT / 2)
    local difference = modulo(channel - previousChannel + halfStationCount, STATION_COUNT) - halfStationCount

    selector.selectedChannel = channel
    selector.lastAudibleChannel = channel
    selector.carouselTarget = snap and channel or (selector.carouselTarget + difference)
    if snap then
        selector.carouselPosition = channel
    end
    return true
end

local function openSelector(channel, snap)
    local wasClosed = selector.panelAmount <= 0.01 and selector.openTarget == 0
    if wasClosed and not selector.skyGfxColorFilterSuspended and type(getNeonClientSetting) == "function" and type(setNeonClientSetting) == "function" and getNeonClientSetting("skygfx.color_filter") == true then
        selector.skyGfxColorFilterSuspended = setNeonClientSetting("skygfx.color_filter", false) == true
        selector.skipFilteredFrame = selector.skyGfxColorFilterSuspended
    end

    if channel == 0 then
        -- GTA represents mute as channel zero, but retaining the last station
        -- lets UNMUTE resume without adding OFF to the carousel.
        selector.muted = true
    elseif setCarouselChannel(channel, snap or wasClosed) then
        selector.muted = false
    end
    selector.openTarget = 1
    selector.lastInteraction = getTickCount()
end

local function closeSelector()
    selector.openTarget = 0
    selector.forcePreview = false
end

local function isInputWindowActive()
    return isChatBoxInputActive() or isConsoleActive() or isMainMenuActive()
end

local function setAcceptedRadioChannel(channel)
    local previous = getRadioChannel()
    selector.applyingRequestedChannel = true
    local changed = setRadioChannel(channel)
    selector.applyingRequestedChannel = false
    if changed then
        local accepted = getRadioChannel()
        openSelector(accepted, false)
        return true
    end

    openSelector(previous, false)
    return false
end

local function toggleMute()
    local current = getRadioChannel()
    if current == 0 then
        setAcceptedRadioChannel(selector.lastAudibleChannel > 0 and selector.lastAudibleChannel or 1)
    else
        selector.lastAudibleChannel = current
        setAcceptedRadioChannel(0)
    end
end

local function tryGetPlaybackState()
    if type(getRadioPlaybackState) ~= "function" then
        return false
    end

    local success, state = pcall(getRadioPlaybackState)
    return success and state or false
end

local function getPlaybackMetadata(channel)
    local station = RADIO_SELECTOR_STATIONS[channel]
    if selector.muted then
        return "OFF", ""
    end

    if channel == 12 then
        return "RHINESTONE COWBOY", "GLEN CAMPBELL"
    end

    if channel == 11 then
        return "LIVE PROGRAMMING", "WEST COAST TALK RADIO"
    end

    local state = tryGetPlaybackState()
    if not state or state.station ~= channel or state.trackId == nil or state.trackId < 0 then
        return "TUNING...", station.genre
    end

    if state.mode ~= nil and state.mode ~= 2 then
        return "TUNING...", station.genre
    end

    local stationTracks = RADIO_SELECTOR_TRACKS[channel]
    local playingTrack = stationTracks and stationTracks[state.trackIndex]
    if playingTrack and (state.trackType == 3 or state.trackType == 4 or state.trackType == 5) then
        return playingTrack.title, playingTrack.artist
    end

    local typeLabels = {
        [0] = "STATION IDENT",
        [1] = "COMMERCIAL BREAK",
        [2] = "WITH THE DJ",
        [3] = "TRACK INTRO",
        [4] = "NOW PLAYING",
        [5] = "TRACK OUTRO",
        [6] = "TUNING...",
        [7] = "USER TRACK",
    }
    local title = typeLabels[state.trackType] or "LIVE BROADCAST"
    return title, station.genre
end

local function drawOutlinedText(text, left, top, right, bottom, color, scale, font, alignX, alignY, outlineSize)
    local outlineAlpha = math.floor(230 * selector.panelAmount)
    local outline = tocolor(5, 6, 8, outlineAlpha)
    local offset = math.max(1, round(outlineSize or 1))

    -- A complete contour keeps menu text legible over both the header and
    -- the changing game scene.
    for offsetY = -offset, offset, offset do
        for offsetX = -offset, offset, offset do
            if offsetX ~= 0 or offsetY ~= 0 then
                dxDrawText(text, left + offsetX, top + offsetY, right + offsetX, bottom + offsetY, outline, scale, font, alignX, alignY, false, false, false, false)
            end
        end
    end
    dxDrawText(text, left, top, right, bottom, color, scale, font, alignX, alignY, false, false, false, false)
end

local function drawBlurPass(source, target, stepX, stepY, desaturation, brightness)
    dxSetRenderTarget(target, true)
    dxSetShaderValue(graphics.blurShader, "sourceTexture", source)
    dxSetShaderValue(graphics.blurShader, "texelStep", stepX, stepY)
    dxSetShaderValue(graphics.blurShader, "desaturation", desaturation or 0)
    dxSetShaderValue(graphics.blurShader, "brightness", brightness or 1)
    dxDrawImage(0, 0, graphics.targetWidth, graphics.targetHeight, graphics.blurShader)
end

local function drawBlurredBackground(width, height, amount)
    local alpha = math.floor(255 * amount)
    if isElement(graphics.screenSource) and isElement(graphics.horizontalTarget) and isElement(graphics.verticalTarget) and isElement(graphics.blurShader) then
        -- Use Neon's stable back-buffer copy. Resampling the current D3D target
        -- can capture a native post-effect's intermediate target instead.
        dxUpdateScreenSource(graphics.screenSource, false)

        -- Downsample through the same explicit shader used by every later
        -- pass, avoiding fixed-function state inherited from native post-FX.
        -- Several tightly sampled Gaussian pairs then build a broad defocus
        -- without exposing samples as horizontal or vertical ghosts.
        local horizontalTexel = amount / graphics.targetWidth
        local verticalTexel = amount / graphics.targetHeight
        drawBlurPass(graphics.screenSource, graphics.horizontalTarget, 1.70 * amount / width, 0, 0, 1)
        drawBlurPass(graphics.horizontalTarget, graphics.verticalTarget, 0, 0.85 * verticalTexel, 0, 1)
        drawBlurPass(graphics.verticalTarget, graphics.horizontalTarget, 1.45 * horizontalTexel, 0, 0, 1)
        drawBlurPass(graphics.horizontalTarget, graphics.verticalTarget, 0, 1.45 * verticalTexel, 0, 1)
        drawBlurPass(graphics.verticalTarget, graphics.horizontalTarget, 2.05 * horizontalTexel, 0, 0, 1)
        drawBlurPass(graphics.horizontalTarget, graphics.verticalTarget, 0, 2.05 * verticalTexel, 0.32 * amount, 1 - 0.12 * amount)

        dxSetRenderTarget()
        dxDrawImage(0, 0, width, height, graphics.verticalTarget, 0, 0, 0, tocolor(255, 255, 255, alpha))
    end

    -- Keep the grade neutral and restrained. The captured scene can already
    -- include SkyGFX or another post-process, so a coloured wash here would
    -- amplify that resource's tint instead of merely defocusing the world.
    dxDrawRectangle(0, 0, width, height, tocolor(176, 176, 176, math.floor(14 * amount)))
    dxDrawRectangle(0, 0, width, height, tocolor(8, 8, 8, math.floor(16 * amount)))
end

local function drawStationTile(channel, centerX, centerY, size, alpha, focused, muteAmount)
    local station = RADIO_SELECTOR_STATIONS[channel]
    local logo = graphics.logos[channel]
    local left = centerX - size / 2
    local top = centerY - size / 2
    local logoAlpha = math.floor(alpha * (1 - 0.36 * muteAmount))
    local color = tocolor(255, 255, 255, logoAlpha)

    dxDrawRectangle(left - 2, top - 2, size + 4, size + 4, tocolor(239, 240, 243, focused and math.floor(alpha * (1 - 0.12 * muteAmount)) or math.floor(alpha * 0.13)))
    dxDrawRectangle(left, top, size, size, tocolor(118, 118, 121, math.floor(alpha * (0.74 + 0.10 * muteAmount))))
    if isElement(logo) then
        if isElement(graphics.logoShader) then
            dxSetShaderValue(graphics.logoShader, "sourceTexture", logo)
            dxSetShaderValue(graphics.logoShader, "desaturation", -0.10 * (1 - muteAmount) + 0.88 * muteAmount)
            dxSetShaderValue(graphics.logoShader, "brightness", 1.05 - 0.23 * muteAmount)
            dxDrawImage(left, top, size, size, graphics.logoShader, 0, 0, 0, color)
        else
            dxDrawImage(left, top, size, size, logo, 0, 0, 0, color)
        end
    else
        dxDrawRectangle(left, top, size, size, tocolor(38, 42, 48, alpha))
        drawOutlinedText(station.short, left, top, left + size, top + size, color, 0.78, "default-bold", "center", "center", 1)
    end

    if focused then
        dxDrawRectangle(left, top + size + 5, size, math.max(2, size * 0.025), tocolor(245, 247, 250, alpha))
    end
end

local function drawSelector(width, height, amount)
    local baseScale = math.min(width / 1920, height / 1080)
    local viewportLeft = (width - 1920 * baseScale) / 2
    local viewportTop = (height - 1080 * baseScale) / 2
    local rowY = viewportTop + 86 * baseScale
    local anchorX = viewportLeft + 960 * baseScale
    local stride = 124 * baseScale
    local baseSize = 108 * baseScale
    local selectedSize = 116 * baseScale
    local baseIndex = math.floor(selector.carouselPosition)
    local globalAlpha = math.floor(255 * amount)

    -- Tying the layout to a 1920x1080 design canvas preserves its framing on
    -- ultrawide and lower-resolution displays instead of stretching it.
    dxDrawRectangle(0, 0, width, viewportTop + 60 * baseScale, tocolor(0, 0, 0, math.floor(255 * amount)))

    local labelLeft = viewportLeft + 470 * baseScale
    local labelRight = viewportLeft + 578 * baseScale
    local railX = viewportLeft + 610 * baseScale
    local onDemandAmount = selector.onDemandAmount
    drawOutlinedText("RADIO", labelLeft, viewportTop + 31 * baseScale, labelRight, viewportTop + 61 * baseScale, tocolor(247, 248, 250, math.floor(globalAlpha * (1 - 0.28 * onDemandAmount))), 2.0 * baseScale, "default-bold", "right", "center", 2 * baseScale)
    drawOutlinedText("ON DEMAND", labelLeft - 16 * baseScale, viewportTop + 73 * baseScale, labelRight, viewportTop + 103 * baseScale, tocolor(235, 237, 241, math.floor(globalAlpha * (0.72 + 0.28 * onDemandAmount))), 1.45 * baseScale, "default-bold", "right", "center", 2 * baseScale)
    dxDrawRectangle(railX - baseScale, viewportTop + 48 * baseScale, 2 * baseScale, 42 * baseScale, tocolor(205, 208, 214, math.floor(globalAlpha * 0.58)))
    dxDrawCircle(railX, viewportTop + 48 * baseScale, (13 - 6 * onDemandAmount) * baseScale, 0, 360, tocolor(85 + 105 * onDemandAmount, 87 + 106 * onDemandAmount, 91 + 108 * onDemandAmount, globalAlpha))
    dxDrawCircle(railX, viewportTop + 90 * baseScale, (7 + 6 * onDemandAmount) * baseScale, 0, 360, tocolor(190 - 105 * onDemandAmount, 193 - 106 * onDemandAmount, 199 - 108 * onDemandAmount, globalAlpha))

    for rawIndex = baseIndex - 3, baseIndex + 3 do
        local distance = rawIndex - selector.carouselPosition
        local absoluteDistance = math.abs(distance)
        if absoluteDistance <= 2.55 then
            local focus = clamp(1 - absoluteDistance, 0, 1)
            local tileSize = baseSize + (selectedSize - baseSize) * focus
            local tileAlpha = math.floor(globalAlpha * (0.60 + 0.40 * clamp(1 - absoluteDistance / 2.55, 0, 1)))
            local centerX = anchorX + distance * stride
            drawStationTile(carouselIndexToChannel(rawIndex), centerX, rowY, tileSize, tileAlpha, focus > 0.82, selector.muteAmount)
        end
    end

    local muteLeft = anchorX + 344 * baseScale
    local muted = selector.muted
    dxDrawCircle(muteLeft, rowY, 16 * baseScale, 0, 360, tocolor(231, 233, 236, math.floor(globalAlpha * (muted and 0.84 or 0.68))))
    drawOutlinedText(muted and "UNMUTE" or "MUTE", muteLeft + 25 * baseScale, rowY - 18 * baseScale, muteLeft + 145 * baseScale, rowY + 18 * baseScale, tocolor(235, 237, 240, math.floor(globalAlpha * 0.88)), 1.55 * baseScale, "default-bold", "left", "center", 2 * baseScale)

    local station = RADIO_SELECTOR_STATIONS[selector.selectedChannel]
    local title, artist = getPlaybackMetadata(selector.selectedChannel)
    local metadataTop = viewportTop + 162 * baseScale
    local metadataLeft = anchorX - 230 * baseScale
    local metadataRight = anchorX + 230 * baseScale
    dxDrawRectangle(anchorX - 101 * baseScale, metadataTop, 202 * baseScale, math.max(2, 3 * baseScale), tocolor(239, 241, 245, math.floor(globalAlpha * 0.68)))
    drawOutlinedText(string.upper(station.name), metadataLeft, metadataTop + 11 * baseScale, metadataRight, metadataTop + 43 * baseScale, tocolor(248, 249, 251, globalAlpha), 2.05 * baseScale, "default-bold", "center", "center", 2 * baseScale)
    drawOutlinedText(string.upper(title), metadataLeft, metadataTop + 49 * baseScale, metadataRight, metadataTop + 75 * baseScale, tocolor(239, 241, 245, math.floor(globalAlpha * 0.92)), 1.60 * baseScale, "default-bold", "center", "center", 2 * baseScale)
    if artist ~= "" then
        drawOutlinedText(string.upper(artist), metadataLeft, metadataTop + 73 * baseScale, metadataRight, metadataTop + 99 * baseScale, tocolor(210, 214, 220, math.floor(globalAlpha * 0.78)), 1.45 * baseScale, "default-bold", "center", "center", 2 * baseScale)
    end
end

local function updateAnimation(now)
    local elapsed = clamp((now - selector.lastFrame) / 1000, 0, 0.1)
    selector.lastFrame = now

    if selector.openTarget == 1 and not selector.forcePreview and now - selector.lastInteraction >= IDLE_DURATION_MS then
        selector.openTarget = 0
    end

    local panelDuration = selector.openTarget == 1 and PANEL_OPEN_DURATION or PANEL_CLOSE_DURATION
    local blurDuration = selector.openTarget == 1 and BLUR_OPEN_DURATION or BLUR_CLOSE_DURATION
    local panelChange = elapsed / panelDuration
    local blurChange = elapsed / blurDuration
    selector.panelAmount = approach(selector.panelAmount, selector.openTarget, panelChange)
    selector.blurAmount = approach(selector.blurAmount, selector.openTarget, blurChange)

    local muteTarget = selector.muted and 1 or 0
    local muteResponse = 1 - math.exp(-20 * elapsed)
    selector.muteAmount = selector.muteAmount + (muteTarget - selector.muteAmount) * muteResponse

    local onDemandTarget = selector.selectedChannel == STATION_COUNT and 1 or 0
    selector.onDemandAmount = selector.onDemandAmount + (onDemandTarget - selector.onDemandAmount) * muteResponse

    local response = 1 - math.exp(-CAROUSEL_RESPONSE * elapsed)
    selector.carouselPosition = selector.carouselPosition + (selector.carouselTarget - selector.carouselPosition) * response
    if math.abs(selector.carouselTarget - selector.carouselPosition) < 0.001 then
        selector.carouselPosition = selector.carouselTarget
    end
end

local function renderRadioSelector()
    local now = getTickCount()
    updateAnimation(now)
    if selector.panelAmount <= 0 and selector.blurAmount <= 0 then
        if selector.skyGfxColorFilterSuspended and type(resetNeonClientSettings) == "function" then
            resetNeonClientSettings()
            selector.skyGfxColorFilterSuspended = false
        end
        return
    end

    -- The setting changes after GTA has already rendered the current frame.
    -- Waiting once prevents the blur from sampling that last filtered frame.
    if selector.skipFilteredFrame then
        selector.skipFilteredFrame = false
        return
    end

    if not selector.forcePreview and not getPedOccupiedVehicle(localPlayer) then
        closeSelector()
    end

    local actualChannel = getRadioChannel()
    if actualChannel == 0 then
        selector.muted = true
    elseif isStationChannel(actualChannel) then
        selector.muted = false
    end
    if actualChannel > 0 and actualChannel ~= selector.selectedChannel then
        setCarouselChannel(actualChannel, false)
    end

    local width, height = ensureGraphics()
    local blurEasedAmount = 1 - (1 - selector.blurAmount) ^ 3
    local panelEasedAmount = 1 - (1 - selector.panelAmount) ^ 3
    drawBlurredBackground(width, height, blurEasedAmount)
    if panelEasedAmount > 0 then
        drawSelector(width, height, panelEasedAmount)
    end
end

local function deferNativeRadioChannel(requestedChannel, expectedCurrentChannel)
    if selector.pendingNativeNavigation then
        return
    end

    selector.pendingNativeNavigation = true
    setTimer(function()
        selector.pendingNativeNavigation = false
        if getRadioChannel() == expectedCurrentChannel then
            setAcceptedRadioChannel(requestedChannel)
        end
    end, 0, 1)
end

addEventHandler("onClientPlayerRadioSwitch", root, function(channel)
    if source ~= localPlayer or type(channel) ~= "number" then
        return
    end

    local current = getRadioChannel()
    if selector.muted and current == 0 and not selector.applyingRequestedChannel and (channel == FIRST_STATION or channel == STATION_COUNT) then
        -- GTA cycles from OFF itself (0 -> 1 or 0 -> 12), which forgets the
        -- retained station. Cancel that boundary jump and
        -- continue from the retained station instead. This event is cancellable
        -- in MTA and also fires while the selector panel is closed.
        cancelEvent()
        local direction = channel == FIRST_STATION and 1 or -1
        local requestedChannel = stepStationChannel(selector.lastAudibleChannel, direction)
        deferNativeRadioChannel(requestedChannel, 0)
        return
    end

    if channel == 0 and not selector.applyingRequestedChannel and (current == FIRST_STATION or current == STATION_COUNT) then
        -- OFF is an implementation channel, not a carousel item. Native GTA
        -- reaches it at either end of the station list; explicit mute requests
        -- are marked by setAcceptedRadioChannel and remain untouched.
        cancelEvent()
        local requestedChannel = current == FIRST_STATION and STATION_COUNT or FIRST_STATION
        deferNativeRadioChannel(requestedChannel, current)
        return
    end

    openSelector(channel, false)
end)

local function noteNativeRadioInput()
    if getPedOccupiedVehicle(localPlayer) then
        openSelector(getRadioChannel(), selector.panelAmount <= 0.01)
    end
end

bindKey("radio_next", "down", noteNativeRadioInput)
bindKey("radio_previous", "down", noteNativeRadioInput)

addEventHandler("onClientKey", root, function(button, pressed)
    if not pressed or selector.panelAmount < 0.2 or isInputWindowActive() then
        return
    end

    if button == "m" then
        cancelEvent()
        toggleMute()
    elseif button == "escape" then
        closeSelector()
    end
end)

addEventHandler("onClientPlayerVehicleExit", localPlayer, function()
    closeSelector()
end)

addEventHandler("onClientPlayerWasted", localPlayer, function()
    closeSelector()
end)

addEventHandler("onClientMinimize", root, function()
    closeSelector()
end)

addEventHandler("onClientRestore", root, function()
    local width, height = guiGetScreenSize()
    createRenderTargets(width, height)
end)

addEventHandler("onClientHUDRender", root, renderRadioSelector, false, "high+100")

addCommandHandler("radioselector", function()
    if selector.openTarget == 1 then
        closeSelector()
        return
    end

    selector.forcePreview = true
    openSelector(getRadioChannel(), true)
end)

addCommandHandler("radiomute", toggleMute)

addCommandHandler("radioui", function(_, channel)
    local requested = tonumber(channel)
    if requested and requested >= 0 and requested <= STATION_COUNT then
        setAcceptedRadioChannel(math.floor(requested))
        return
    end

    outputChatBox("[radio-selector] Usage: /radioui 0-12", 190, 220, 255)
end)

addEventHandler("onClientResourceStart", resourceRoot, function()
    local current = getRadioChannel()
    selector.selectedChannel = current > 0 and current or FIRST_STATION
    selector.lastAudibleChannel = current > 0 and current or 1
    selector.muted = current == 0
    selector.muteAmount = selector.muted and 1 or 0
    selector.onDemandAmount = selector.selectedChannel == STATION_COUNT and 1 or 0
    selector.carouselPosition = selector.selectedChannel
    selector.carouselTarget = selector.selectedChannel
    selector.nativeRadioHudWasVisible = isPlayerHudComponentVisible("radio")
    setPlayerHudComponentVisible("radio", false)
    ensureGraphics()
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    unbindKey("radio_next", "down", noteNativeRadioInput)
    unbindKey("radio_previous", "down", noteNativeRadioInput)
    setPlayerHudComponentVisible("radio", selector.nativeRadioHudWasVisible)
    if selector.skyGfxColorFilterSuspended and type(resetNeonClientSettings) == "function" then
        resetNeonClientSettings()
    end
    destroyGraphics()
end)
