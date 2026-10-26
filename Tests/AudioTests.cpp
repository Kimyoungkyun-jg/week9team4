#include "EnginePCH.h"
#include "Audio/SoundManager.h"
#include "Audio/FootstepCadence.h"
#include <chrono>
#include <thread>
#include <stdexcept>

static int Failures = 0;
static int AudioWarnings = 0;
void FLog::Emit(ELogVerbosity, const FString& Message) { ++AudioWarnings; std::cout << Message << '\n'; }
static void Check(bool Condition, const char* Name)
{
	std::cout << (Condition ? "PASS " : "FAIL ") << Name << '\n';
	if (!Condition) ++Failures;
}

int main()
{
	for (int FPS : {30, 60, 144})
	{
		FFootstepCadence Cadence;
		int Steps = 0;
		for (int Frame = 0; Frame < FPS * 7; ++Frame) Steps += Cadence.Advance(1.0f / FPS, true);
		Check(Steps == 20, ("7 seconds cadence at " + std::to_string(FPS) + " FPS").c_str());
		Check(!Cadence.Advance(10.0f, false), "no movement sample produces no step");
		Check(!Cadence.Advance(0.01f, true), "restart does not inherit idle time");
		Check(Cadence.Advance(3.5f, true), "delayed frame emits one step");
		Check(!Cadence.Advance(0.01f, true), "delayed frame leaves no backlog");
		Cadence.Reset();
		Check(!Cadence.Advance(0.2f, true), "reset between play sessions");
	}

	FSoundManager Sound;
	if (!Sound.Initialize())
	{
		Check(!Sound.PlaySFX("Assets/Sounds/coin.wav"), "device failure is nonfatal");
		Sound.SetPaused(true); Sound.Update(); Sound.SetPaused(false); Sound.Shutdown();
		std::cout << "SKIP playback tests: no working audio device\n";
		return Failures ? 1 : 0;
	}
	Sound.SetSFXVolume(0.0f); // Tests exercise real voices, without requiring listening.
	for (const char* File : {"coin", "footstep00", "footstep01", "footstep02", "footstep03", "footstep04", "Damaged", "GameClear", "GameOver"})
		Check(Sound.Preload(std::string("Assets/Sounds/") + File + ".wav"), File);
	const int Before = AudioWarnings;
	for (int I = 0; I < 10; ++I) Check(!Sound.PlaySFX("Build/AudioTests/missing.wav"), "missing file returns failure");
	Check(AudioWarnings == Before + 1, "missing file logs once");
	std::ofstream("Build/AudioTests/invalid.wav") << "invalid WAV";
	Check(!Sound.Preload("Build/AudioTests/invalid.wav"), "unsupported WAV does not throw");
	Sound.SetMaxSFXVoices(2);
	Check(Sound.PlaySFX("Assets/Sounds/coin.wav"), "first coin");
	Check(Sound.PlaySFX("Assets/Sounds/coin.wav"), "second coin overlaps first voice");
	Check(Sound.GetActiveSFXCount() == 2, "two independently owned voices");
	Check(!Sound.PlaySFX("Assets/Sounds/footstep00.wav"), "voice limit drops newest");
	Sound.SetPaused(true);
	Check(!Sound.PlaySFX("Assets/Sounds/footstep00.wav"), "paused session rejects new SFX");
	for (int I = 0; I < 70; ++I) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); Sound.Update(); }
	Check(Sound.IsAvailable() && Sound.GetActiveSFXCount() == 2, "paused engine updates preserve voices");
	Sound.SetPaused(false);
	Sound.SetSFXVolume(0.2f); Sound.SetSFXVolume(0.0f);
	for (int I = 0; I < 1000 && Sound.GetActiveSFXCount(); ++I) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); Sound.Update(); }
	Check(Sound.GetActiveSFXCount() == 0, "completed voices reclaimed");
	// A silent, test-only PCM fixture exercises BGM without selecting a game SFX as music.
	{
		std::ofstream Wave("Build/AudioTests/music-fixture.wav", std::ios::binary);
		auto U16 = [&Wave](unsigned V) { Wave.put(static_cast<char>(V)); Wave.put(static_cast<char>(V >> 8)); };
		auto U32 = [&U16](unsigned V) { U16(V & 65535); U16(V >> 16); };
		Wave.write("RIFF", 4); U32(16036); Wave.write("WAVEfmt ", 8); U32(16); U16(1); U16(1);
		U32(8000); U32(16000); U16(2); U16(16); Wave.write("data", 4); U32(16000);
		for (int I = 0; I < 8000; ++I) U16(0);
	}
	Check(Sound.PlayBGM("Build/AudioTests/music-fixture.wav"), "BGM loop");
	Check(Sound.PlayBGM("Build/AudioTests/./music-fixture.wav"), "same normalized BGM request");
	Sound.PauseBGM(); Sound.Update(); Sound.ResumeBGM(); Sound.SetBGMVolume(0.5f); Sound.StopBGM();
	Check(Sound.PlayBGM("Build/AudioTests/music-fixture.wav", false), "BGM restart without loop");
	Check(!Sound.PlayBGM("Build/AudioTests/no-music.wav"), "failed BGM replacement is nonfatal");
	Sound.StopAll(); Check(Sound.GetActiveSFXCount() == 0, "session stop clears voices");
	Sound.Shutdown(); Sound.Shutdown();
	Check(Sound.Initialize(), "fresh audio session after shutdown");
	Sound.Shutdown();
	return Failures ? 1 : 0;
}
