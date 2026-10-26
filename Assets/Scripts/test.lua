local MyScript = {}

function MyScript:BeginPlay()
    local root = self.Owner:GetRootComponent()
    if root then
        local loc = root:GetRelativeLocation()
        -- 시작 기준 위치 저장
        self.BaseX = loc.X
        self.BaseY = loc.Y
        self.BaseZ = loc.Z
        self.Time = 0.0
    end
end

function MyScript:Tick(dt)
    local root = self.Owner:GetRootComponent()
    if not root then return end

    -- 누적 시간 (느린 속도 계수 1.2)
    self.Time = (self.Time or 0.0) + dt * 1.2

    -- 1. X축: -10 ~ +10 범위를 천천히 왕복
    local offsetX = math.sin(self.Time) * 10.0

    -- 2. Y-Z축: 용수철 모양(나선형)으로 회전 (반경 3.0, 회전 속도 4.0배)
    local springRadius = 10.0
    local springSpeed = self.Time * 5.0
    local offsetY = math.cos(springSpeed) * springRadius
    local offsetZ = math.sin(springSpeed) * springRadius

    -- 최종 위치 적용
    local newLoc = FVector.new(
        (self.BaseX or 0.0) + offsetX,
        (self.BaseY or 0.0) + offsetY,
        (self.BaseZ or 0.0) + offsetZ
    )
    root:SetRelativeLocation(newLoc)
end

return MyScript