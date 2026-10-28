local PacmanSkyLight = {}

PacmanSkyLight.FadeDuration = 1.0

function PacmanSkyLight:BeginPlay()
    local sky = ASkyLightActor.Cast(self.Owner)
    self.Light = sky and sky:GetSkyLightComponent()
    if not self.Light then return end

    -- Restore the scene's authored brightness, including values other than 1.
    self.OriginalIntensity = self.Light:GetIntensity()
    self:BindGameMode()
end

function PacmanSkyLight:BindGameMode()
    if self.Mode or not self.Light then return end
    local world = self.Owner:GetWorld()
    local mode = world and APacmanGameMode.Cast(world:GetGameMode())
    if not mode then return end

    self.Mode = mode
    self.GameplayHandle = mode.OnGameplayStarted:Add(function() self:OnGameplayStarted() end)
    self.ClearHandle = mode.OnClearCinematicStarted:Add(function() self:OnClearCinematicStarted() end)

    -- Also support a component activated after the corresponding event.
    if mode:HasClearCinematicStarted() then
        self:OnClearCinematicStarted()
    elseif mode:HasGameplayStarted() then
        self:OnGameplayStarted()
    end
end

function PacmanSkyLight:OnGameplayStarted()
    if self.GameplayStarted or self.ClearStarted then return end
    self.GameplayStarted = true
    self.FadeStartIntensity = self.Light:GetIntensity()
    self.FadeElapsed = 0.0
end

function PacmanSkyLight:OnClearCinematicStarted()
    self.ClearStarted = true
    self.FadeElapsed = nil
    self.Light:SetIntensity(self.OriginalIntensity)
end

function PacmanSkyLight:Tick(dt)
    if not self.Light then return end
    self:BindGameMode()
    if self.FadeElapsed == nil then return end

    self.FadeElapsed = self.FadeElapsed + math.max(dt, 0.0)
    local alpha = self.FadeDuration > 0.0 and math.min(self.FadeElapsed / self.FadeDuration, 1.0) or 1.0
    self.Light:SetIntensity(self.FadeStartIntensity * (1.0 - alpha))
    if alpha >= 1.0 then self.FadeElapsed = nil end
end

function PacmanSkyLight:OnReload()
    -- Keep the original intensity and fade progress across script reloads.
    self:BindGameMode()
end

function PacmanSkyLight:EndPlay()
    local world = self.Owner:GetWorld()
    local mode = world and APacmanGameMode.Cast(world:GetGameMode())
    if mode and mode == self.Mode then
        if self.GameplayHandle then mode.OnGameplayStarted:Remove(self.GameplayHandle) end
        if self.ClearHandle then mode.OnClearCinematicStarted:Remove(self.ClearHandle) end
    end
    self.Mode = nil
    if self.Light then self.Light:SetIntensity(self.OriginalIntensity) end
    self.Light = nil
end

return PacmanSkyLight
