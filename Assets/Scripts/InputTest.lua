local InputTest = {}

-- 감지할 키 목록 (원하는 키를 자유롭게 추가/제거 가능)
local WatchedKeys = {
    { Name = "W", Code = KeyCode.W },
    { Name = "A", Code = KeyCode.A },
    { Name = "S", Code = KeyCode.S },
    { Name = "D", Code = KeyCode.D },
    { Name = "Space", Code = KeyCode.Space },
    { Name = "Shift", Code = KeyCode.Shift },
}

-- 감지할 마우스 버튼 목록
local WatchedMouseButtons = {
    { Name = "Left", Button = MouseButton.Left },
    { Name = "Right", Button = MouseButton.Right },
}

function InputTest:BeginPlay()
    Log("[InputTest] Attached to " .. self.Owner:GetName() .. " - Input monitoring started")
end

function InputTest:Tick(deltaTime)
    -- 1. 키보드 입력 감지 (Pressed & Released)
    for _, key in ipairs(WatchedKeys) do
        if Input.IsKeyPressed(key.Code) then
            Log("[InputTest] Key Pressed: " .. key.Name)
        elseif Input.IsKeyReleased(key.Code) then
            Log("[InputTest] Key Released: " .. key.Name)
        end
    end

    -- 2. 마우스 버튼 감지 (Pressed & Released)
    for _, mouse in ipairs(WatchedMouseButtons) do
        if Input.IsMousePressed(mouse.Button) then
            Log("[InputTest] Mouse Pressed: " .. mouse.Name)
        elseif Input.IsMouseReleased(mouse.Button) then
            Log("[InputTest] Mouse Released: " .. mouse.Name)
        end
    end
end

function InputTest:EndPlay()
    Log("[InputTest] EndPlay: " .. self.Owner:GetName())
end

return InputTest
