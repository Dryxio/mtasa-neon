-- Exercise the production registry with adversarial lifecycle transitions.
local now, checks, elements, handlers = 0, 0, {}, {}
local function check(value, reason) checks = checks + 1; assert(value, reason) end
root = {}; local own, other = {}, {}; local player = {kind="player",x=0}
local function car(x) local v={kind="vehicle",x=x or 200,occupants={}};elements[#elements+1]=v;return v end
function isElement(e) return type(e)=="table" and e.kind and not e.dead end
function getElementType(e) return e.kind end
function getElementDimension(e) return e.dimension or 0 end
function getElementInterior(e) return e.interior or 0 end
function getElementPosition(e) return e.x or 0,0,0 end
function getDistanceBetweenPoints3D(x,y,z,a,b,c) return math.abs(x-a) end
function getVehicleOccupants(v) return v.occupants end
function getVehicleOccupant(v,seat) return v.occupants[seat] end
function getVehicleTowingVehicle(v) return v.tower end
function getVehicleTowedByVehicle(v) return v.trailer end
function getElementAttachedTo(v) return v.parent end
function getAttachedElements(v) return v.attached or {} end
function getTickCount() return now end
function getElementsByType(kind) return kind=="player" and {player} or {} end
function getThisResource() return own end
function getResourceState(r) return r.stopped and "stopped" or "running" end
function addEventHandler(name,_,fn) handlers[name]=fn end
function setTimer(fn,interval) check(interval==1000,"bounded cleanup frequency");return {} end
function destroyElement(v)
    check(not next(v.occupants),"never destroy an occupied car")
    if v.refuseDestroy then return false end
    v.dead=true;source=v;handlers.onElementDestroy();return true
end
dofile(arg[1] or "test-resources/native-vehicle-traffic/takeover_lifecycle.lua")
local api=TrafficTakeoverLifecycle
local function tick(ms) now=(now+(ms or 1000))%4294967296;api.update() end
local v=car();check(api.retain(v,player) and api.count()==1,"pending takeover consumes shared budget")
tick(120000);check(isElement(v),"pending car belongs to entry watchdog")
check(not api.confirm(v),"entry must be corroborated")
sourceResource=other;check(not adoptTrafficVehicle(v),"cannot claim an unconfirmed transfer")
v.occupants[0]=player;check(api.confirm(v),"ordinary success confirmed");tick(120000)
check(isElement(v) and api.remaining(1)==0,"occupied confirmed car remains capped")
v.occupants={};tick();player.x=200;tick(60000);check(isElement(v),"observer prevents cleanup")
player.x=0;v.tower=car();tick();v.tower=nil;tick();tick(59999);check(isElement(v),"detach starts a fresh grace")
v.parent=car();tick();v.parent=nil;v.attached={car()};tick();v.attached={};tick()
v.refuseDestroy=true;tick(60000);check(api.count()==1,"failed destruction retains capacity")
v.refuseDestroy=false;tick();check(not isElement(v) and api.count()==0,"abandoned empty car retires")
local late=car();api.retain(late,player);api.expire(late);late.occupants[1]=player;tick(120000)
check(isElement(late),"watchdog expiry preserves unexpected passenger")
late.occupants={};tick();tick(60000);check(not isElement(late),"expired empty car cleaned")
local owned=car();api.retain(owned,player);owned.occupants[0]=player;api.confirm(owned)
client=player;check(not adoptTrafficVehicle(owned),"remote client cannot adopt");client=nil
sourceResource=own;check(not adoptTrafficVehicle(owned),"not an external ownership handoff")
sourceResource=other;other.stopped=true;check(not adoptTrafficVehicle(owned),"dead resource cannot adopt");other.stopped=nil
check(adoptTrafficVehicle(owned) and api.count()==0,"trusted external handoff releases registry")
owned.occupants={};tick(300000);check(isElement(owned),"adopted car excluded from cleanup")
check(not api.expire(owned) and not adoptTrafficVehicle(owned),"old transaction cannot recapture transferred car")
-- Repeated legitimate takeovers exchange one ambient slot for one retained car.
local fleet={};for i=1,64 do local c=car();api.retain(c,player);c.occupants[0]=player;api.confirm(c);fleet[#fleet+1]=c end
check(api.remaining(64)==0 and api.count()==64,"64 retained cars stop refill")
for _,c in ipairs(fleet)do c.occupants={} end
tick();tick(60000);check(api.count()==0 and api.remaining(64)==64,"cleanup restores refill budget")
now=4294967200;local wrapped=car();api.retain(wrapped,player);api.expire(wrapped);tick(1);tick(60001)
check(not isElement(wrapped),"tick wrap cannot retain abandoned cars forever")
print("takeover_lifecycle: "..checks.." checks PASS")
