local GhostAI = {}

function GhostAI:BeginPlay()
    Log("[GhostAI] BeginPlay: " .. self.Owner:GetName())

    -- C++ AI 컴포넌트 참조 획득 (Owner 메소드 또는 FindComponentByName)
    if self.Owner.GetAIComponent then
        self.AI = self.Owner:GetAIComponent()
    end
    if not self.AI and self.Owner.FindComponentByName then
        self.AI = self.Owner:FindComponentByName("GhostAIComponent")
    end

    if not self.AI then
        LogError("[GhostAI] Failed to find GhostAIComponent on " .. self.Owner:GetName())
        return
    end

    -- FSM 상태: "Patrol", "Alert", "Chase", "Return"
    self.State = "Patrol"

    -- 에디터 패널에서 설정한 파라미터 값 가져오기
    self.WaitTimeBeforeChase = self.AI:GetWaitTimeBeforeChase()
    self.MaxChaseDuration = self.AI:GetMaxChaseDuration()

    self.AlertTimer = 0.0
    self.ChaseTimer = 0.0
    self.RepathTimer = 0.0
    self.RepathInterval = 0.3       -- BFS 재계산 간격 (초)

    -- 초기 순찰 시작
    self.AI:MoveToNextPatrol()
end

function GhostAI:ResetToHome()
    Log("[GhostAI] Resetting to home state: " .. self.Owner:GetName())
    if self.Owner.RestoreEye then
        self.Owner:RestoreEye()
    end
    self.State = "Patrol"
    self.AlertTimer = 0.0
    self.ChaseTimer = 0.0
    self.RepathTimer = 0.0
    if self.AI then
        self.AI:ResetToHome()
        self.AI:MoveToNextPatrol()
    end
end

function GhostAI:Tick(dt)
    if not self.AI then return end

    -- 연출(CloseUp, Fade) 진행 중일 때는 AI 업데이트 일시 중지
    if self.Owner.GetWorld then
        local world = self.Owner:GetWorld()
        if world and world.GetGameMode then
            local gm = world:GetGameMode()
            if gm and gm.IsResolvingGhostHit and gm:IsResolvingGhostHit() then
                return
            end
        end
    end

    -- 1. [순찰 모드 (Patrol)]
    if self.State == "Patrol" then
        if self.Owner:IsPlayerInDetectionRange() then
            -- 플레이어 감지 시 대기(Alert) 상태로 전환하고 멈춤
            Log("[GhostAI] Player detected! Waiting before chase...")
            self.State = "Alert"
            self.AlertTimer = 0.0
            self.AI:StopMovement() -- 대기시간 동안 정지
        elseif self.AI:HasReachedDestination() then
            -- 다음 순찰 지점으로 이동
            self.AI:MoveToNextPatrol()
        end

    -- 2. [대기 모드 (Alert)]: 대기시간 동안 멈춰있다가 눈 색 바꾸고 추격 시작
    elseif self.State == "Alert" then
        self.AlertTimer = self.AlertTimer + dt
        if self.AlertTimer >= self.WaitTimeBeforeChase then
            Log("[GhostAI] Alert wait finished! Changing eye to red and starting chase.")
            -- Ghost 눈 색 변경
            self.Owner:ChangeEye()

            -- Chase 상태로 전환 및 즉시 플레이어 추격 시작
            self.State = "Chase"
            self.ChaseTimer = 0.0
            self.RepathTimer = 0.0
            self.AI:ChasePlayer()
        end

    -- 3. [추격 모드 (Chase)]
    elseif self.State == "Chase" then
        self.ChaseTimer = self.ChaseTimer + dt
        self.RepathTimer = self.RepathTimer + dt

        -- 0.3초 간격으로 플레이어 위치를 향해 BFS 경로 재탐색
        if self.RepathTimer >= self.RepathInterval then
            self.RepathTimer = 0.0
            self.AI:ChasePlayer()
        end

        -- 2초 경과 시 재검사 (확장된 추격 감지 범위로 검사)
        if self.ChaseTimer >= self.MaxChaseDuration then
            local bStillInSight = false
            if self.Owner.IsPlayerInChaseRange then
                bStillInSight = self.Owner:IsPlayerInChaseRange()
            else
                bStillInSight = self.Owner:IsPlayerInDetectionRange()
            end

            if bStillInSight then
                -- 플레이어가 확장된 감지 범위(추격 범위) 안에 있음: 2초 연장 후 계속 추격
                self.ChaseTimer = 0.0
            else
                -- 플레이어가 추격 범위를 벗어남: 눈 원래대로 복구하고 복귀 시작
                Log("[GhostAI] Player lost! Restoring eye and returning to home.")
                self.Owner:RestoreEye()
                self.State = "Return"
                if self.AI.SetChasing then self.AI:SetChasing(false) end
                self.AI:ReturnToHome()
            end
        end

    -- 4. [복귀 모드 (Return)]
    elseif self.State == "Return" then
        if self.Owner:IsPlayerInDetectionRange() then
            -- 복귀 중 다시 플레이어가 감지되면 Alert로 전환
            Log("[GhostAI] Player detected again during return! Waiting before chase...")
            self.State = "Alert"
            self.AlertTimer = 0.0
            self.AI:StopMovement()
        elseif self.AI:HasReachedDestination() then
            -- 복귀 완료: 순찰 재개
            Log("[GhostAI] Resuming patrol.")
            self.State = "Patrol"
            self.AI:MoveToNextPatrol()
        end
    end
end

function GhostAI:EndPlay()
    Log("[GhostAI] EndPlay: " .. self.Owner:GetName())
end

return GhostAI
