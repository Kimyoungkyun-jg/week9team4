#pragma once

#include <optional>

class AGameHUD;

namespace ScoreboardUI
{
	void Populate(AGameHUD& HUD, std::optional<double> CurrentSeconds);
	void Layout(AGameHUD& HUD);
}
