local CONFIG_PATH = "neon-agent-proof-config.json"
local REPORT_PATH = "neon-agent-proof-report.json"
local REPORT_TEMP_PATH = "neon-agent-proof-report.tmp"
local observations = {}

local function readDocument(path, maximum)
    local handle = fileOpen(path, true)
    if not handle then
        return false
    end
    local size = fileGetSize(handle)
    if size < 1 or size > maximum then
        fileClose(handle)
        return false
    end
    local payload = fileRead(handle, size)
    fileClose(handle)
    return fromJSON(payload)
end

local function bounded(value, maximum)
    value = tostring(value or "unknown")
    return value:sub(1, maximum)
end

local function validHex(value, length)
    return type(value) == "string" and #value == length and not value:find("[^0-9a-f]")
end

local function validConfig(value)
    return type(value) == "table"
        and value.schemaVersion == "1.0.0"
        and type(value.sessionId) == "string"
        and validHex(value.challenge, 64)
        and validHex(value.secret, 64)
        and validHex(value.projectSha256, 64)
        and validHex(value.catalogueSha256, 64)
        and (value.profile == "neon-pair" or value.profile == "neon-multiclient")
        and type(value.expectedClients) == "number"
        and value.expectedClients >= 1 and value.expectedClients <= 8
        and type(value.issuedUnix) == "number"
        and type(value.expiresUnix) == "number"
        and value.issuedUnix <= getRealTime().timestamp
        and value.expiresUnix >= getRealTime().timestamp
end

local config = readDocument(CONFIG_PATH, 8192)
if not validConfig(config) then
    outputDebugString("Neon runtime probe refused an invalid or expired private configuration", 2)
    config = false
end

local function engineVersion(version)
    local candidate = bounded(version and (version.sortable or version.mta or version.number), 64)
    return candidate:match("(%d+%.%d+%.%d+)") or "0.0.0"
end

local function signaturePayload(report)
    local fields = {
        report.sessionId,
        report.challenge,
        report.profile,
        report.projectSha256,
        report.catalogueSha256,
        tostring(report.observedUnix),
        tostring(report.expectedClients),
        report.server.engineVersion,
        report.server.buildId,
    }
    for _, observation in ipairs(report.clients) do
        fields[#fields + 1] = tostring(observation.ordinal)
        fields[#fields + 1] = observation.engineVersion
        fields[#fields + 1] = observation.buildId
        fields[#fields + 1] = observation.nonce
    end
    return table.concat(fields, "\n")
end

local function writeReport()
    if not config then
        return
    end

    local clients = {}
    for _, observation in pairs(observations) do
        clients[#clients + 1] = observation
    end
    if #clients < config.expectedClients then
        return
    end
    table.sort(clients, function(left, right)
        return left.nonce < right.nonce
    end)
    for ordinal, observation in ipairs(clients) do
        observation.ordinal = ordinal
    end

    local version = getVersion()
    local report = {
        schemaVersion = "1.0.0",
        sessionId = config.sessionId,
        challenge = config.challenge,
        profile = config.profile,
        projectSha256 = config.projectSha256,
        catalogueSha256 = config.catalogueSha256,
        observedUnix = getRealTime().timestamp,
        expectedClients = config.expectedClients,
        server = {
            engineVersion = engineVersion(version),
            buildId = bounded(version and (version.sortable or version.number), 128),
        },
        clients = clients,
    }
    report.authorization = hash("hmac", signaturePayload(report), {
        algorithm = "sha256",
        key = config.secret,
    })

    if fileExists(REPORT_TEMP_PATH) then
        fileDelete(REPORT_TEMP_PATH)
    end
    local handle = fileCreate(REPORT_TEMP_PATH)
    if not handle then
        outputDebugString("Neon runtime probe could not create its private report", 1)
        return
    end
    local encoded = toJSON(report, true)
    if encoded:sub(1, 1) == "[" and encoded:sub(-1) == "]" then
        encoded = encoded:sub(2, -2)
    end
    fileWrite(handle, encoded)
    fileFlush(handle)
    fileClose(handle)
    -- Readers must see a complete old report or no report, never a partially
    -- written JSON document while this resource refreshes the evidence.
    if fileExists(REPORT_PATH) then
        fileDelete(REPORT_PATH)
    end
    if not fileRename(REPORT_TEMP_PATH, REPORT_PATH) then
        fileDelete(REPORT_TEMP_PATH)
        outputDebugString("Neon runtime probe could not publish its private report", 1)
    end
end

addEvent("neonAgentProof:client", true)
addEventHandler("neonAgentProof:client", resourceRoot, function()
    if not config or source ~= resourceRoot or not isElement(client) then
        return
    end
    local version = getPlayerVersion(client)
    if type(version) ~= "string" or #version < 1 or #version > 128 then
        return
    end
    local entropy = table.concat({
        bounded(getPlayerSerial(client), 64),
        bounded(getPlayerName(client), 64),
        tostring(getTickCount()),
        version,
    }, "|")
    observations[client] = {
        engineVersion = engineVersion({ sortable = version }),
        buildId = bounded(version, 128),
        nonce = hash("sha256", entropy),
    }
    writeReport()
end)

addEventHandler("onPlayerQuit", root, function()
    observations[source] = nil
    -- A report is an observation of the currently connected topology. Once a
    -- contributing player leaves, it must not remain reusable until the exact
    -- topology has been observed and signed again.
    if fileExists(REPORT_PATH) then
        fileDelete(REPORT_PATH)
    end
end)

addEventHandler("onResourceStop", resourceRoot, function()
    if fileExists(REPORT_PATH) then
        fileDelete(REPORT_PATH)
    end
end)

-- A short heartbeat makes proof freshness about an active resource, not merely
-- a report produced at some earlier point in the session TTL.
setTimer(writeReport, 1000, 0)
