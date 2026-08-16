local cutsceneToken
local leases = {}

local function lease(element)
    if type(acquireElementStreamingLease) ~= "function" then return false end
    local token = acquireElementStreamingLease(element)
    if token then leases[#leases+1] = token end
    return token ~= false
end

local function clearLeases()
    for _,token in ipairs(leases) do if type(releaseElementStreamingLease)=="function" then releaseElementStreamingLease(token) end end
    leases={}
end

local function finishCutscene(id, ok)
    if cutsceneToken and type(releaseFileCutscene)=="function" then releaseFileCutscene(cutsceneToken) end
    cutsceneToken=nil
    triggerServerEvent("sak:cutsceneDone",resourceRoot,id,ok==true)
end

addEvent("sak:playCutscene",true)
addEventHandler("sak:playCutscene",resourceRoot,function(id,name)
    if type(requestFileCutscene)~="function" then return finishCutscene(id,false) end
    cutsceneToken=requestFileCutscene(name)
    if not cutsceneToken then return finishCutscene(id,false) end
    local started=false; local began=getTickCount()
    local timer
    timer=setTimer(function()
        if not cutsceneToken then if isTimer(timer) then killTimer(timer) end return end
        if not started then
            if isFileCutsceneLoaded(cutsceneToken) then
                started=startFileCutscene(cutsceneToken)==true
                if started and type(fadeFileCutscene)=="function" then fadeFileCutscene(cutsceneToken,true,1000,0,0,0) end
            elseif getTickCount()-began>60000 then
                killTimer(timer); finishCutscene(id,false)
            end
        elseif isFileCutsceneFinished(cutsceneToken) then
            killTimer(timer); finishCutscene(id,true)
        elseif type(isFileCutsceneSkipInputPressed)=="function" and isFileCutsceneSkipInputPressed(cutsceneToken) then
            skipFileCutscene(cutsceneToken)
        elseif getTickCount()-began>120000 then
            killTimer(timer); finishCutscene(id,false)
        end
    end,50,0)
end)

local function dispatchRoute(ped,bike,route)
    clearLeases(); if not lease(ped) or not lease(bike) then return false end
    if not isElementStreamedIn(ped) or not isElementStreamedIn(bike) or not isElementSyncer(ped) or not isElementSyncer(bike) then return false end
    setPedMissionActor(ped,true); setPedCanBeKnockedOffBike(ped,false)
    if type(setVehicleStraightLineDistance)=="function" then setVehicleStraightLineDistance(bike,30) end
    local seq={}; for _,p in ipairs(route) do seq[#seq+1]={task="drive_to",x=p[1],y=p[2],z=p[3],speed=p[4],mode="normal",vehicleModel=SAK.bikeModel,drivingStyle="avoid_cars"} end
    local ok=setPedTaskSequence(ped,seq,false)
    if ok and type(setVehicleStraightLineDistance)=="function" then setVehicleStraightLineDistance(bike,10) end
    return ok
end

addEvent("sak:route",true)
addEventHandler("sak:route",resourceRoot,function(id,ped,bike,route)
    local ok=dispatchRoute(ped,bike,route)
    triggerServerEvent("sak:taskAck",resourceRoot,id,"route",ped,ok)
end)

addEvent("sak:voodoo",true)
addEventHandler("sak:voodoo",resourceRoot,function(id,driver,passenger,voodoo,targetVehicle,targetPlayer)
    clearLeases(); for _,e in ipairs({driver,passenger,voodoo,targetVehicle,targetPlayer}) do if not lease(e) then return triggerServerEvent("sak:taskAck",resourceRoot,id,"voodoo",driver,false) end end
    if getElementSyncer(driver)~=localPlayer or getElementSyncer(passenger)~=localPlayer or getElementSyncer(voodoo)~=localPlayer then
        return triggerServerEvent("sak:taskAck",resourceRoot,id,"voodoo",driver,false)
    end
    setPedMissionActor(driver,true); setPedMissionActor(passenger,true)
    local drive=setPedDriveMission(driver,voodoo,targetVehicle,"escort_left",30.0,"avoid_cars")
    local shoot=setPedDriveBy(passenger,targetPlayer,5000.0,"ai_all_directions",true,40)
    triggerServerEvent("sak:taskAck",resourceRoot,id,"voodoo",driver,drive and shoot)
end)

addEvent("sak:ballasProofs",true)
addEventHandler("sak:ballasProofs",resourceRoot,function(driver,passenger,enabled)
    if type(setPedPhysicalProofs)~="function" then return end
    for _,ped in ipairs({driver,passenger}) do if isElement(ped) and isElementStreamedIn(ped) and isElementSyncer(ped) then setPedPhysicalProofs(ped,enabled,enabled,enabled,enabled,enabled) end end
end)

addEvent("sak:message",true)
addEventHandler("sak:message",resourceRoot,function(text) outputChatBox("[Sweet & Kendl] "..tostring(text),210,210,210) end)

addEventHandler("onClientResourceStop",resourceRoot,function()
    clearLeases(); if cutsceneToken and type(releaseFileCutscene)=="function" then releaseFileCutscene(cutsceneToken) end
end)
