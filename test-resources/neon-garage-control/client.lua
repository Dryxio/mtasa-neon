-- Opt-in checks: no teleport, vehicle creation, or automatic garage mutation.
-- Run only on an isolated test server with no other garage resource active.
addCommandHandler('garagelease_test',function()
    local id=5
    assert(type(acquireGarageControl)=='function','Updated Neon client required')
    assert(not acquireGarageControl(4294967295),'Out-of-range ID must fail without wrapping')
    assert(not releaseGarageControl(id),'An unowned garage must not release')
    assert(acquireGarageControl(id),'Garage 5 must be available for this isolated test')
    assert(acquireGarageControl(id),'Repeated acquisition by the same resource must succeed')
    assert(releaseGarageControl(id),'Owner must be able to release')
    assert(not releaseGarageControl(id),'Repeated release must fail')
    outputChatBox('Garage control lease checks passed; garage 5 was left opening.')
end)

addCommandHandler('garagelease_hold',function()
    assert(acquireGarageControl(5),'Garage 5 unavailable')
    outputChatBox('Holding garage 5. A second resource must fail to acquire or release it; stop this resource to release.')
end)
