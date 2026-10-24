local Player = {}

function Player:BeginPlay()
    Log("Player BeginPlay: " .. self.Owner:GetName())
end

return Player
