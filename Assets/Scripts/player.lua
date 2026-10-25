local Player = {}

function Player:BeginPlay()
    Log("Player BeginPlay: " .. self.Owner:GetName())
    -- 액터에 부착된 MovementComponent 탐색 (있으면 사용)
    self.Movement = self.Owner:FindComponentByName("MovementComponent")
    self.Speed = 600.0
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
end

function Player:EndPlay()
    Log("Player EndPlay")
end

return Player
