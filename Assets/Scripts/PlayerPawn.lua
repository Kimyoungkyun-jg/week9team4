local PlayerPawn = {}
local CameraControls = require("Assets.Scripts.player")

PlayerPawn.FlashlightDrainPerSecond = 10.0
-- One swing costs half of one second's continuous drain.
PlayerPawn.FlashlightAttackCost = PlayerPawn.FlashlightDrainPerSecond * 0.5
-- Durations in seconds: prepare, swing, return to rest.
PlayerPawn.FlashlightWindupDuration = 0.1
PlayerPawn.FlashlightSwingDuration = 0.4
PlayerPawn.FlashlightRecoveryDuration = 0.25
-- Maximum length and capsule radius in metres (visual and overlap volume).
PlayerPawn.FlashlightAttackReach = 20.0
PlayerPawn.FlashlightCapsuleRadius = 0.18
-- Angular spacing of the two visual-only fan trails (degrees).
PlayerPawn.FlashlightTrailAngleStep = 2.0
-- Hitstop, ghost recoil duration (minimum 0.1 seconds), attack brightness multiplier.
PlayerPawn.FlashlightHitStopDuration = 0.12
PlayerPawn.FlashlightGhostStaggerDuration = 0.8
PlayerPawn.FlashlightFlashMultiplier = 3.0
-- Camera-local metres: X forward, Y right, Z up.
PlayerPawn.FlashlightSwingPivot = FVector(0.5, -0.0, -0.2)
-- Fan coordinate orientation in degrees: Pitch, Yaw, Roll. (0,0,0) is horizontal.
PlayerPawn.FlashlightSwingCoordinateRotation = FRotator(-15.0, 0.0, 150.0)
PlayerPawn.FlashlightSwingHalfAngle = 80.0
PlayerPawn.FlashlightSwingHandRadius = 0.6

function PlayerPawn:ApplyFlashlightSettings()
    local player = APlayerPawn.Cast(self.Owner)
    player:ConfigureFlashlight(
        PlayerPawn.FlashlightDrainPerSecond,
        PlayerPawn.FlashlightAttackCost,
        PlayerPawn.FlashlightSwingDuration,
        PlayerPawn.FlashlightAttackReach)
    player:ConfigureFlashlightSwing(
        PlayerPawn.FlashlightSwingPivot,
        PlayerPawn.FlashlightSwingCoordinateRotation,
        PlayerPawn.FlashlightSwingHalfAngle,
        PlayerPawn.FlashlightSwingHandRadius)
    player:ConfigureFlashlightAttack(
        PlayerPawn.FlashlightWindupDuration,
        PlayerPawn.FlashlightRecoveryDuration,
        PlayerPawn.FlashlightCapsuleRadius,
        PlayerPawn.FlashlightHitStopDuration,
        PlayerPawn.FlashlightGhostStaggerDuration,
        PlayerPawn.FlashlightFlashMultiplier,
        PlayerPawn.FlashlightTrailAngleStep)
end

function PlayerPawn:BeginPlay()
    CameraControls.BeginPlay(self)
    self:ApplyFlashlightSettings()
end

function PlayerPawn:Tick(deltaTime)
    CameraControls.Tick(self, deltaTime)
end

function PlayerPawn:EndPlay()
    CameraControls.EndPlay(self)
end

function PlayerPawn:OnReload()
    self:ApplyFlashlightSettings()
end

return PlayerPawn
