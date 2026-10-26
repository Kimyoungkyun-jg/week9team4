#include "EnginePCH.h"
#include "Audio/SoundManager.h"
#include <Audio.h>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
	float SafeVolume(float Value) { return std::isfinite(Value) ? std::clamp(Value, 0.0f, 1.0f) : 0.0f; }
	std::filesystem::path WavePath(const std::string& Path)
	{
		return std::filesystem::path(std::u8string(Path.begin(), Path.end())).lexically_normal();
	}
	std::string WaveKey(const std::filesystem::path& Path)
	{
		const auto UTF8 = Path.generic_u8string();
		return std::string(UTF8.begin(), UTF8.end());
	}
}

struct FSoundManager::FImpl
{
	struct FVoice { std::unique_ptr<DirectX::SoundEffectInstance> Instance; float Volume; };
	// Destruction order: voices -> cached wave data -> engine -> COM.
	std::unique_ptr<DirectX::AudioEngine> Engine;
	std::unordered_map<std::string, std::unique_ptr<DirectX::SoundEffect>> Cache;
	std::unordered_set<std::string> FailedPaths;
	std::vector<FVoice> Voices;
	std::unique_ptr<DirectX::SoundEffectInstance> BGM;
	std::string BGMPath;
	float BGMVolume = 1.0f;
	float SFXVolume = 1.0f;
	std::size_t MaxSFXVoices = 32;
	bool bPaused = false;
	bool bReady = false;
	bool bOwnsCOM = false;
	bool bInitialized = false;

	void LogFailure(const std::string& Path, const char* Reason)
	{
		if (FailedPaths.insert(Path).second)
		{
			HTR_LOG(Warning, "Audio: {}: {}", Path, Reason);
			// Standalone Game has no editor log sink; keep errors visible to a debugger.
			const std::string Message = "Audio: " + Path + ": " + Reason + "\n";
			OutputDebugStringA(Message.c_str());
		}
	}

	DirectX::SoundEffect* Load(const std::string& Path)
	{
		if (!bReady) return nullptr;
		std::string Key = Path;
		try
		{
			const auto File = WavePath(Path);
			Key = WaveKey(File);
			if (FailedPaths.contains(Key)) return nullptr;
			if (const auto It = Cache.find(Key); It != Cache.end()) return It->second.get();
			if (!std::filesystem::exists(File))
			{
				LogFailure(Key, "WAV file not found; check the path and repository-root working directory");
				return nullptr;
			}
			auto Effect = std::make_unique<DirectX::SoundEffect>(Engine.get(), File.c_str());
			auto* Result = Effect.get();
			Cache.emplace(Key, std::move(Effect));
			return Result;
		}
		catch (const std::exception& Error)
		{
			LogFailure(Key, (std::string("WAV load failed (check read permissions/format): ") + Error.what()).c_str());
			return nullptr;
		}
	}

	void Collect()
	{
		std::erase_if(Voices, [](const FVoice& Voice) { return Voice.Instance->GetState() == DirectX::STOPPED; });
	}
};

FSoundManager::FSoundManager() : Impl(std::make_unique<FImpl>()) {}
FSoundManager::~FSoundManager() { Shutdown(); }

bool FSoundManager::Initialize()
{
	if (Impl->bInitialized) return Impl->bReady;
	Impl->bInitialized = true;
	const HRESULT Result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	Impl->bOwnsCOM = SUCCEEDED(Result);
	if (FAILED(Result) && Result != RPC_E_CHANGED_MODE)
	{
		Impl->LogFailure("initialization", "COM initialization failed");
		return false;
	}
	try
	{
		Impl->Engine = std::make_unique<DirectX::AudioEngine>();
		Impl->bReady = Impl->Engine->IsAudioDevicePresent();
		if (!Impl->bReady) Impl->LogFailure("device", "No audio output device; game continues without sound. Restart the session after connecting a device.");
	}
	catch (const std::exception& Error) { Impl->LogFailure("initialization", Error.what()); }
	return Impl->bReady;
}

void FSoundManager::Shutdown()
{
	StopAll();
	Impl->Cache.clear();
	Impl->FailedPaths.clear();
	Impl->Engine.reset();
	Impl->bReady = false;
	Impl->bPaused = false;
	Impl->bInitialized = false;
	if (Impl->bOwnsCOM) { CoUninitialize(); Impl->bOwnsCOM = false; }
}

