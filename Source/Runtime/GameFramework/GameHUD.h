#pragma once

#include "GameFramework/Actor.h"

class APlayerController;
class FGameViewportClient;
class UWidget;
#include <functional>

// Controls the screen UI for one player. The viewport owns the widget tree.
class AGameHUD : public AActor
{
	DECLARE_CLASS(AGameHUD, AActor)
	REFLECT_START(AGameHUD)
		PROPERTY(WidgetAssetPath)
	REFLECT_END()

public:
	AGameHUD();
	void SetWidgetAssetPath(const FString& Path) { WidgetAssetPath = Path; }
	const FString& GetWidgetAssetPath() const { return WidgetAssetPath; }
	APlayerController* GetOwningPlayerController() const { return OwningPlayerController; }
	FGameViewportClient* GetGameViewport() const { return GameViewport; }

	virtual bool Initialize(APlayerController* PlayerController, FGameViewportClient* Viewport);
	bool ShowWidget(const FString& Path);
	uint64 AddOverlay(const FString& Path, bool bModal = true);
	bool RemoveOverlay(uint64 Handle);
	UWidget* FindWidget(const FString& Name) const;
	bool SetText(const FString& Name, const FString& Text);
	bool StartTimer(const FString& Name);
	bool PauseTimer(const FString& Name);
	bool ResumeTimer(const FString& Name);
	bool StopTimer(const FString& Name);
	bool ResetTimer(const FString& Name, bool bStartImmediately = true);
	double GetTimerElapsedSeconds(const FString& Name) const;
	bool SetWidgetVisible(const FString& Name, bool bVisible);
	bool BindButton(const FString& Name, std::function<void()> Callback);
	void EndPlay() override;

protected:
	virtual void OnWidgetShown(const FString& Path) {}

private:
	FString WidgetAssetPath;
	APlayerController* OwningPlayerController = nullptr;
	FGameViewportClient* GameViewport = nullptr;
};

class ASampleHUD : public AGameHUD
{
	DECLARE_CLASS(ASampleHUD, AGameHUD)

public:
	ASampleHUD();

protected:
	void OnWidgetShown(const FString& Path) override;
};
