#pragma once

#include "Core/Types.h"
#include "Math/EngineMath.h"
#include "Math/Rotator.h"

enum class ECameraShakePattern : uint8
{
	SineWave,
	PerlinNoise
};

enum class ECameraShakeInitialOffset : uint8
{
	Zero,
	Random
};

// Duration은 블렌드 인/아웃을 포함한 전체 시간(초)이다.
struct FCameraShakeSettings
{
	ECameraShakePattern Pattern = ECameraShakePattern::SineWave;
	// 사인파의 초기 위상. 펄린은 Seed로 정한 축별 시작 좌표를 사용한다.
	ECameraShakeInitialOffset InitialOffset = ECameraShakeInitialOffset::Zero;
	// 같은 Seed와 설정은 같은 결과를 낸다. 다르게 재생하려면 Seed를 바꾼다.
	uint32 Seed = 0;

	float Duration = 0.5f;
	float BlendInTime = 0.05f;
	float BlendOutTime = 0.2f;

	// 카메라 로컬 축: X 전방, Y 오른쪽, Z 위. 진폭은 엔진 거리 단위이다.
	FVector LocationAmplitude = FVector(0.02f, 0.02f, 0.02f);
	// 사인파는 Hz, 펄린은 초당 노이즈 격자 진행량이다. 0이면 초기 샘플을 유지한다.
	FVector LocationFrequency = FVector(11.0f, 13.0f, 9.0f);
	FRotator RotationAmplitude = FRotator(0.8f, 0.8f, 0.4f); // 도
	FVector RotationFrequency = FVector(13.0f, 11.0f, 9.0f); // Pitch, Yaw, Roll 순서
	float FOVAmplitude = 0.0f; // 도
	float FOVFrequency = 10.0f;

	bool IsValid() const;
};

// 객체 포인터 대신 사용한다. IsValid는 발급 여부이며, 재생 여부는 Manager에서 조회한다.
struct FCameraShakeHandle
{
	bool IsValid() const { return Id != 0; }
	bool operator==(const FCameraShakeHandle&) const = default;

private:
	friend class UCameraModifier_CameraShake;
	uint64 Id = 0;
};

struct FCameraShakeOffset
{
	FVector Location = FVector::ZeroVector;
	FRotator Rotation;
	float FOV = 0.0f;
};

// 실행 상태는 설정과 분리해 같은 설정으로 여러 Shake를 동시에 재생할 수 있다.
class FCameraShakeInstance
{
public:
	FCameraShakeInstance(const FCameraShakeSettings& InSettings, float InScale);
	void Update(float DeltaTime, FCameraShakeOffset& OutOffset);
	void Stop(bool bImmediately);
	bool IsFinished() const;

private:
	float GetWeight() const;
	float SamplePattern(float Frequency, int32 Channel) const;
	FCameraShakeSettings Settings;
	float Scale;
	// 위치 XYZ, 회전 Pitch/Yaw/Roll, FOV의 7개 독립 채널.
	double SampleOffsets[7] = {};
	double Elapsed = 0.0;
	double StopElapsed = 0.0;
	float StopStartWeight = 0.0f;
	bool bStopping = false;
	bool bFinished = false;
};
