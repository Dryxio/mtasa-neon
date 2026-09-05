-- Run from the repository root with Lua 5.1+. Execute production functions
-- against mocked MTA boundaries, including the unchanged evidence validator.
local function read(path)
    local f=assert(io.open(path)); local s=f:read('*a'); f:close(); return s
end
local function section(text, first, last)
    local a=assert(text:find(first,1,true)); local b=assert(text:find(last,a+#first, true)); return text:sub(a,b-1)
end
local function run(s) return assert((loadstring or load)(s))() end
local base='test-resources/native-vehicle-traffic/'
local server, clientCode=read(base..'server.lua'),read(base..'client.lua')
dofile(base..'transport.lua')
local transport=VehicleTrafficTransport
local alive, sent={},{}
local function current(u) return alive[u] end
local function send(batch) sent[#sent+1]=batch end
-- Sixteen periodic reports every 500 ms become two envelopes per second.
for cycle=1,2 do
    for id=1,16 do
        local u={id=id,epoch=1}; alive[u]=true
        transport.enqueue(u,'owner-sample',{seq=cycle})
    end
    transport.flush(current,send)
end
assert(#sent==2 and #sent[1]==16 and #sent[2]==16,'32 periodic events -> 2 batches')
transport.flush(current,send); assert(#sent==2,'empty queue is silent')
local u={id=99,epoch=3}; alive[u]=true
transport.enqueue(u,'owner-sample',{seq=1}); transport.enqueue(u,'owner-sample',{seq=2})
transport.flush(current,send); assert(#sent[3]==1 and sent[3][1].data.seq==2,'coalesce latest sample')
transport.enqueue(u,'owner-sample',{seq=3}); u.epoch=4
transport.flush(current,send); assert(#sent==3,'old epoch discarded')
transport.enqueue(u,'observer-sample',{}); alive[u]=nil
transport.flush(current,send); assert(#sent==3,'released unit discarded')
for id=1,65 do local v={id=id,epoch=1}; alive[v]=true; transport.enqueue(v,'observer-sample',{}) end
sent={}; transport.flush(current,send)
assert(#sent==3 and #sent[1]==32 and #sent[2]==32 and #sent[3]==1,'bounded envelopes')
local network={}
resourceRoot={}; root={}
function triggerServerEvent(...) network[#network+1]={...} end
local report=run(section(clientCode,'local function report(', 'local function sendDiagnostic')..'\nreturn report')
report(u,'accepted',{}); report(u,'failure',{})
assert(#network==2 and network[1][1]=='carTraffic:evidence','critical events bypass queue')
report(u,'owner-sample',{}); assert(#network==2,'samples queued')
local diagnostic=run(section(clientCode,'local function sendDiagnostic(', 'addEvent("carTraffic:diagnostics"')..'\nreturn sendDiagnostic')
diagnostic(1,1,'owner-readiness',{}); assert(#network==2,'diagnostics default off')
transport.diagnosticsEnabled=true; diagnostic(1,1,'interaction-exit-timeout',{})
assert(#network==3,'test diagnostics preserve timeout path')

local handlers={}
function addEvent() end
function addEventHandler(name,_,fn) handlers[name]=fn end
function isElement(v) return type(v)=='table' end
function getTickCount() return 1000 end
function finiteNumber(v) return type(v)=='number' and v==v and v or nil end
local failures=0
function fail() failures=failures+1 end
VehicleTrafficTelemetry={record=function() end,updateMotion=function() return {},false end}
telemetryWindow={}
client={}; source=resourceRoot; activeTest=false
local unit={id=1,epoch=7,owner=client,state='active',ownerLastSeq=0,ownerTaskSamples=0,passengers={}}
units={[1]=unit}
run(section(server,'local function receiveEvidence(', 'addEvent("carTraffic:clientDiagnostic"'))
local function sample(epoch,seq)
    return {id=1,epoch=epoch,evidence='owner-sample',data={seq=seq,task=true,pedSyncer=true,vehicleSyncer=true,seat=0,pedVehicleDelta=0,passengers={},x=0,y=0,rz=0,vx=0,vy=0}}
end
local batch=handlers['carTraffic:evidenceBatch']
batch({sample(7,1)}); assert(unit.ownerLastSeq==1,'production validator receives batch')
batch({sample(6,2)}); assert(unit.ownerLastSeq==1,'stale epoch ignored')
local owner=client; client={}; batch({sample(7,2)}); assert(unit.ownerLastSeq==1,'wrong owner ignored'); client=owner
source={}; batch({sample(7,2)}); assert(unit.ownerLastSeq==1,'wrong event source ignored'); source=resourceRoot
local excessive={}; for i=1,33 do excessive[i]=sample(7,i+1) end
batch(excessive); assert(unit.ownerLastSeq==1,'oversized envelope rejected')
batch({sample(7,2),{evidence='accepted'}}); assert(unit.ownerLastSeq==1,'control events prohibited in batches')
batch({sample(7,1)}); assert(failures==1,'existing duplicate sequence check retained')
handlers['carTraffic:evidence'](1,7,'owner-sample',sample(7,2).data)
assert(unit.ownerLastSeq==2,'legacy endpoint retained')

local serializations,logs=0,0
function toJSON() serializations=serializations+1; return '{}' end
function outputServerLog() logs=logs+1 end
function outputDebugString() logs=logs+1 end
debugEnabled=false
local trace=run(section(server,'local function trace(', 'local function finiteNumber')..'\nreturn trace')
trace('candidate',{}); trace('population-snapshot',{}); assert(logs==0 and serializations==0,'quiet mode skips log encoding')
trace('unit-failure',{}); trace('PASS-cleanup',{}); trace('status',{}); assert(logs==3,'errors and explicit results remain')
debugEnabled=true; trace('candidate',{}); assert(logs==4,'debug enables routine logs')
local pedServer=read('test-resources/native-ped-traffic/server.lua')
local pedLog=run(section(pedServer,'local function log(', 'local function utcTimestamp')..'\nreturn log')
debugEnabled=false; pedLog('group-spawn',true); assert(logs==4,'forced routine ped logs suppressed')
pedLog('cop-test FAIL',true); pedLog('couple-test CANCEL',true); assert(logs==6,'ped test results remain')
print('PASS: batching, queue lifecycle, immediate control events, diagnostics, epoch/owner/sequence checks and quiet logs')
