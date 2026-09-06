-- Exercise the actual resource event handlers with a deterministic MTA facade.
-- This verifies server authority and recovery, not native client animation.
local count, check = 0, assert
local function assert(...) count=count+1; return check(...) end
local now, commands, handlers, timers, messages = 0, {}, {}, {}, {}
local function element(kind,x,y,z)
    return {kind=kind,x=x or 0,y=y or 0,z=z or 0,interior=0,dimension=0,alive=true,onGround=true}
end
root, resourceRoot = element('root'), element('resource')
function isElement(e) return type(e)=='table' and e.alive end
function getElementType(e) return e.kind end
function createObject(_,x,y,z) return element('object',x,y,z) end
function createPed(_,x,y,z) return element('ped',x,y,z) end
function getElementPosition(e) return e.x,e.y,e.z end
function setElementPosition(e,x,y,z) e.x,e.y,e.z=x,y,z end
function getElementDimension(e) return e.dimension end
function setElementDimension(e,v) e.dimension=v end
function getElementInterior(e) return e.interior end
function setElementInterior(e,v) e.interior=v end
function setElementFrozen(e,v) e.frozen=v end
function spawnPlayer(e,x,y,z,rotation,skin,interior,dimension)
    e.x,e.y,e.z,e.interior,e.dimension,e.dead=x,y,z,interior,dimension,false
end
function fadeCamera(e,visible) e.cameraVisible=visible end
function setCameraTarget(e,target) e.cameraTarget=target end
function isPedDead(e) return e.dead or false end
function isPedInVehicle(e) return e.vehicle or false end
function isPedOnGround(e) return e.onGround end
function isElementInWater(e) return e.water or false end
function getElementSyncer(e) return e.syncer end
function setElementSyncer(e,p) e.syncer=p end
function getDistanceBetweenPoints3D(x,y,z,a,b,c) return ((x-a)^2+(y-b)^2+(z-c)^2)^0.5 end
function getTickCount() return now end
function addEvent() end
function addEventHandler(name,_,fn) handlers[name]=fn end
function addCommandHandler(name,fn) commands[name]=fn end
function setTimer(fn) timers[#timers+1]=fn end
function outputChatBox() end
function outputDebugString() end
function triggerClientEvent(target,name,_,object,revision,phase,ped,executor)
    messages[#messages+1]={target=target,name=name,object=object,revision=revision,phase=phase,ped=ped,executor=executor}
end
function destroyElement(e)
    local previous=source; source=e; handlers.onElementDestroy(); source=previous; e.alive=false
end
local function report(player,object,revision,state)
    client,source=player,resourceRoot
    handlers['nativeCargo:report'](object,revision,state,99999,99999,99999)
end
local function last() return messages[#messages] end
local alice,bob=element('player',0,0,10),element('player',0,0,10)
dofile('test-resources/native-cargo/state.lua')
dofile('test-resources/native-cargo/server.lua')
commands.cargotest(alice)
local box=last().object
assert(last().phase=='available' and box.frozen)
alice.x=100
commands.cargocarry(alice)
assert(last().phase=='available','reject distant request')
alice.x=0
commands.cargocarry(alice)
assert(last().phase=='reserved' and last().executor==alice and last().ped==alice)
local token=last().revision
report(bob,box,token,'holding')
assert(last().phase=='reserved','observer cannot acknowledge')
report(alice,box,token-1,'holding')
assert(last().phase=='reserved','stale token cannot acknowledge')
report(alice,box,token,'holding')
assert(last().phase=='carrying')
alice.x=5; now=100; timers[1]()
report(alice,box,token,'released')
assert(last().phase=='available' and box.x==5 and box.y==0 and box.z==9.4,'drop ignores claimed coordinates')
commands.cargocarry(alice)
assert(last().phase=='reserved')
local token2=last().revision
report(alice,box,token,'released')
assert(last().phase=='reserved','old cancellation cannot release a new lease')
now=8100; timers[1]()
assert(last().phase=='available','startup timeout recovers')
commands.cargocarry(alice)
report(alice,box,last().revision,'holding')
alice.dimension=1; timers[1]()
assert(last().phase=='available','dimension change recovers')
alice.dimension=0
commands.cargonpc(alice)
box=last().object
commands.cargocarry(alice)
local npc=last().ped
assert(last().phase=='reserved' and npc~=alice and npc.syncer==alice)
report(alice,box,last().revision,'holding')
npc.syncer=bob; timers[1]()
assert(last().phase=='available','ownership transfer does not force holding back on')
npc.syncer=alice
commands.cargocarry(alice)
npc.dead=true; timers[1]()
assert(last().phase=='available','death releases without client cooperation')
client,source=bob,resourceRoot
handlers['nativeCargo:ready']()
assert(last().target==bob and last().phase=='available','late observer gets authoritative snapshot')
source=alice; handlers.onPlayerQuit()
assert(not isElement(box) and not isElement(npc),'test fixtures do not leak on disconnect')
local charlie=element('player'); charlie.dead=true
commands.cargospawn(charlie)
assert(not charlie.dead and charlie.x==2492 and charlie.y==-1685 and charlie.z==13.5,'explicit spawn enables isolated gameplay test')
assert(charlie.cameraVisible and charlie.cameraTarget==charlie,'spawn restores player camera')
print('native-cargo server handlers: '..count..' assertions passed')
