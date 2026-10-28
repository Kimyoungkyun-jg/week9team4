local Player = {}

function Player:BeginPlay()
    self.SlomoDilation = 0.3
    self.SlomoRequested = false
    self.SlomoNeedsRelease = false
    self.bIsFadedOut = false
    self.bIsLetterboxActive = false
end

function Player:Tick(deltaTime)
    -- A zero owner delta means hitstop; native state preserves its time override.
    if deltaTime <= 0.0 then return end
    local pawn = APlayerPawn.Cast(self.Owner)
    if not pawn then return end
    local world = self.Owner:GetWorld()
    local controller = world:GetPlayerController()
    local mode = world:GetGameMode()
    if (mode and not mode:AllowsPlayerControllerInput()) or
       (controller and (controller:IsMoveInputIgnored() or controller:IsLookInputIgnored() or controller:IsInCinematicMode())) then
        pawn:SetSlomoActive(false)
        self.SlomoRequested = false
        return
    end

    local held = Input.IsKeyDown(KeyCode.T)
    if not held then
        pawn:SetSlomoActive(false)
        self.SlomoRequested = false
        self.SlomoNeedsRelease = false
    else
        -- Depletion ends the request until T is released, even as the gauge recovers.
        if self.SlomoRequested and not pawn:IsSlomoActive() then
            self.SlomoNeedsRelease = true
        end
        if not self.SlomoNeedsRelease and not pawn:IsSlomoActive() then
            pawn:StartSlomo(self.SlomoDilation)
        end
        self.SlomoRequested = pawn:IsSlomoActive()
    end

    -- 키보드 F 키: FadeOut / FadeIn 토글 테스트
    if Input.IsKeyPressed(KeyCode.F) then
        if not self.bIsFadedOut then
            self.bIsFadedOut = true
            FadeOut(1.5)
            Log("Camera FadeOut Triggered (1.5s)")
        else
            self.bIsFadedOut = false
            FadeIn(1.5)
            Log("Camera FadeIn Triggered (1.5s)")
        end
    end

    -- 키보드 L 키: Letterbox Enter / Exit 토글 테스트
    if Input.IsKeyPressed(KeyCode.L) then
        if not self.bIsLetterboxActive then
            self.bIsLetterboxActive = true
            EnterLetterbox(0.5, 0.12) -- 0.5초 동안 상하 12% 레터박스 진입
            Log("Camera EnterLetterbox Triggered (0.5s, 12%)")
        else
            self.bIsLetterboxActive = false
            ExitLetterbox(0.5) -- 0.5초 동안 레터박스 퇴장
            Log("Camera ExitLetterbox Triggered (0.5s)")
        end
    end
end

function Player:EndPlay()
    local pawn = APlayerPawn.Cast(self.Owner)
    if pawn then pawn:SetSlomoActive(false) end
    Log("Player EndPlay")
end

return Player
