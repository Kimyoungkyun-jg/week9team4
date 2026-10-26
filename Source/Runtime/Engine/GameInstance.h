#pragma once

#include <functional>
#include <chrono>
#include <utility>
#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Lua/LuaManager.h"

class UWorld;
class UEngine;
struct FWorldContext;

class UGameInstance : public UObject
{
	DECLARE_CLASS(UGameInstance, UObject)
public:
	UGameInstance();
	~UGameInstance() override;
	virtual void Init();
	virtual void Shutdown();
	void RequestEndSession();
	void StartRunTimer();
	double StopRunTimer();
	double GetRunElapsedSeconds() const;
	bool IsRunTimerRunning() const { return bRunTimerRunning; }
	void SetEndSessionHandler(std::function<void()> Handler) { EndSessionHandler = std::move(Handler); }
	UWorld* GetWorld() const;
	FWorldContext* GetWorldContext() const { return WorldContext; }
	
	void SetWorldContext(FWorldContext* Context);
	virtual bool InitializeForPlayInEditor(int32 PIEInstance);
	virtual bool StartPlayInEditorGameInstance();
	
	UEngine* GetEngine() const;
	FLuaManager* GetLuaManager() { return LuaManager.get(); }
private:
	FWorldContext* WorldContext = nullptr;
	TUniquePtr<FLuaManager> LuaManager;
	std::function<void()> EndSessionHandler;
	bool bEndSessionRequested = false;
	std::chrono::steady_clock::time_point RunStartedAt{};
	double LastRunSeconds = 0.0;
	bool bRunTimerRunning = false;
};
