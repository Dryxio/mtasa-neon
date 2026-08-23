-- The stock play resource does not publish a useful ASE mode or map name.
-- Keep these live fields aligned with the persistent Neon registry profile so
-- the server browser does not fall back to "MTA:SA" and "None".
addEventHandler("onResourceStart", resourceRoot, function()
    setGameType("Freeroam")
    setMapName("San Andreas")
end)
