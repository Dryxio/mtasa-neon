local mission
local serial=0

local function snap(p)
    local x,y,z=getElementPosition(p); local rx,ry,rz=getElementRotation(p)
    return {x=x,y=y,z=z,rx=rx,ry=ry,rz=rz,dim=getElementDimension(p),int=getElementInterior(p),alpha=getElementAlpha(p),frozen=isElementFrozen(p)}
end
local function restore(p,s)
    if not isElement(p) then return end; if isPedInVehicle(p) then removePedFromVehicle(p) end
    setElementInterior(p,s.int); setElementDimension(p,s.dim); setElementPosition(p,s.x,s.y,s.z); setElementRotation(p,s.rx,s.ry,s.rz); setElementAlpha(p,s.alpha); setElementFrozen(p,s.frozen)
end
local function destroyMission(restorePlayers)
    if not mission then return end
    if isTimer(mission.monitor) then killTimer(mission.monitor) end
    for p,s in pairs(mission.snapshots) do if isElement(p) then if restorePlayers then restore(p,s) end end end
    for _,e in ipairs(mission.entities) do if isElement(e) then destroyElement(e) end end
    mission=nil
end
local function participants() local t={} for p in pairs(mission.snapshots) do if isElement(p) then t[#t+1]=p end end return t end
local function allNear(x,y,z,r)
    for _,p in ipairs(participants()) do local px,py,pz=getElementPosition(p); if getDistanceBetweenPoints3D(px,py,pz,x,y,z)>r then return false end end
    return true
end
local function owner() return mission and mission.owner end
local function track(e) mission.entities[#mission.entities+1]=e; setElementDimension(e,SAK.dimension); return e end
local function sync(e,p) if isElement(e) and isElement(p) then setElementSyncer(e,p,true,true) end end

local function playCutscene(name,nextPhase)
    mission.phase=nextPhase; mission.cutsceneId=mission.cutsceneId+1; mission.cutsceneWaiting={}
    for _,p in ipairs(participants()) do mission.cutsceneWaiting[p]=true; triggerClientEvent(p,"sak:playCutscene",resourceRoot,mission.cutsceneId,name) end
end

local function assignRoute(role,route)
    local a=mission.gang[role]; if not a then return end
    sync(a.ped,owner()); sync(a.bike,owner()); triggerClientEvent(owner(),"sak:route",resourceRoot,mission.id,a.ped,a.bike,route)
end

local function beginRide()
    mission.phase="ride1"
    local starts={smoke={969.2,-1110.3,22.87,110},ryder={971.8,-1115.5,23.1,140},sweet={971.8,-1113.0,23.1,140}}
    mission.gang={}
    for role,cfg in pairs(SAK.gang) do
        local s=starts[role]; local bike=track(createVehicle(SAK.bikeModel,s[1],s[2],s[3],0,0,s[4])); local ped=track(createPed(cfg.model,s[1],s[2],s[3]+0.5,s[4]))
        warpPedIntoVehicle(ped,bike,0); setElementHealth(ped,2000); setElementHealth(bike,2000); mission.gang[role]={ped=ped,bike=bike}
    end
    mission.playerBikes={}
    local offset=0
    for _,p in ipairs(participants()) do
        local bike=track(createVehicle(SAK.bikeModel,970.1+offset,-1107.8,23.1,0,0,254.5)); mission.playerBikes[p]=bike
        setElementPosition(p,970.1+offset,-1107.8,23.4); warpPedIntoVehicle(p,bike,0); setElementFrozen(p,false); setElementAlpha(p,255); offset=offset+1.6
    end
    local b=SAK.ballas; local voodoo=track(createVehicle(b.voodooModel,b.start[1],b.start[2],b.start[3],0,0,b.start[4])); local d=track(createPed(b.driverModel,b.start[1],b.start[2],b.start[3]+1,b.start[4])); local q=track(createPed(b.passengerModel,b.start[1],b.start[2],b.start[3]+1,b.start[4]))
    warpPedIntoVehicle(d,voodoo,0); warpPedIntoVehicle(q,voodoo,1); giveWeapon(q,b.weapon,30000,true); setElementHealth(voodoo,2000)
    mission.voodoo={vehicle=voodoo,driver=d,passenger=q}; for _,e in ipairs({voodoo,d,q}) do sync(e,owner()) end
    for _,a in pairs(mission.gang) do sync(a.ped,owner()); sync(a.bike,owner()) end
    assignRoute("smoke",SAK.route1); assignRoute("ryder",SAK.route1); assignRoute("sweet",SAK.route1)
    local leaderBike=mission.playerBikes[mission.leader]
    triggerClientEvent(owner(),"sak:voodoo",resourceRoot,mission.id,d,q,voodoo,leaderBike,mission.leader)
    triggerClientEvent(root,"sak:message",resourceRoot,"Get on the bike and follow Sweet.")
end

local function splitRide()
    if mission.phase~="ride1" then return end; mission.phase="ride2"
    assignRoute("ryder",SAK.route2); assignRoute("smoke",SAK.smokeRoute2); assignRoute("sweet",SAK.route2)
    local v=mission.voodoo; if v and isElement(v.driver) then triggerClientEvent(owner(),"sak:voodoo",resourceRoot,mission.id,v.driver,v.passenger,v.vehicle,mission.playerBikes[mission.leader],mission.leader) end
    triggerClientEvent(root,"sak:message",resourceRoot,"Split up! Follow Ryder back to Grove Street.")
end

local function passMission()
    if mission.phase=="passed" then return end; mission.phase="passed"
    outputChatBox("[Sweet & Kendl] MISSION PASSED - WIP conformance path complete.",root,100,240,120)
    setTimer(function() destroyMission(true) end,3000,1)
end

local function startMonitor()
    mission.monitor=setTimer(function()
        if not mission then return end
        for role,a in pairs(mission.gang or {}) do if not isElement(a.ped) or isPedDead(a.ped) or not isElement(a.bike) or isVehicleBlown(a.bike) then outputChatBox("[Sweet & Kendl] Mission failed: "..role,root,255,80,80); return destroyMission(true) end end
        for p,bike in pairs(mission.playerBikes or {}) do if not isElement(p) or not isElement(bike) or isVehicleBlown(bike) then return destroyMission(true) end end
        if mission.phase=="ride1" and allNear(SAK.split[1],SAK.split[2],SAK.split[3],35) then splitRide()
        elseif mission.phase=="ride2" and allNear(SAK.grove[1],SAK.grove[2],SAK.grove[3],45) then passMission() end
    end,250,0)
end

addCommandHandler("sweetandkendl",function(player)
    destroyMission(true); serial=serial+1
    mission={id=serial,leader=player,owner=player,phase="intro",cutsceneId=0,entities={},snapshots={},gang={},playerBikes={}}
    for _,p in ipairs(getElementsByType("player")) do mission.snapshots[p]=snap(p); setElementInterior(p,0); setElementDimension(p,SAK.dimension); setElementFrozen(p,true); setElementAlpha(p,p==player and 255 or 0); setElementPosition(p,2495.6,-1687.0,13.0) end
    playCutscene(SAK.cutscenes.intro,"intro_cutscene")
end)

addEvent("sak:cutsceneDone",true)
addEventHandler("sak:cutsceneDone",resourceRoot,function(id,ok)
    if source~=resourceRoot or not mission or id~=mission.cutsceneId or not mission.cutsceneWaiting[client] then return end
    mission.cutsceneWaiting[client]=nil; if not ok then return destroyMission(true) end
    for p in pairs(mission.cutsceneWaiting) do if isElement(p) then return end end
    if mission.phase=="intro_cutscene" then
        for _,p in ipairs(participants()) do setElementPosition(p,910.78,-1075.26,23.29); setElementFrozen(p,true) end
        playCutscene(SAK.cutscenes.funeral,"funeral_cutscene")
    elseif mission.phase=="funeral_cutscene" then beginRide(); startMonitor() end
end)

addEvent("sak:taskAck",true)
addEventHandler("sak:taskAck",resourceRoot,function(id,kind,ped,ok)
    if source~=resourceRoot or not mission or id~=mission.id or client~=mission.owner then return end
    outputDebugString(("[sweet-and-kendl] task %s ped=%s accepted=%s"):format(tostring(kind),tostring(ped),tostring(ok)),ok and 3 or 2)
end)

addCommandHandler("sweetandkendlabort",function() destroyMission(true) end)
addEventHandler("onPlayerQuit",root,function() if mission and mission.snapshots[source] then destroyMission(true) end end)
addEventHandler("onResourceStop",resourceRoot,function() destroyMission(true) end)
