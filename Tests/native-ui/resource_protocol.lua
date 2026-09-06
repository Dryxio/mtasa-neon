-- Runs the actual test resource's server script without GTA/MTA. It verifies
-- authority, stale-source rejection, disconnect cleanup and tick-wrap handling.
local handlers, pulses, sent = {}, {}, {}
local now = 100
resourceRoot, root = {}, {}
function addEvent() end
function addEventHandler(name, _, callback) handlers[name] = callback end
function getTickCount() return now end
function setTimer(callback) pulses[#pulses + 1] = callback end
function triggerClientEvent(player, name, eventSource, seq, remaining)
    sent[#sent + 1] = {player=player, name=name, source=eventSource, seq=seq, remaining=remaining}
end
assert(loadfile('test-resources/native-ui-test/server.lua'))()
local player = {}
client, source = player, {}
handlers['native-ui-test:startClock']()
pulses[1]();assert(#sent==0, 'foreign event source must be rejected')
client, source = nil, resourceRoot
handlers['native-ui-test:startClock']()
pulses[1]();assert(#sent==0, 'missing remote client must be rejected')
client, source = player, resourceRoot
handlers['native-ui-test:startClock']()
now = 5100;pulses[1]()
assert(#sent==1 and sent[1].seq==1 and sent[1].remaining==55000 and sent[1].player==player)
now = 11100;pulses[1]();assert(sent[2].seq==2 and sent[2].remaining==49000)
source = player;handlers.onPlayerQuit();pulses[1]();assert(#sent==2)
now = 4294967000;client,source=player,resourceRoot;handlers['native-ui-test:startClock']()
now = 4704;pulses[1]();assert(sent[3].remaining==55000, 'server timer must survive tick wrap')
now = 70000;pulses[1]();assert(sent[4].remaining==0);pulses[1]();assert(#sent==4, 'terminal correction must only be sent once')
print('native-ui resource protocol: authority, sequence, disconnect, tick-wrap PASS')
