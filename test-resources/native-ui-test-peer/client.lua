function probe(foreignHandle)
    assert(getNativeUIState(foreignHandle)==false)
    assert(updateNativeUI(foreignHandle,{value=9000})==false)
    assert(destroyNativeUI(foreignHandle)==false)
    assert(createNativeUI('clock',{value=1000,visible=false})==false)
    assert(createNativeUI('counter',{color=0,visible=false})==false)
    assert(createNativeUI('grid',{columns=2,visible=false})==false)
    local id=assert(createNativeUI('text',{name='peer',content='Autre ressource'}))
    assert(destroyNativeUI(id))
    return true
end
-- A persistent competing message can be tested independently of the selftest.
addCommandHandler('nui-peer',function(_,action)
    releaseNativeUI()
    if action=='clear' then return end
    local id,err=createNativeUI('text',{name='peer-message',content='Message appartenant à la seconde ressource.'})
    if id then id,err=showNativeText(id,'objective',60000) end
    outputChatBox('[native-ui-peer] '..tostring(id)..' '..tostring(err))
end)
