#pragma once
#include <algorithm>
#include <cmath>

// At most one step per frame. Idle time never contributes to the next step.
class FFootstepCadence
{
public:
	float IntervalSeconds = 0.35f;
	void Reset() { Elapsed = 0.0f; }
	bool Advance(float DeltaTime, bool bMoved)
	{
		if (!bMoved || !std::isfinite(DeltaTime) || DeltaTime <= 0.0f) { Reset(); return false; }
		const float Interval = std::isfinite(IntervalSeconds) ? std::max(0.01f, IntervalSeconds) : 0.35f;
		Elapsed += DeltaTime;
		if (Elapsed + 0.00001f < Interval) return false;
		Elapsed = std::fmod(std::max(Elapsed, Interval), Interval);
		return true;
	}
private:
	float Elapsed = 0.0f;
};
