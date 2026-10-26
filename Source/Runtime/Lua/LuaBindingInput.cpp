#include "EnginePCH.h"
#include "LuaBinding.h"
#include "Input/InputSystem.h"
#include "Input/KeyCode.h"
#include "Input/MouseButton.h"

namespace LuaBindings
{
	void BindInput(sol::state& Lua)
	{
		// 1. KeyCode Enum 바인딩
		Lua.new_enum<EKeyCode>("KeyCode", {
			{"W", EKeyCode::W},
			{"A", EKeyCode::A},
			{"S", EKeyCode::S},
			{"D", EKeyCode::D},
			{"Q", EKeyCode::Q},
			{"E", EKeyCode::E},
			{"R", EKeyCode::R},
			{"F", EKeyCode::F},
			{"Space", EKeyCode::Space},
			{"Shift", EKeyCode::Shift},
			{"LShift", EKeyCode::LShift},
			{"RShift", EKeyCode::RShift},
			{"Control", EKeyCode::Control},
			{"LControl", EKeyCode::LControl},
			{"RControl", EKeyCode::RControl},
			{"Alt", EKeyCode::Alt},
			{"Escape", EKeyCode::Escape},
			{"Enter", EKeyCode::Enter},
			{"Tab", EKeyCode::Tab},
			{"Backspace", EKeyCode::Backspace},
			{"Left", EKeyCode::Left},
			{"Right", EKeyCode::Right},
			{"Up", EKeyCode::Up},
			{"Down", EKeyCode::Down},
			{"Num0", EKeyCode::Num0},
			{"Num1", EKeyCode::Num1},
			{"Num2", EKeyCode::Num2},
			{"Num3", EKeyCode::Num3},
			{"Num4", EKeyCode::Num4},
			{"Num5", EKeyCode::Num5},
			{"Num6", EKeyCode::Num6},
			{"Num7", EKeyCode::Num7},
			{"Num8", EKeyCode::Num8},
			{"Num9", EKeyCode::Num9}
		});

		// 2. MouseButton Enum 바인딩
		Lua.new_enum<EMouseButton>("MouseButton", {
			{"Left", EMouseButton::Left},
			{"Right", EMouseButton::Right},
			{"Middle", EMouseButton::Middle},
			{"Side1", EMouseButton::Side1},
			{"Side2", EMouseButton::Side2}
		});

		// 3. Input 전역 테이블 바인딩
		sol::table InputTable = Lua.create_named_table("Input");
		InputTable["IsKeyDown"] = &FInputSystem::IsKeyDown;
		InputTable["IsKeyPressed"] = &FInputSystem::IsKeyPressed;
		InputTable["IsKeyReleased"] = &FInputSystem::IsKeyReleased;
		InputTable["IsMouseDown"] = &FInputSystem::IsMouseDown;
		InputTable["IsMousePressed"] = &FInputSystem::IsMousePressed;
		InputTable["IsMouseReleased"] = &FInputSystem::IsMouseReleased;
		InputTable["GetMouseX"] = &FInputSystem::GetMouseX;
		InputTable["GetMouseY"] = &FInputSystem::GetMouseY;
		InputTable["GetMouseDeltaX"] = &FInputSystem::GetMouseDeltaX;
		InputTable["GetMouseDeltaY"] = &FInputSystem::GetMouseDeltaY;
		InputTable["GetWheelDelta"] = &FInputSystem::GetWheelDelta;
	}
}
