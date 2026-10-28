local Fog = {}

Fog.Density = 0.0

Fog.HeightFalloff = 0.02

-- Spooky Purple: { 0.4, 0.1, 0.6, 1.0 }
-- Mystic Blue:   { 0.2, 0.5, 0.9, 1.0 }
-- Sunset Red:    { 0.9, 0.4, 0.2, 1.0 }
-- Clean White:   { 0.8, 0.8, 0.8, 1.0 }
Fog.Color = { 0.1, 0.1, 0.1, 1.0 }

Fog.StartDistance = 0.0

Fog.MaxOpacity = 1.0

Fog.CutoffDistance = 0.0

Fog.bAnimate = false


function Fog:ApplySettings()
    if not self.FogComp then
        if self.Owner.GetFogComponent then
            self.FogComp = self.Owner:GetFogComponent()
        elseif self.Owner.GetExponentialHeightFogComponent then
            self.FogComp = self.Owner:GetExponentialHeightFogComponent()
        elseif self.Owner.FindComponentByName then
            self.FogComp = self.Owner:FindComponentByName("UExponentialHeightFogComponent")
        end
    end

    if not self.FogComp then
        LogWarning("[Fog.lua] ExponentialHeightFogComponent not found on " .. self.Owner:GetName())
        return
    end

    self.FogComp:SetFogDensity(Fog.Density)
    self.FogComp:SetFogHeightFalloff(Fog.HeightFalloff)
    self.FogComp:SetFogColor(Fog.Color[1], Fog.Color[2], Fog.Color[3], Fog.Color[4] or 1.0)
    self.FogComp:SetStartDistance(Fog.StartDistance)
    self.FogComp:SetFogMaxOpacity(Fog.MaxOpacity)
    self.FogComp:SetFogCutoffDistance(Fog.CutoffDistance)
end

function Fog:BeginPlay()
    Log("[Fog.lua] BeginPlay on " .. self.Owner:GetName())
    self.Time = 0.0
    self:ApplySettings()
end

function Fog:OnReload()
    self:ApplySettings()
end

function Fog:Tick(deltaTime)
    if not self.FogComp then return end

    if Fog.bAnimate then
        self.Time = (self.Time or 0.0) + deltaTime
        local wave = (math.sin(self.Time * 2.0) + 1.0) * 0.5 -- 0.0 ~ 1.0
        local dynamicDensity = Fog.Density * (0.5 + wave * 0.5)
        self.FogComp:SetFogDensity(dynamicDensity)
    end
end

function Fog:EndPlay()
    Log("[Fog.lua] EndPlay: " .. self.Owner:GetName())
end

return Fog
