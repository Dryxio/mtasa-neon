MirrorMaze = {
    baseX = 7000,
    baseY = 7000,
    floorZ = 100,
    dimension = 4242,
    interior = 0,
    gridSize = 5,
    cellSize = 10,
    roomHeight = 8,
    panelModel = 3095,
    seed = 23063,
}

local maze = MirrorMaze

maze.minX = maze.baseX - maze.gridSize * maze.cellSize * 0.5
maze.minY = maze.baseY - maze.gridSize * maze.cellSize * 0.5

local directions = {
    { name = "west", dx = -1, dy = 0, axis = "x" },
    { name = "east", dx = 1, dy = 0, axis = "x" },
    { name = "south", dx = 0, dy = -1, axis = "y" },
    { name = "north", dx = 0, dy = 1, axis = "y" },
}

function maze.cellIndex(x, y)
    return y * maze.gridSize + x + 1
end

function maze.cellCenter(x, y)
    return maze.minX + (x + 0.5) * maze.cellSize, maze.minY + (y + 0.5) * maze.cellSize
end

function maze.edgeKey(ax, ay, bx, by)
    local first = maze.cellIndex(ax, ay)
    local second = maze.cellIndex(bx, by)
    if first > second then
        first, second = second, first
    end
    return first .. ":" .. second
end

function maze.hasPassage(passages, ax, ay, bx, by)
    return passages[maze.edgeKey(ax, ay, bx, by)] == true
end

function maze.generatePassages()
    local passages = {}
    local visited = { [maze.cellIndex(0, 0)] = true }
    local stack = { { x = 0, y = 0 } }
    local randomState = maze.seed

    local function choose(count)
        randomState = randomState * 48271 % 2147483647
        return randomState % count + 1
    end

    while #stack > 0 do
        local current = stack[#stack]
        local candidates = {}
        for _, direction in ipairs(directions) do
            local nextX = current.x + direction.dx
            local nextY = current.y + direction.dy
            if nextX >= 0 and nextX < maze.gridSize and nextY >= 0 and nextY < maze.gridSize and not visited[maze.cellIndex(nextX, nextY)] then
                candidates[#candidates + 1] = { x = nextX, y = nextY }
            end
        end

        if #candidates == 0 then
            table.remove(stack)
        else
            local nextCell = candidates[choose(#candidates)]
            passages[maze.edgeKey(current.x, current.y, nextCell.x, nextCell.y)] = true
            visited[maze.cellIndex(nextCell.x, nextCell.y)] = true
            stack[#stack + 1] = nextCell
        end
    end

    return passages
end

function maze.isClosed(passages, x, y, direction)
    local nextX = x + direction.dx
    local nextY = y + direction.dy
    if nextX < 0 or nextX >= maze.gridSize or nextY < 0 or nextY >= maze.gridSize then
        return true
    end
    return not maze.hasPassage(passages, x, y, nextX, nextY)
end

local function makeWall(x, y, direction)
    local centerX, centerY = maze.cellCenter(x, y)
    if direction.name == "west" then
        return { name = direction.name, axis = "x", value = centerX - maze.cellSize * 0.5, internal = x > 0 }
    elseif direction.name == "east" then
        return { name = direction.name, axis = "x", value = centerX + maze.cellSize * 0.5, internal = x < maze.gridSize - 1 }
    elseif direction.name == "south" then
        return { name = direction.name, axis = "y", value = centerY - maze.cellSize * 0.5, internal = y > 0 }
    end
    return { name = direction.name, axis = "y", value = centerY + maze.cellSize * 0.5, internal = y < maze.gridSize - 1 }
end

function maze.chooseMirrorWall(passages, x, y)
    local internal = {}
    local boundary = {}
    for _, direction in ipairs(directions) do
        if maze.isClosed(passages, x, y, direction) then
            local wall = makeWall(x, y, direction)
            local target = wall.internal and internal or boundary
            target[#target + 1] = wall
        end
    end

    local choices = #internal > 0 and internal or boundary
    local index = (maze.cellIndex(x, y) * 7 + maze.seed) % #choices + 1
    local wall = choices[index]
    wall.normalX = wall.axis == "x" and 1 or 0
    wall.normalY = wall.axis == "y" and 1 or 0
    wall.normalZ = 0
    return wall
end

function maze.cellFromPoint(x, y)
    local cellX = math.floor((x - maze.minX) / maze.cellSize)
    local cellY = math.floor((y - maze.minY) / maze.cellSize)
    if cellX < 0 or cellX >= maze.gridSize or cellY < 0 or cellY >= maze.gridSize then
        return nil
    end
    return cellX, cellY
end

maze.directions = directions
