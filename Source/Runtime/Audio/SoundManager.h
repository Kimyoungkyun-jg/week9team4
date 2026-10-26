#pragma once

#include <cstddef>
#include <memory>
#include <string>

// Game-thread only. Owned by GameInstance; actors never own playing voices.
class FSoundManager
{
public:
	FSoundManager();
	~FSoundManager();
	FSoundManager(const FSoundManager&) = delete;
	FSoundManager& operator=(const FSoundManager&) = delete;
	bool Initialize();
	void Shutdown();
	void Update();
	bool Preload(const std::string& Path);
	bool PlaySFX(const std::string& Path, float Volume = 1.0f);
	bool PlayBGM(const std::string& Path, bool bLoop = true);
	void StopBGM();
	void PauseBGM();
	void ResumeBGM();
	void SetBGMVolume(float Volume);
	void SetSFXVolume(float Volume);
	void SetMaxSFXVoices(std::size_t Count);
	void SetPaused(bool bPaused);
	void StopAll();
	bool IsAvailable() const;
	std::size_t GetActiveSFXCount() const;
private:
	struct FImpl;
	std::unique_ptr<FImpl> Impl;
};
