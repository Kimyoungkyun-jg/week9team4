#include "EnginePCH.h"
#include "LuaBinding.h"
#include "Math/Vector.h"
#include "Math/Vector4.h"
#include "Math/Rotator.h"
#include "Math/Transform.h"

namespace LuaBindings
{
	void BindMath(sol::state& Lua)
	{
		// 1. FVector 바인딩
		Lua.new_usertype<FVector>("FVector",
			sol::constructors<FVector(), FVector(float, float, float), FVector(float)>(),
			sol::call_constructor, sol::constructors<FVector(), FVector(float, float, float), FVector(float)>(),
			"X", &FVector::X,
			"Y", &FVector::Y,
			"Z", &FVector::Z,

			// 연산자 오버로딩
			sol::meta_function::addition, sol::overload(
				[](const FVector& a, const FVector& b) { return a + b; }
			),
			sol::meta_function::subtraction, sol::overload(
				[](const FVector& a, const FVector& b) { return a - b; },
				[](const FVector& a) { return -a; }
			),
			sol::meta_function::multiplication, sol::overload(
				[](const FVector& a, const FVector& b) { return a * b; },
				[](const FVector& a, float b) { return a * b; },
				[](float a, const FVector& b) { return b * a; }
			),
			sol::meta_function::division, sol::overload(
				[](const FVector& a, const FVector& b) { return a / b; },
				[](const FVector& a, float b) { return a / b; }
			),
			sol::meta_function::equal_to, [](const FVector& a, const FVector& b) { return a == b; },

			// 메서드
			"Length", &FVector::Length,
			"Size", &FVector::Size,
			"SizeSquared", &FVector::SizeSquared,
			"Normalized", &FVector::Normalized,
			"Normalize", &FVector::Normalized, // alias
			"GetSafeNormal", [](const FVector& Self) { return Self.GetSafeNormal(); },
			"Dot", [](const FVector& a, const FVector& b) { return a.Dot(b); },
			"Cross", [](const FVector& a, const FVector& b) { return a.Cross(b); },
			"Dist", [](const FVector& a, const FVector& b) { return FVector::Distance(a, b); }
		);

		// FVector 정적 방향 벡터 상수들
		sol::table VectorTable = Lua["FVector"];
		VectorTable["Zero"] = FVector::ZeroVector;
		VectorTable["One"] = FVector::OneVector;
		VectorTable["Forward"] = ForwardVector;
		VectorTable["Right"] = RightVector;
		VectorTable["Up"] = UpVector;

		// 2. FRotator 바인딩
		Lua.new_usertype<FRotator>("FRotator",
			sol::constructors<FRotator(), FRotator(float, float, float)>(),
			sol::call_constructor, sol::constructors<FRotator(), FRotator(float, float, float)>(),
			"Pitch", &FRotator::Pitch,
			"Yaw", &FRotator::Yaw,
			"Roll", &FRotator::Roll,
			sol::meta_function::addition, sol::overload(
				[](const FRotator& a, const FRotator& b) { return FRotator(a.Pitch + b.Pitch, a.Yaw + b.Yaw, a.Roll + b.Roll); }
			),
			sol::meta_function::multiplication, sol::overload(
				[](const FRotator& a, float b) { return FRotator(a.Pitch * b, a.Yaw * b, a.Roll * b); }
			)
		);

		// 3. FTransform 바인딩
		Lua.new_usertype<FTransform>("FTransform",
			sol::constructors<FTransform(), FTransform(const FRotator&, const FVector&, const FVector&)>(),
			sol::call_constructor, sol::constructors<FTransform(), FTransform(const FRotator&, const FVector&, const FVector&)>(),
			"Location", &FTransform::Location,
			"Rotation", &FTransform::Rotation,
			"Scale", &FTransform::Scale
		);

		// 4. FVector4 바인딩
		Lua.new_usertype<FVector4>("FVector4",
			sol::constructors<FVector4(), FVector4(float, float, float, float), FVector4(float), FVector4(const FVector&, float)>(),
			sol::call_constructor, sol::constructors<FVector4(), FVector4(float, float, float, float), FVector4(float), FVector4(const FVector&, float)>(),
			"X", &FVector4::X,
			"Y", &FVector4::Y,
			"Z", &FVector4::Z,
			"W", &FVector4::W,
			"R", &FVector4::X,
			"G", &FVector4::Y,
			"B", &FVector4::Z,
			"A", &FVector4::W
		);
	}
}