void FSoundManager::Update()
{
	if (!Impl->Engine || !Impl->bReady) return;
	try
	{
		if (!Impl->Engine->Update())
		{
			Impl->LogFailure("device", "Audio device lost; restart the session after restoring the output device.");
			Impl->bReady = false;
			StopAll();
			return;
		}
		Impl->Collect();
	}
	catch (const std::exception& Error)
	{
		Impl->LogFailure("update", Error.what());
		Impl->bReady = false;
		StopAll();
	}
}

bool FSoundManager::Preload(const std::string& Path) { return Impl->Load(Path) != nullptr; }

bool FSoundManager::PlaySFX(const std::string& Path, float Volume)
{
	if (!Impl->bReady || Impl->bPaused) return false;
	Impl->Collect();
	if (Impl->Voices.size() >= Impl->MaxSFXVoices) return false; // Drop newest.
	if (auto* Effect = Impl->Load(Path))
	{
		try
		{
			auto Instance = Effect->CreateInstance();
			Volume = SafeVolume(Volume);
			Instance->SetVolume(Volume * Impl->SFXVolume);
			Instance->Play(false);
			Impl->Voices.push_back({std::move(Instance), Volume});
			return true;
		}
		catch (const std::exception& Error) { Impl->LogFailure(Path, Error.what()); }
	}
	return false;
}

bool FSoundManager::PlayBGM(const std::string& Path, bool bLoop)
{
	if (!Impl->bReady) return false;
	try
	{
		const auto Key = WaveKey(WavePath(Path));
		if (Impl->BGM && Impl->BGMPath == Key && Impl->BGM->GetState() != DirectX::STOPPED) return true;
		if (auto* Effect = Impl->Load(Path))
		{
			auto Instance = Effect->CreateInstance();
			Instance->SetVolume(Impl->BGMVolume);
			Instance->Play(bLoop);
			Impl->BGM = std::move(Instance);
			Impl->BGMPath = Key;
			return true;
		}
	}
	catch (const std::exception& Error) { Impl->LogFailure(Path, Error.what()); }
	return false;
}

void FSoundManager::StopBGM() { Impl->BGM.reset(); Impl->BGMPath.clear(); }
void FSoundManager::PauseBGM() { if (Impl->BGM) Impl->BGM->Pause(); }
void FSoundManager::ResumeBGM()
{
	try { if (Impl->BGM) Impl->BGM->Resume(); }
	catch (const std::exception& Error) { Impl->LogFailure("BGM resume", Error.what()); }
}
void FSoundManager::SetBGMVolume(float Volume)
{
	Impl->BGMVolume = SafeVolume(Volume);
	try { if (Impl->BGM) Impl->BGM->SetVolume(Impl->BGMVolume); }
	catch (const std::exception& Error) { Impl->LogFailure("BGM volume", Error.what()); }
}
void FSoundManager::SetSFXVolume(float Volume)
{
	Impl->SFXVolume = SafeVolume(Volume);
	try { for (auto& Voice : Impl->Voices) Voice.Instance->SetVolume(Voice.Volume * Impl->SFXVolume); }
	catch (const std::exception& Error) { Impl->LogFailure("SFX volume", Error.what()); }
}
void FSoundManager::SetMaxSFXVoices(std::size_t Count)
{
	Impl->MaxSFXVoices = Count;
	if (Impl->Voices.size() > Count) Impl->Voices.resize(Count);
}
void FSoundManager::SetPaused(bool bPaused)
{
	if (Impl->bPaused == bPaused) return;
	Impl->bPaused = bPaused;
	if (Impl->Engine)
	{
		if (bPaused) Impl->Engine->Suspend();
		else
		{
			try { Impl->Engine->Resume(); }
			catch (const std::exception& Error)
			{
				Impl->LogFailure("device resume", Error.what());
				Impl->bReady = false;
				StopAll();
			}
		}
	}
}
void FSoundManager::StopAll() { Impl->Voices.clear(); StopBGM(); }
bool FSoundManager::IsAvailable() const { return Impl->bReady; }
std::size_t FSoundManager::GetActiveSFXCount() const { return Impl->Voices.size(); }
