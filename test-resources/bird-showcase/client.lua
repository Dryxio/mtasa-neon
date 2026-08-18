local show = {
    active = false,
    startedAt = 0,
    cx = 0, cy = 0, cz = 0,
    dimension = 0,
    birds = {},
    targets = {},
    hero = nil,
    finished = false,
    shootBirds = {},
    shootUntil = 0,
}

local patterns = {
    {"10001","11001","10101","10011","10001","10001","10001"},
    {"11111","10000","10000","11110","10000","10000","11111"},
    {"01110","10001","10001","10001","10001","10001","01110"},
    {"10001","11001","10101","10011","10001","10001","10001"},
}

local function clamp(v, a, b) return math.max(a, math.min(b, v)) end
local function smoothstep(t) t = clamp(t, 0, 1); return t * t * (3 - 2 * t) end
local function lerp(a, b, t) return a + (b - a) * t end

local function cameraLerp(a, b, t)
    t = smoothstep(t)
    setCameraMatrix(
        lerp(a[1],b[1],t), lerp(a[2],b[2],t), lerp(a[3],b[3],t),
        lerp(a[4],b[4],t), lerp(a[5],b[5],t), lerp(a[6],b[6],t), 0,
        lerp(a[7] or 70,b[7] or 70,t)
    )
end

local function presentation(visible)
    showPlayerHudComponent("all", visible)
    showChat(visible)
end

local function destroyList(list)
    for _, element in ipairs(list) do
        if isElement(element) then destroyElement(element) end
    end
end

local function stopShow()
    destroyList(show.birds)
    show.birds, show.targets, show.hero = {}, {}, nil
    show.active = false
    show.finished = false
    setCameraTarget(localPlayer)
    presentation(true)
end

