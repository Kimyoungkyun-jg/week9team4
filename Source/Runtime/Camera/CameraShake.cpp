#include "EnginePCH.h"
#include "Camera/CameraShake.h"
#include <algorithm>
#include <cmath>

namespace
{
	constexpr double TwoPi = 6.28318530717958647692;

	uint32 MixBits(uint32 Value)
	{
		Value ^= Value >> 16;
		Value *= 0x7feb352dU;
		Value ^= Value >> 15;
		Value *= 0x846ca68bU;
		return Value ^ (Value >> 16);
	}

	uint32 ChannelSeed(uint32 Seed, int32 Channel)
	{
		return MixBits(Seed ^ (static_cast<uint32>(Channel + 1) * 0x9e3779b9U));
	}

	float SamplePerlin1D(double Position, uint32 Seed)
	{
		// 큰 시간/주파수도 정수 변환 범위를 넘지 않도록 격자 좌표를 순환시킨다.
		constexpr double LatticePeriod = 4294967296.0;
		const double Wrapped = std::fmod(Position, LatticePeriod);
		const double Lattice = std::floor(Wrapped);
		const uint32 Left = static_cast<uint32>(Lattice);
		const double T = Wrapped - Lattice;

		// 격자점의 랜덤 기울기와 거리의 내적을 보간하는 1차원 gradient noise.
		const double LeftGradient = (MixBits(Left ^ Seed) & 1U) ? 1.0 : -1.0;
		const double RightGradient = (MixBits((Left + 1U) ^ Seed) & 1U) ? 1.0 : -1.0;
		const double FromLeft = LeftGradient * T;
		const double FromRight = RightGradient * (T - 1.0);
		// 6t^5 - 15t^4 + 10t^3: 격자 경계에서 1차/2차 도함수가 연속이다.
		const double Fade = T * T * T * (T * (T * 6.0 - 15.0) + 10.0);
		return static_cast<float>(2.0 * (FromLeft + (FromRight - FromLeft) * Fade));
	}
}

bool FCameraShakeSettings::IsValid() const
{
	if ((Pattern != ECameraShakePattern::SineWave && Pattern != ECameraShakePattern::PerlinNoise) ||
		(InitialOffset != ECameraShakeInitialOffset::Zero && InitialOffset != ECameraShakeInitialOffset::Random))
		return false;
	if (!std::isfinite(Duration) || Duration <= 0.0f ||
		!std::isfinite(BlendInTime) || BlendInTime < 0.0f ||
		!std::isfinite(BlendOutTime) || BlendOutTime < 0.0f ||
		!std::isfinite(FOVAmplitude) || !std::isfinite(FOVFrequency) || FOVFrequency < 0.0f)
		return false;

	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (!std::isfinite(LocationAmplitude[Axis]) || !std::isfinite(RotationAmplitude[Axis]) ||
			!std::isfinite(LocationFrequency[Axis]) || LocationFrequency[Axis] < 0.0f ||
			!std::isfinite(RotationFrequency[Axis]) || RotationFrequency[Axis] < 0.0f)
			return false;
	}
	return true;
}

FCameraShakeInstance::FCameraShakeInstance(const FCameraShakeSettings& InSettings, float InScale)
	: Settings(InSettings), Scale(InScale)
{
	for (int32 Channel = 0; Channel < 7; ++Channel)
	{
		const double UnitRandom = static_cast<double>(MixBits(ChannelSeed(Settings.Seed, Channel)) >> 8) / 16777216.0;
		if (Settings.Pattern == ECameraShakePattern::PerlinNoise)
			SampleOffsets[Channel] = UnitRandom * 256.0;
		else if (Settings.InitialOffset == ECameraShakeInitialOffset::Random)
			SampleOffsets[Channel] = UnitRandom * TwoPi;
	}
}

float FCameraShakeInstance::SamplePattern(float Frequency, int32 Channel) const
{
	const double Time = static_cast<double>(Frequency) * Elapsed;
	if (Settings.Pattern == ECameraShakePattern::PerlinNoise)
		return SamplePerlin1D(Time + SampleOffsets[Channel], ChannelSeed(Settings.Seed, Channel));
	return static_cast<float>(std::sin(TwoPi * Time + SampleOffsets[Channel]));
}

float FCameraShakeInstance::GetWeight() const
{
	if (IsFinished()) return 0.0f;
	const double In = Settings.BlendInTime > 0.0f ? Elapsed / Settings.BlendInTime : 1.0;
	const double Out = Settings.BlendOutTime > 0.0f ? (Settings.Duration - Elapsed) / Settings.BlendOutTime : 1.0;
	float Weight = static_cast<float>(std::clamp(std::min(In, Out), 0.0, 1.0));
	if (bStopping)
	{
		// 블렌드 인 도중 중지해도 중지 시점보다 강해지지 않게 한다.
		const float StopWeight = StopStartWeight * static_cast<float>(
			std::clamp(1.0 - StopElapsed / Settings.BlendOutTime, 0.0, 1.0));
		Weight = std::min(Weight, StopWeight);
	}
	return Weight;
}

bool FCameraShakeInstance::IsFinished() const
{
	return bFinished || Elapsed >= Settings.Duration ||
		(bStopping && StopElapsed >= Settings.BlendOutTime);
}

void FCameraShakeInstance::Stop(bool bImmediately)
{
	if (bImmediately || Settings.BlendOutTime <= 0.0f)
	{
		bFinished = true;
		return;
	}
	if (bStopping || IsFinished()) return;
	StopStartWeight = GetWeight();
	StopElapsed = 0.0;
	bStopping = true;
}

void FCameraShakeInstance::Update(float DeltaTime, FCameraShakeOffset& OutOffset)
{
	OutOffset = FCameraShakeOffset{};
	const double Step = std::isfinite(DeltaTime) && DeltaTime > 0.0f ? DeltaTime : 0.0;
	Elapsed = std::min(Elapsed + Step, static_cast<double>(Settings.Duration));
	if (bStopping) StopElapsed += Step;
	const float Weight = GetWeight() * Scale;
	if (Weight == 0.0f) return;

	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		OutOffset.Location[Axis] = SamplePattern(Settings.LocationFrequency[Axis], Axis) * Settings.LocationAmplitude[Axis] * Weight;
		OutOffset.Rotation[Axis] = SamplePattern(Settings.RotationFrequency[Axis], Axis + 3) * Settings.RotationAmplitude[Axis] * Weight;
	}
	OutOffset.FOV = SamplePattern(Settings.FOVFrequency, 6) * Settings.FOVAmplitude * Weight;
}
