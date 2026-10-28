local Player = {}

function Player:BeginPlay()
    Log("Player BeginPlay: " .. self.Owner:GetName())
    -- 액터에 부착된 MovementComponent 탐색 (있으면 사용)
    self.Movement = self.Owner:FindComponentByName("MovementComponent")
    self.Speed = 600.0
    self.bIsSlomo = false;
    self.SlomoDilation = 0.3
    self.bIsFadedOut = false
    self.bIsLetterboxActive = false
end

function Player:Tick(deltaTime)
    --[[ C++ PlayerController와의 중복 이동 방지를 위해 임시 주석 처리
    local moveDir = FVector(0, 0, 0)

    -- 키보드 입력 체크 (W, A, S, D)
    if Input.IsKeyDown(KeyCode.W) then moveDir.X = moveDir.X + 1 end
    if Input.IsKeyDown(KeyCode.S) then moveDir.X = moveDir.X - 1 end
    if Input.IsKeyDown(KeyCode.A) then moveDir.Y = moveDir.Y - 1 end
    if Input.IsKeyDown(KeyCode.D) then moveDir.Y = moveDir.Y + 1 end

    -- 이동 처리
    if moveDir:Length() > 0 then
        local delta = moveDir:Normalize() * self.Speed * deltaTime

        if self.Movement then
            -- MovementComponent를 통한 충돌 이동
            self.Movement:MoveUpdatedComponent(delta, self.Owner:GetActorRotation(), true)
        else
            -- 컴포넌트가 없을 경우 액터 위치 직접 이동
            local curLoc = self.Owner:GetActorLocation()
            self.Owner:SetActorLocation(curLoc + delta)
        end
    end
    --]]
    local bKeyDown = Input.IsKeyDown(KeyCode.T)
    if bKeyDown and not self.bIsSlomo then
        self.bIsSlomo = true
        local world = self.Owner:GetWorld()
        local worldDilation = self.SlomoDilation
        local playerDilation = (worldDilation > 0.0) and (1.0 / worldDilation) or 1.0
        world:SetWorldTimeDilation(worldDilation)
        self.Owner:SetCustomTimeDilation(playerDilation)
        Log("Slomo On")
    elseif not bKeyDown and self.bIsSlomo then
        self.bIsSlomo = false
        local world = self.Owner:GetWorld()
        world:SetWorldTimeDilation(1.0)
        self.Owner:SetCustomTimeDilation(1.0)
        Log("Slomo Off")
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
    Log("Player EndPlay")
end

return Player