local function formationPoints(cx, cy, cz)
    local points = {}
    local step = 1.45
    local gap = 2
    local totalColumns = 4 * 5 + 3 * gap
    local firstX = cx - ((totalColumns - 1) * step * 0.5)
    for letterIndex, rows in ipairs(patterns) do
        local base = (letterIndex - 1) * (5 + gap)
        for rowIndex, row in ipairs(rows) do
            for col = 1, #row do
                if row:sub(col,col) == "1" then
                    local x = firstX + (base + col - 1) * step
                    local z = cz + (7 - rowIndex) * step
                    points[#points + 1] = {x, cy + 4.0, z}
                end
            end
        end
    end
    return points
end

local function spawnFlock(cx, cy, cz, dimension)
    local targets = formationPoints(cx, cy, cz)
    for i = 1, 96 do
        local col = (i - 1) % 16
        local row = math.floor((i - 1) / 16)
        local x = cx - 18 + col * 2.4 + math.sin(i * 1.7) * 0.8
        local y = cy + 52 + row * 1.2
        local z = cz - 2 + (i % 9) * 0.75
        local bird = createBird(x, y, z, {
            preset = (i % 11 == 0) and "water" or "normal",
            velocity = {math.sin(i) * 0.4, -9.0 - (i % 5) * 0.25, math.cos(i * 0.7) * 0.2},
            targetVelocity = {0, -9.5, 0},
            size = 0.62 + (i % 4) * 0.035,
            renderDistance = 220,
            bodyColor = {205, 215, 225},
            wingColor = {245, 248, 255},
            wingBeatTime = 300 + (i % 7) * 35,
            shootable = true,
        })
        if isElement(bird) then
            setElementDimension(bird, dimension)
            show.birds[#show.birds + 1] = bird
            if i == 1 then show.hero = bird end
            if targets[i] then
                show.targets[i] = targets[i]
            else
                local a = ((i - #targets) / math.max(1, 96 - #targets)) * math.pi * 2
                show.targets[i] = {cx + math.cos(a) * 18, cy + 5.5, cz + 12 + math.sin(a) * 3.5}
            end
        end
    end
end

local function steerTo(bird, tx, ty, tz, strength, maxSpeed)
    if not isElement(bird) then return 0 end
    local x,y,z = getElementPosition(bird)
    local dx,dy,dz = tx-x,ty-y,tz-z
    local d = math.sqrt(dx*dx+dy*dy+dz*dz)
    if d < 0.001 then
        setBirdVelocity(bird, Vector3(0,0,0))
        setBirdTargetVelocity(bird, Vector3(0,0,0))
        return d
    end
    local vx,vy,vz = dx*strength,dy*strength,dz*strength
    local speed = math.sqrt(vx*vx+vy*vy+vz*vz)
    if speed > maxSpeed then
        vx,vy,vz = vx/speed*maxSpeed,vy/speed*maxSpeed,vz/speed*maxSpeed
    end
    setBirdTargetVelocity(bird, Vector3(vx,vy,vz))
    return d
end

local function caption(text, sub)
    local w,h = guiGetScreenSize()
    dxDrawText(text, 0, h*0.78, w, h*0.86, tocolor(255,255,255,245), 1.55, "default-bold", "center", "center", false, false, true)
    if sub then
        dxDrawText(sub, 0, h*0.85, w, h*0.91, tocolor(220,225,235,230), 1.0, "default", "center", "center", false, false, true)
    end
end

local function updateShow()
    if not show.active then return end
    local elapsed = getTickCount() - show.startedAt
    local cx,cy,cz = show.cx,show.cy,show.cz

    if elapsed < 6500 then
        for i,bird in ipairs(show.birds) do
            if isElement(bird) then
                setBirdCurvedFlightEnabled(bird, false)
                setBirdTargetVelocity(bird, Vector3(math.sin(i*1.31)*0.6, -9.5, math.cos(i*0.63)*0.25))
            end
        end
    elseif elapsed < 13500 then
        for i,bird in ipairs(show.birds) do
            if isElement(bird) then
                local x,y,z = getElementPosition(bird)
                local side = (i % 2 == 0) and -1 or 1
                local ox,oy = cx + side*8, cy + 1
                local dx,dy = x-ox,y-oy
                local len = math.max(math.sqrt(dx*dx+dy*dy), 0.5)
                local tangentX,tangentY = -dy/len*side,dx/len*side
                local pull = clamp((len-7)*0.32,-2.0,3.5)
                local vx = tangentX*7 - dx/len*pull
                local vy = tangentY*7 - dy/len*pull
                local vz = (cz + 4 + math.sin(i*0.5)*3 - z)*0.9
                setBirdTargetVelocity(bird, Vector3(vx,vy,vz))
            end
        end
    elseif elapsed < 19000 then
        if isElement(show.hero) then
            setBirdColors(show.hero, 255,55,55,255,210,210)
            setBirdSize(show.hero, 1.8)
            local d = steerTo(show.hero, cx+1,cy-4,cz+4,1.4,8)
            if d < 0.35 then
                setBirdMovementEnabled(show.hero, false)
            end
        end
        for i=2,#show.birds do
            local bird = show.birds[i]
            if isElement(bird) then
                local a = i*0.38 + elapsed/1450
                steerTo(bird,cx+math.cos(a)*12,cy+3+math.sin(a)*7,cz+5+math.sin(a*1.7)*4,0.75,8)
            end
        end
    elseif elapsed < 29200 then
        for i,bird in ipairs(show.birds) do
            local target = show.targets[i]
            if isElement(bird) and target then
                if bird == show.hero and not isBirdMovementEnabled(bird) then setBirdMovementEnabled(bird,true) end
                if bird == show.hero then setBirdSize(bird,0.72); setBirdColors(bird,225,230,238,255,255,255) end
                setBirdCurvedFlightEnabled(bird,false)
                local d = steerTo(bird,target[1],target[2],target[3],1.15,10)
                if d < 0.18 then
                    setElementPosition(bird,target[1],target[2],target[3])
                    setBirdVelocity(bird,Vector3(0,0,0))
                    setBirdTargetVelocity(bird,Vector3(0,0,0))
                    setBirdMovementEnabled(bird,false)
                end
            end
        end
    end

    if elapsed < 6500 then
        cameraLerp({cx,cy-20,cz+3,cx,cy+8,cz+3,82},{cx+2,cy-14,cz+5,cx,cy+9,cz+4,76},elapsed/6500)
        if elapsed < 2600 then caption("SCRIPTABLE BIRDS") else caption("96 LUA-CONTROLLED BIRDS","GTA SA's native ambient pool is 6") end
    elseif elapsed < 13500 then
        local t=(elapsed-6500)/7000
        cameraLerp({cx+2,cy-14,cz+5,cx,cy+4,cz+4,76},{cx-14,cy-8,cz+9,cx,cy+2,cz+5,68},t)
        caption("LIVE FLIGHT CONTROL","velocity • target velocity • curved flight")
    elseif elapsed < 19000 then
        local t=(elapsed-13500)/5500
        cameraLerp({cx-14,cy-8,cz+9,cx,cy+2,cz+5,68},{cx+5,cy-12,cz+5,cx+1,cy-4,cz+4,58},t)
        caption("EVERY BIRD IS AN ELEMENT","one bird changes color, size, motion — live")
    else
        local t=clamp((elapsed-19000)/10000,0,1)
        cameraLerp({cx+5,cy-12,cz+5,cx,cy+4,cz+5,58},{cx,cy-28,cz+11,cx,cy+4,cz+5,70},t)
        caption(elapsed < 25500 and "96 BIRDS → ONE SCRIPTED FORMATION" or "SCRIPTABLE BIRDS","POSITION  •  FLIGHT  •  SIZE  •  COLOR  •  SHOT EVENTS")
    end

    if elapsed > 30500 and not show.finished then
        show.finished = true
        triggerServerEvent("birdShowcase:finished", resourceRoot)
    end
end

addEvent("birdShowcase:start", true)
addEventHandler("birdShowcase:start", resourceRoot, function(cx,cy,cz,dimension)
    stopShow()
    show.active=true; show.startedAt=getTickCount(); show.cx=cx; show.cy=cy; show.cz=cz; show.dimension=dimension; show.finished=false
    spawnFlock(cx,cy,cz,dimension)
    presentation(false)
    setTime(18,30)
end)

addEvent("birdShowcase:stop", true)
addEventHandler("birdShowcase:stop", resourceRoot, stopShow)

addEvent("birdShowcase:shoot", true)
addEventHandler("birdShowcase:shoot", resourceRoot, function()
    destroyList(show.shootBirds); show.shootBirds={}
    local x,y,z=getElementPosition(localPlayer)
    local _,_,rz=getElementRotation(localPlayer)
    local a=math.rad(rz); local fx,fy=-math.sin(a),math.cos(a); local rx,ry=fy,-fx
    for i=1,12 do
        local d=12+(i%4)*3
        local bird=createBird(x+fx*d+rx*(i-6.5)*1.4,y+fy*d+ry*(i-6.5)*1.4,z+2.2+(i%3)*1.2,{
            velocity={0,0,0},targetVelocity={0,0,0},movementEnabled=false,size=i==6 and 2.0 or 0.9,
            bodyColor=i==6 and {255,60,60} or {210,215,225},wingColor={255,255,255},renderDistance=100,shootable=true,
        })
        if isElement(bird) then show.shootBirds[#show.shootBirds+1]=bird end
    end
    show.shootUntil=getTickCount()+12000
    outputChatBox("[BIRD SHOWCASE] Shoot the flock. Each hit is a cancellable onClientBirdShot event.",255,210,80)
end)

addEventHandler("onClientBirdShot", root, function(attacker,weapon)
    if show.shootUntil>getTickCount() then
        outputChatBox(("[BIRD SHOWCASE] onClientBirdShot — weapon %s"):format(tostring(weapon)),120,255,140)
    end
end)

addEventHandler("onClientPlayerWeaponFire", localPlayer, function(weapon,ammo,ammoInClip,hitX,hitY,hitZ,hitElement,startX,startY,startZ)
    if not hitX or not startX then return end
    processBirdGunShot(Vector3(startX,startY,startZ),Vector3(hitX,hitY,hitZ),weapon)
end)

addEventHandler("onClientRender", root, function()
    updateShow()
    if show.shootUntil>getTickCount() and not show.active then
        caption("SHOOTABLE LUA BIRDS","onClientBirdShot is cancellable")
    end
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    stopShow(); destroyList(show.shootBirds); show.shootBirds={}
end)
