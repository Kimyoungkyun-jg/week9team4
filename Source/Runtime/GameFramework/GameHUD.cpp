#include "EnginePCH.h"
#include "GameFramework/GameHUD.h"
#include "Engine/GameViewportClient.h"
#include "UI/Widget.h"
#include "Core/EngineLog.h"

bool AGameHUD::Initialize(APlayerController* PlayerController, FGameViewportClient* Viewport)
{
	if (!PlayerController || !Viewport) return false;
	OwningPlayerController = PlayerController;
	GameViewport = Viewport;
	return WidgetAssetPath.empty() || ShowWidget(WidgetAssetPath);
}

bool AGameHUD::ShowWidget(const FString& Path)
{
	if (!GameViewport || Path.empty() || !GameViewport->LoadWidgetRoot(Path))
	{
		HTR_LOG(Warning, "Failed to show HUD widget {}", Path);
		return false;
	}
	OnWidgetShown(Path);
	return true;
}

uint64 AGameHUD::AddOverlay(const FString& Path, bool bModal)
{
	const uint64 Handle = GameViewport ? GameViewport->AddWidgetOverlay(Path, bModal) : 0;
	if (!Handle) HTR_LOG(Warning, "Failed to add HUD overlay {}", Path);
	if (Handle) OnWidgetShown(Path);
	return Handle;
}

bool AGameHUD::RemoveOverlay(uint64 Handle)
{
	return GameViewport && GameViewport->RemoveWidgetOverlay(Handle);
}

UWidget* AGameHUD::FindWidget(const FString& Name) const
{
	return GameViewport ? GameViewport->FindWidget(Name) : nullptr;
}

bool AGameHUD::SetText(const FString& Name, const FString& Text)
{
	if (UTextWidget* Widget = Cast<UTextWidget>(FindWidget(Name)))
	{ Widget->SetText(Text); return true; }
	return false;
}

bool AGameHUD::SetWidgetVisible(const FString& Name, bool bVisible)
{
	if (UWidget* Widget = FindWidget(Name))
	{ Widget->SetVisible(bVisible); return true; }
	return false;
}

bool AGameHUD::BindButton(const FString& Name, std::function<void()> Callback)
{
	return GameViewport && GameViewport->BindButtonAction(Name, std::move(Callback));
}

void AGameHUD::EndPlay()
{
	if (GameViewport)
	{
		GameViewport->ClearWidgetOverlays();
		GameViewport->ClearWidgetRoot();
	}
	OwningPlayerController = nullptr;
	GameViewport = nullptr;
	Super::EndPlay();
}

ASampleHUD::ASampleHUD()
{
	SetWidgetAssetPath("Assets/UI/SampleHUD.ui.json");
}

void ASampleHUD::OnWidgetShown(const FString& Path)
{
	if (Path == GetWidgetAssetPath())
		BindButton("DemoButton", [] { HTR_LOG(Info, "DemoButton clicked"); });
}
