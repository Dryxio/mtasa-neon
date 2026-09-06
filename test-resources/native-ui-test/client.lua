-- Run /nui help. All presented text, menus, counters, windows and cards use
-- the engine API; this lab never replaces their rendering with DX or CEF.
local handles, timers, completions = {}, {}, {}
local activeClock, activeMenu, sequence = nil, nil, 0
local serial = 0
local function log(text) outputChatBox('[native-ui] '..text, 140, 220, 255) end
local function create(kind, options)
    local id, err = createNativeUI(kind, options)
    assert(id, kind..': '..tostring(err))
    handles[#handles + 1] = id
    return id
end
local function text(content)
    serial = serial + 1
    return create('text', {name='lab-'..serial, content=content})
end
local function reset()
    for _, timer in ipairs(timers) do if isTimer(timer) then killTimer(timer) end end
    timers, handles, completions = {}, {}, {}
    releaseNativeUI()
    activeClock, activeMenu, sequence = nil, nil, 0
    triggerServerEvent('native-ui-test:stopClock', resourceRoot)
end
local function later(fn, ms, times)
    local id = setTimer(fn, ms, times or 1)
    timers[#timers + 1] = id
    return id
end
local function line(content, x, y, more)
    local options = {text=text(content), x=x, y=y, width=250, scaleX=.48, scaleY=1.6, alignment=1}
    for k,v in pairs(more or {}) do options[k]=v end
    return create('drawText', options)
end
local function expectFalse(label, value, err)
    assert(value == false, label..' unexpectedly succeeded')
    log('PASS '..label..' ('..tostring(err)..')')
end
local scenes = {}
function scenes.texts()
    local objective = text('~y~Objectif inédit~s~ : rejoignez le café. ~k~~PED_SPRINT~ pour courir.')
    assert(showNativeText(objective, 'objective', 12000))
    later(function()
        assert(showNativeText(text('~z~Écoute, André : ça commence ici.'), 'dialogue', 8000))
        log('Dialogue : comparer avec le réglage sous-titres activé puis désactivé.')
    end, 12500)
    later(function() assert(showNativeText(text('Aide : ~k~~VEHICLE_ENTER_EXIT~ pour entrer.'), 'help', 10000)) end, 21000)
    later(function() assert(showNativeText(text('~g~Mission réussie~s~~n~Bonus : $~1~'), 'big', 6000, 1, 250)) end, 32000)
    log('Séquence 38 s : objectif, dialogue, aide, gros message. /nui clear annule.')
end
function scenes.taxi()
    activeClock = create('clock', {text=text('TIME'), value=60000, beepSeconds=12})
    create('counter', {text=text('FARES'), value=3, style=0, color=3, flash=true})
    create('counter', {text=text('BONUS'), value=25, maximum=100, style=1, color=1})
    create('counter', {text=text('CLIENTS'), value=3, maximum=10, style=2, color=3})
    create('counter', {text=text('TOTAL'), value=150, style=0, color=2})
    triggerServerEvent('native-ui-test:startClock', resourceRoot)
    log('1 chrono + 4 compteurs. /nui pause ; corrections serveur toutes les 5 s.')
end
function scenes.correction()
    local clock=create('clock',{value=100,visible=false})
    later(function() assert(updateNativeUI(clock,{value=100})) end,1000)
    later(function()
        local state=assert(getNativeUIState(clock))
        assert(state.finished and completions[clock]==1,'late correction emitted duplicate completion')
        log('PASS correction tardive : une seule notification de fin pour ce handle.')
    end,2000)
end
function scenes.menu()
    activeMenu = create('menu', {
        text=text('Garage de César'), x=80, y=120, columns=2, width=170,
        headers={text('Service'),text('Prix')},
        cells={text('Réparation'),text('$250'),text('Indisponible'),text('$500'),text('Peinture'),text('$100')},
        enabled={true,false,true}, widths={230,100}, alignments={1,2}, selected=1,
    })
    log('Navigation native ; sprint = valider, entrer/sortir = annuler. Tester les binds et touches maintenues.')
end
function scenes.grid()
    activeMenu = create('grid', {x=170,y=100,width=240,columns=8,selected=1})
    log('Grille native 8×8. Tester les cases après la 12e, puis valider/annuler.')
end
function scenes.blackjack()
    create('window', {text=text('Wager'), x=27,y=130,width=185,height=245,background=0x000000BE})
    line('Total Wager',36,150,{color=0x869BB8FF,scaleX=.4714,scaleY=2.5077})
    line('$250',36,175,{color=0xB4B4B4FF,scaleX=.6253,scaleY=2.7876})
    line("Dealer's Score",36,230,{color=0x869BB8FF,scaleY=2.5077})
    line('18',40,255,{scaleX=.6253,scaleY=2.7876})
    line('Your Score',36,305,{color=0x869BB8FF,scaleY=2.5077})
    line('21',40,330,{scaleX=.6253,scaleY=2.7876})
    create('card',{card=53,x=240,y=150,width=70,height=100})
    create('card',{card=9,x=318,y=150,width=70,height=100})
    create('card',{card=1,x=240,y=280,width=70,height=100})
    create('card',{card=13,x=318,y=280,width=70,height=100})
    line('~g~Blackjack !',240,395,{scaleX=.65,scaleY=2})
    -- Same registered string, different substitutions: catches shared scratch
    -- buffers causing every positioned draw to show the last number.
    local shared=text('~1~ / ~2~')
    create('drawText',{text=shared,x=450,y=200,width=140,number1=1,number2=10})
    create('drawText',{text=shared,x=450,y=230,width=140,number1=2,number2=20})
    log('Composition native Wager/cartes/scores. Comparer 4:3, 16:9, ultralarge et HUD Match Aspect Ratio.')
end
function scenes.fade()
    line('AVANT FONDU',180,160,{beforeFade=true})
    line('APRÈS FONDU',180,250,{beforeFade=false})
    log('Déclencher un fondu depuis votre scénario : seule la deuxième ligne doit rester au-dessus.')
end
function scenes.stress()
    local id=text('État initial')
    local count=0
    later(function()
        count=count+1
        assert(updateNativeUI(id,{content='Révision '..count..' : café, été, Noël.'}))
        assert(showNativeText(id,'objective',4000))
    end,100,100)
    log('100 remplacements. Faire restart native-ui-test pendant le message, puis /nui texts.')
end
function scenes.selftest()
    local id=text('Test privé')
    expectFalse('unknown option', createNativeUI('drawText',{text=id,typo=1}))
    expectFalse('wrong type', createNativeUI('clock',{value='100'}))
    expectFalse('NaN', createNativeUI('clock',{value=0/0}))
    expectFalse('unsupported token', createNativeUI('text',{name='bad',content='~q~No'}))
    expectFalse('unsupported Unicode', createNativeUI('text',{name='emoji',content='😀'}))
    expectFalse('duplicate name', createNativeUI('text',{name='lab-'..serial,content='Duplicate'}))
    local drawn=create('drawText',{text=id,visible=false})
    expectFalse('referenced text',destroyNativeUI(id))
    assert(destroyNativeUI(drawn));assert(destroyNativeUI(id))
    expectFalse('stale handle',getNativeUIState(id))
    local clock=create('clock',{value=10000,visible=false})
    expectFalse('second clock',createNativeUI('clock',{value=1,visible=false}))
    for i=1,4 do create('counter',{value=i,color=0,visible=false}) end
    expectFalse('fifth counter',createNativeUI('counter',{color=0,visible=false}))
    local title=text('Menu')
    create('menu',{cells={title},visible=false})
    create('grid',{columns=2,visible=false})
    expectFalse('third menu',createNativeUI('grid',{columns=2,visible=false}))
    local peer=getResourceFromName('native-ui-test-peer')
    if peer and getResourceState(peer)=='running' then
        assert(exports['native-ui-test-peer']:probe(clock))
        log('PASS second-resource ownership and saturation')
    else log('Démarrer native-ui-test-peer pour ajouter le test inter-ressources.') end
    local old=clock
    reset()
    expectFalse('released generation',getNativeUIState(old))
    local replacement=create('clock',{value=1,visible=false})
    assert(replacement~=old)
    reset();log('Selftest terminé. Ces assertions ne valident pas le rendu visuel.')
end
addEvent('native-ui-test:clock',true)
addEventHandler('native-ui-test:clock',resourceRoot,function(seq,remaining)
    if source~=resourceRoot or not activeClock or type(seq)~='number' or type(remaining)~='number' or seq<=sequence then return end
    sequence=seq
    local state=getNativeUIState(activeClock)
    if state and not state.paused then assert(updateNativeUI(activeClock,{value=math.max(0,remaining)})) end
end)
addEventHandler('onClientNativeUI',resourceRoot,function(handle,action,selection,color)
    if action=='finished' then completions[handle]=(completions[handle] or 0)+1 end
    log('Événement '..action..' ; handle '..handle..' ; sélection '..selection..' ; couleur '..color)
end)
addEventHandler('onClientRender',root,function()
    if not activeMenu then return end
    local state=getNativeUIState(activeMenu)
    if not state then activeMenu=nil return end
    if state.accepted>0 then log('Validation ligne/case '..state.accepted..' ; couleur GTA '..state.color);activeMenu=nil
    elseif state.cancelled then log('Annulation ; contrôles rendus.');activeMenu=nil end
end)
addCommandHandler('nui',function(_,scene)
    scene=scene or 'help'
    if scene=='help' then log('texts | taxi | correction | menu | grid | blackjack | fade | stress | selftest | pause | clear');return end
    if scene=='pause' then
        local state=activeClock and getNativeUIState(activeClock)
        if state then assert(updateNativeUI(activeClock,{paused=not state.paused}));log('Pause '..tostring(not state.paused)) end
        return
    end
    if scene~='clear' and not scenes[scene] then log('Scène inconnue. /nui help');return end
    reset()
    if scene=='clear' then log('Libéré.');return end
    local ok,err=pcall(scenes[scene])
    if not ok then reset();log('ÉCHEC '..tostring(err)) end
end)
addEventHandler('onClientResourceStop',resourceRoot,function() releaseNativeUI() end)
log('Prêt : /nui help. Rien ne s’affiche automatiquement.')
