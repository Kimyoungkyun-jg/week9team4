local GhostAI = {}

-- 코루틴 안에서 지정한 시간(초) 동안 대기하는 헬퍼 함수
local function WaitSeconds(seconds)
    local remain = seconds
    while remain > 0 do
        local dt = coroutine.yield() -- Tick(dt)에서 전달해준 dt 수신
        remain = remain - (dt or 0)
    end
end

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

    self.AlertCo = nil
    self.ChaseTimer = 0.0
    self.RepathTimer = 0.0
    self.RepathInterval = 0.3       -- BFS 재계산 간격 (초)

    -- 초기 순찰 시작
    self.AI:MoveToNextPatrol()
end

-- Alert 상태 진입 및 코루틴 시작
function GhostAI:EnterAlert()
    Log("[GhostAI] Player detected! Waiting before chase...")
    self.State = "Alert"
    self.AI:StopMovement() -- 대기시간 동안 정지

    self.AlertCo = coroutine.create(function()
        -- 1. 대기 시간 동안 프레임마다 yield하며 대기
        WaitSeconds(self.WaitTimeBeforeChase)

        -- 2. 대기 완료 후 눈 색 변경 및 추격 시작
        Log("[GhostAI] Alert wait finished! Changing eye to red and starting chase.")
        self.Owner:ChangeEye()

        self.State = "Chase"
        self.ChaseTimer = 0.0
        self.RepathTimer = 0.0
        self.AI:ChasePlayer()

        self.AlertCo = nil
    end)

    -- 코루틴 최초 1회 실행 (WaitSeconds 진입 및 첫 yield 지점까지 실행)
    coroutine.resume(self.AlertCo)
end

function GhostAI:ResetToHome()
    Log("[GhostAI] Resetting to home state: " .. self.Owner:GetName())
    if self.Owner.RestoreEye then
        self.Owner:RestoreEye()
    end
    self.State = "Patrol"
    self.AlertCo = nil
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
            -- 플레이어 감지 시 대기(Alert) 코루틴 시작
            self:EnterAlert()
        elseif self.AI:HasReachedDestination() then
            -- 다음 순찰 지점으로 이동
            self.AI:MoveToNextPatrol()
        end

    -- 2. [대기 모드 (Alert)]: 코루틴에 dt를 전달하여 대기 시간 진행
    elseif self.State == "Alert" then
        if self.AlertCo and coroutine.status(self.AlertCo) ~= "dead" then
            local success, err = coroutine.resume(self.AlertCo, dt)
            if not success then
                LogError("[GhostAI] Alert coroutine error: " .. tostring(err))
                self.AlertCo = nil
            end
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
            self:EnterAlert()
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
