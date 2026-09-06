-- Run from the repository root with Lua 5.1+.
-- Execute the production handlers with deterministic MTA boundaries. These
-- tests check routing/authority; GTA collision and animation require the VM.
local function read(path)
    local file = assert(io.open(path)); local text = file:read('*a'); file:close(); return text
end
local function section(text, first, last)
    local a = assert(text:find(first, 1, true)); local b = assert(text:find(last, a + #first, true))
    return text:sub(a, b - 1)
end
local function evaluate(code)
    return assert((loadstring or load)(code))()
end
local server = read('official-resources/native-ped-traffic/server.lua')
local clientCode = read('official-resources/native-ped-traffic/client.lua')
local handlers, sends, timers, hits = {}, {}, {}, {}
root, resourceRoot = {}, {}
config = {nativeMeleeDamageRadius=4, nativeMeleeDamageInterval=250, nativeFirearmDamageInterval=40}
function addEvent() end
function addEventHandler(name, _, fn) handlers[name] = fn end
function isElement(x) return type(x) == 'table' and x.kind ~= nil end
function getElementType(x) return x.kind end
function getElementHealth(x) return x.health or 100 end
function getElementDimension(x) return x.dimension or 0 end
function getElementInterior(x) return x.interior or 0 end
function getElementPosition(x) return x.x or 0, 0, 0 end
function getElementData(x, key) return x.data and x.data[key] end
function getElementSyncer(x) return x.owner end
function getPedWeapon(x) return x.weapon or 0 end
function getPlayerName(x) assert(x.kind == 'player'); return x.name or 'test' end
function squaredDistance(x,y,z,a,b,c) return (x-a)^2+(y-b)^2+(z-c)^2 end
function isIntegerInRange(x,a,b) return type(x)=='number' and x==math.floor(x) and x>=a and x<=b end
local now = 1000
function getTickCount() return now end
function log() end
function triggerClientEvent(...) sends[#sends+1] = {...}; return true end
function triggerServerEvent(...) sends[#sends+1] = {...}; return true end
function setTimer(fn) timers[#timers+1] = fn; return #timers end
function writePopulationTrace() end
function rememberTrafficCombatContext() end
local ownerA, ownerB = {kind='player'}, {kind='player'}
local attacker = {kind='ped',owner=ownerA}
local victim = {kind='ped',owner=ownerB}
local function record(ped, id)
    return {ped=ped,id=id,owner=ped.owner,epoch=7,state='active',populationClass='civilian'}
end
trafficPeds = {[attacker]=record(attacker,1),[victim]=record(victim,2)}
client = ownerA
local canonical = section(server, 'local stockUnarmedGangDamageFactors', '-- Candidate identity')
local handler = section(server, 'addEvent("pedTraffic:nativePlayerDamageObserved"', 'local nativeBikeJackTargetDoors')
evaluate(canonical .. '\n' .. handler)
local observed = handlers['pedTraffic:nativePlayerDamageObserved']
local function send(target, nonce, epoch, factor)
    observed(attacker,target,7,nonce,attacker.weapon or 0,3,factor or 6,0,epoch)
end
send(victim,1,7)
assert(#sends==1 and sends[1][1]==ownerB and sends[1][11]==victim and sends[1][12]==7,'NPC hit routes to victim authority with epoch')
send(victim,1,7); assert(#sends==1,'duplicate nonce')
now=1500; send(victim,2,6); assert(#sends==1,'stale victim epoch')
client=ownerB; send(victim,2,7); assert(#sends==1,'wrong attacker owner'); client=ownerA
victim.owner=ownerA; send(victim,2,7); assert(#sends==1,'same owner must simulate locally'); victim.owner=ownerB
send(ownerB,2,false); assert(#sends==2 and sends[2][1]==ownerB and sends[2][11]==false,'player route remains compatible')
-- One swing can contact distinct victims; rate limits must not couple them.
send(victim,3,7); assert(#sends==3,'independent victim cadence')
send(victim,4,7); assert(#sends==3,'same victim cadence')
now=2000; trafficPeds[attacker].populationClass='cop'; attacker.weapon=3
send(ownerB,5,false,18); assert(#sends==4,'stock nightstick accepted')
now=2500; send(ownerB,6,false,200); assert(#sends==4,'invented nightstick factor rejected')
attacker.weapon=22; send(ownerB,6,false,25); assert(#sends==5,'cop pistol accepted')
now=3000; attacker.weapon=0; send(ownerB,7,false,6); assert(#sends==6,'cop fists accepted')
trafficPeds[attacker].couple={epoch=6,state='active',owner=ownerA}
now=3500; send(ownerB,8,false,6); assert(#sends==6,'stale couple epoch'); trafficPeds[attacker].couple=nil

-- Receiver: only victim authority, fresh epochs, exactly one replay.
localPlayer=ownerB; enabled=true
function isElementSyncer(x) return x.owner==localPlayer end
assignments={[victim]={accepted=true,epoch=7}}; groupByPed={}; coupleByPed={}
nativeEventProfiles={[attacker]=12}; nativePlayerDamageReceipts={}
trafficNativeDamageWeapons={[0]=true,[3]=true,[22]=true}
attacker.data={['neon:ambientPedTraffic']=true,['neon:ambientPedTrafficEpoch']=7}
victim.data={['neon:ambientPedTraffic']=true,['neon:ambientPedTrafficEpoch']=7}
function addPedNativeDamageEvent(...) hits[#hits+1]={...}; return true end
evaluate(section(clientCode,'addEvent("pedTraffic:nativePlayerDamage"','addEventHandler("onClientPedDamage"'))
local replay=handlers['pedTraffic:nativePlayerDamage']
replay(attacker,7,20,0,3,6,0,victim,7); assert(#hits==1 and hits[1][1]==victim,'NPC replay')
replay(attacker,7,20,0,3,6,0,victim,7); assert(#hits==1,'receiver duplicate')
replay(attacker,7,21,0,3,6,0,victim,6); assert(#hits==1,'receiver stale victim epoch')
victim.owner=ownerA; replay(attacker,7,21,0,3,6,0,victim,7); assert(#hits==1,'receiver lost ownership'); victim.owner=ownerB
replay(attacker,7,21,0,3,6,0); assert(#hits==2 and hits[2][1]==ownerB,'legacy player payload')
-- Couples use their relation assignment on both ends.
assignments[victim]=nil; coupleByPed[victim]={accepted=true,epoch=7}
replay(attacker,7,22,0,3,6,0,victim,7); assert(#hits==3,'couple victim')
localPlayer=ownerA; nativeEventProfiles[attacker]=12; coupleByPed[attacker]={accepted=true,epoch=7}
function isPedNativeEventProfileActive() return true end
nextNativePlayerDamageNonce=0
sends={}; source=victim
evaluate(section(clientCode,'local function observeNativeTrafficDamage','addEventHandler("onClientPedNativeBikeJackAttempt"'))
handlers['onClientPedNativeDamageAttempt'](attacker,0,3,6,0)
assert(#sends==1 and sends[1][4]==victim and sends[1][11]==7,'couple attacker and victim epoch captured')

-- A queued response from A must not replay after A -> B -> A.
nativeDamageReplayReceipts={}; nativeEventProfiles[victim]=13
function consumeNativeDamage() return false end
function addPedNativeDamageResponseEvent() hits[#hits+1]='response'; return true end
evaluate(section(clientCode,'addEvent("pedTraffic:damageResponse"','addEvent("pedTraffic:nativePlayerDamage"'))
timers={}; local before=#hits
handlers['pedTraffic:damageResponse'](victim,attacker,0,3,false,7)
assert(#timers==1,'response delayed')
victim.data['neon:ambientPedTrafficEpoch']=9; timers[1]()
assert(#hits==before,'old response discarded after handoff')
print('PASS: combat routing, epochs, owner checks, cadence, cops, couples, duplicate replay and delayed handoff')
