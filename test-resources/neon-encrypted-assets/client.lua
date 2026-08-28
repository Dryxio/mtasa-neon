-- WOSA's escooter-rental model replaces this stock object, which gives the
-- runtime checkpoint a known-good DFF/TXD/COL triplet instead of arbitrary data.
local modelId = 1874
local assets = {
    "models/test.txd.neonasset",
    "models/test.dff.neonasset",
    "models/test.col.neonasset",
}

for _, path in ipairs(assets) do
    local element = engineReplaceEncryptedModel(path, modelId)
    assert(element, ("failed to load authenticated asset %s"):format(path))
end

outputDebugString("[neon-encrypted-assets] authenticated TXD/DFF/COL replacements loaded", 3)
triggerServerEvent("neonEncryptedAssetsLoaded", resourceRoot)
