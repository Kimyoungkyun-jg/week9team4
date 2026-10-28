local PacmanGameMode = {}

function PacmanGameMode:BeginPlay()
    self.ResultShown = false
end

function PacmanGameMode:Tick()
    if self.ResultShown or not self.Mode or self.Mode:IsResolvingGhostHit() then return end

    if self.Mode:GetPlayerLife() <= 0 then
        self.Mode:ShowGameResult(false)
        self.ResultShown = true
        return
    end

    
	local maxCoins = 50
    if maxCoins > 0 and self.Mode:GetCollectedCoinCount() >= maxCoins then
        self.Mode:ShowGameResult(true)
        self.ResultShown = true
    end
end

return PacmanGameMode
