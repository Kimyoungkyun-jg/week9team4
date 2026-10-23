local hitCount = 0
local endSequence = nil

function OnProjectileHit(hit)
    if endSequence then return end

    hitCount = hitCount + 1
    Log("Impact " .. hitCount .. "/7")

    if hitCount == 7 then
        endSequence = coroutine.create(function()
            Log("Game Over")
            local elapsed = 0
            while elapsed < 2 do
                elapsed = elapsed + coroutine.yield()
            end
            EndGame()
        end)

        local ok, err = coroutine.resume(endSequence)
        if not ok then Log(err) end
    end
end

function Tick(deltaTime)
    if not endSequence or coroutine.status(endSequence) ~= "suspended" then return end

    local ok, err = coroutine.resume(endSequence, deltaTime)
    if not ok then Log(err) end
end