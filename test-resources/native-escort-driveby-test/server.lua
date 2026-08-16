local session
local serial = 0

local function snapshot(p)
    local x,y,z = getElementPosition(p)
    return {x=x,y=y,z=z,dim=getElementDimension(p),int=getElementInterior(p),alpha=getElementAlpha(p),frozen=isElementFrozen(p)}
end

local function restore(p,s)
    if not isElement(p) then return end
    if isPedInVehicle(p) then removePedFromVehicle(p) end
    setElementInterior(p,s.int); setElementDimension(p,s.dim); setElementPosition(p,s.x,s.y,s.z)
    setElementAlpha(p,s.alpha); setElementFrozen(p,s.frozen)
end

local function cleanup(doRestore)
    if not session then return end
    if isTimer(session.monitor) then killTimer(session.monitor) end
    if isTimer(session.timeout) then killTimer(session.timeout) end
    for p,s in pairs(session.snapshots) do
        if isElement(p) then
            triggerClientEvent(p,"nativeEscortDriveBy:stop",resourceRoot,session.id)
            if doRestore then restore(p,s) end
        end
    end
    for _,e in ipairs({session.driver,session.passenger,session.voodoo,session.targetVehicle}) do if isElement(e) then destroyElement(e) end end
    session=nil
end

local function nonce() return tostring(getTickCount())..":"..tostring(math.random(100000,999999)) end
local function participant(p) return session and isElement(p) and session.snapshots[p] ~= nil end

local function publish(owner)
    session.owner=owner; session.epoch=session.epoch+1; session.nonce=nonce(); session.acceptDriver=false; session.acceptPassenger=false; session.seen={}
    setElementSyncer(session.voodoo,owner,true,true); setElementSyncer(session.driver,owner,true,true); setElementSyncer(session.passenger,owner,true,true)
    for p in pairs(session.snapshots) do
        if isElement(p) then triggerClientEvent(p,"nativeEscortDriveBy:assign",resourceRoot,session.id,session.epoch,session.nonce,
            session.voodoo,session.driver,session.passenger,session.targetVehicle,session.leader,p==owner) end
    end
end

addCommandHandler("nativeescortdriveby",function(player)
    cleanup(true); if isPedInVehicle(player) then return outputChatBox("Sors du vehicule avant le test.",player,255,120,80) end
    serial=serial+1; local c=VOODOO_TEST; local dim=64600+(serial%300)
    local target=createVehicle(c.targetModel,c.target[1],c.target[2],c.target[3],0,0,c.target[4])
    local voodoo=createVehicle(c.voodooModel,c.voodoo[1],c.voodoo[2],c.voodoo[3],0,0,c.voodoo[4])
    local driver=createPed(c.driverModel,c.voodoo[1],c.voodoo[2],c.voodoo[3]+1,c.voodoo[4])
    local passenger=createPed(c.passengerModel,c.voodoo[1],c.voodoo[2],c.voodoo[3]+1,c.voodoo[4])
    if not target or not voodoo or not driver or not passenger then return cleanup(false) end
    session={id=serial,epoch=0,leader=player,owner=player,targetVehicle=target,voodoo=voodoo,driver=driver,passenger=passenger,snapshots={},seen={},started=getTickCount()}
    for _,e in ipairs({target,voodoo,driver,passenger}) do setElementDimension(e,dim) end
    setElementHealth(target,4000); setElementHealth(voodoo,2000); warpPedIntoVehicle(driver,voodoo,0); warpPedIntoVehicle(passenger,voodoo,1); giveWeapon(passenger,c.weapon,9999,true)
    for _,p in ipairs(getElementsByType("player")) do
        session.snapshots[p]=snapshot(p); setElementInterior(p,0); setElementDimension(p,dim)
        if p==player then setElementPosition(p,c.target[1],c.target[2],c.target[3]+1); warpPedIntoVehicle(p,target,0); setElementAlpha(p,255); setElementFrozen(p,false)
        else setElementPosition(p,c.target[1]+5,c.target[2]+4,c.target[3]+1); setElementAlpha(p,0); setElementFrozen(p,true) end
    end
    publish(player)
    outputChatBox("[escort-driveby] Conduis le Greenwood. Sur client 2: /nativeescortdrivebyhandoff",root,100,235,140)
    session.monitor=setTimer(function()
        if not session then return end
        local vx,vy,vz=getElementPosition(session.voodoo); local tx,ty,tz=getElementPosition(session.targetVehicle); local d=getDistanceBetweenPoints3D(vx,vy,vz,tx,ty,tz)
        local owners=getElementSyncer(session.voodoo)==session.owner and getElementSyncer(session.driver)==session.owner and getElementSyncer(session.passenger)==session.owner
        local observers,seen=0,0; for p in pairs(session.snapshots) do if p~=session.owner and isElement(p) then observers=observers+1; if session.seen[p]==session.epoch then seen=seen+1 end end end
        if not session.passed and session.acceptDriver and session.acceptPassenger and owners and d<45 and getTickCount()-session.started>4000 and (observers==0 or seen>0) then
            session.passed=true; outputChatBox(("[escort-driveby] PASS epoch=%d dist=%.1f observer=%d/%d"):format(session.epoch,d,seen,observers),root,80,240,120)
        end
    end,500,0)
end)

addCommandHandler("nativeescortdrivebyhandoff",function(p)
    if not participant(p) or p==session.owner or session.pending then return end
    session.pending={from=session.owner,to=p,epoch=session.epoch,nonce=session.nonce}
    triggerClientEvent(session.owner,"nativeEscortDriveBy:revoke",resourceRoot,session.id,session.epoch,session.nonce)
    session.timeout=setTimer(function() if session then session.pending=nil end end,5000,1)
end)

addEvent("nativeEscortDriveBy:revoked",true)
addEventHandler("nativeEscortDriveBy:revoked",resourceRoot,function(id,epoch,n)
    local q=session and session.pending; if source~=resourceRoot or not q or client~=q.from or id~=session.id or epoch~=q.epoch or n~=q.nonce then return end
    if isTimer(session.timeout) then killTimer(session.timeout) end; local to=q.to; session.pending=nil; publish(to)
end)

addEvent("nativeEscortDriveBy:evidence",true)
addEventHandler("nativeEscortDriveBy:evidence",resourceRoot,function(id,epoch,n,kind,...)
    if source~=resourceRoot or not session or id~=session.id or epoch~=session.epoch or n~=session.nonce or not participant(client) then return outputDebugString("[escort-driveby] stale evidence rejected",2) end
    local a={...}; if kind=="observer" then if client~=session.owner then session.seen[client]=session.epoch end; return end
    if client~=session.owner then return outputDebugString("[escort-driveby] non-owner evidence rejected",2) end
    if kind=="acceptance" then session.acceptDriver=a[1]==true; session.acceptPassenger=a[2]==true
    elseif kind=="failure" then outputChatBox("[escort-driveby] ECHEC: "..tostring(a[1]),root,255,80,80) end
end)

addCommandHandler("nativeescortdrivebycleanup",function() cleanup(true) end)
addEventHandler("onResourceStop",resourceRoot,function() cleanup(true) end)
