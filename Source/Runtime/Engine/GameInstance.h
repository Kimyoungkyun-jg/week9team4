#pragma once

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
	virtual ~UGameInstance() override = default;
	virtual void Init();
	virtual void Shutdown();
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
};
