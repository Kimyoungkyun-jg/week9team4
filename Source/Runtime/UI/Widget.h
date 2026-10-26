#pragma once

#include "UObject/Class.h"
#include "Math/Vector2.h"
#include "Math/Rect.h"
#include "Math/Color.h"
#include "Containers/Array.h"
#include <functional>
#include <chrono>
#include <algorithm>

class UCanvasWidget;
class AMapActor;
class FTexture2D;

// Position is a pixel offset from Anchor; Pivot is a normalized point within Size.
class UWidget : public UObject
{
	DECLARE_CLASS(UWidget, UObject)
	REFLECT_START(UWidget)
		PROPERTY(Position)
		PROPERTY(WidgetName)
		PROPERTY(Size)
		PROPERTY(Anchor)
		PROPERTY(Pivot)
		PROPERTY(ZOrder)
		PROPERTY(bVisible)
	REFLECT_END()

public:
	~UWidget() override;

	void SetWidgetName(const FString& InName) { WidgetName = InName; }
	void SetPosition(const FVector2& InPosition) { Position = InPosition; }
	void SetSize(const FVector2& InSize) { Size = InSize; }
	void SetAnchor(const FVector2& InAnchor) { Anchor = InAnchor; }
	void SetPivot(const FVector2& InPivot) { Pivot = InPivot; }
	void SetZOrder(int32 InZOrder) { ZOrder = InZOrder; }
	void SetVisible(bool bInVisible) { bVisible = bInVisible; }

	const FString& GetWidgetName() const { return WidgetName; }
	const FVector2& GetPosition() const { return Position; }
	const FVector2& GetSize() const { return Size; }
	const FVector2& GetAnchor() const { return Anchor; }
	const FVector2& GetPivot() const { return Pivot; }
	int32 GetZOrder() const { return ZOrder; }
	bool IsVisible() const { return bVisible; }
	UCanvasWidget* GetParent() const { return Parent; }
	const FRect& GetLayoutRect() const { return LayoutRect; }

	// The root fills ViewportRect. Descendants are positioned relative to their parent.
	virtual void Layout(const FRect& ParentRect);

protected:
	FString WidgetName;
	FVector2 Position{0.0f, 0.0f};
	FVector2 Size{100.0f, 100.0f};
	FVector2 Anchor{0.0f, 0.0f};
	FVector2 Pivot{0.0f, 0.0f};
	int32 ZOrder = 0;
	bool bVisible = true;

private:
	friend class UCanvasWidget;
	UCanvasWidget* Parent = nullptr;
	FRect LayoutRect{};
};

class UCanvasWidget : public UWidget
{
	DECLARE_CLASS(UCanvasWidget, UWidget)

public:
	~UCanvasWidget() override;
	bool AddChild(UWidget* Child);
	bool RemoveChild(UWidget* Child); // Detached child remains caller-owned.
	const TArray<UWidget*>& GetChildren() const { return Children; }
	TArray<UWidget*> GetChildrenInPaintOrder() const;
	void Layout(const FRect& ParentRect) override;

private:
	TArray<UWidget*> Children;
};

class UImageWidget : public UWidget
{
	DECLARE_CLASS(UImageWidget, UWidget)
	REFLECT_START(UImageWidget)
		PROPERTY(TexturePath)
		PROPERTY(Tint)
	REFLECT_END()

public:
	void SetTexturePath(const FString& InPath) { TexturePath = InPath; }
	void SetTint(const FLinearColor& InTint) { Tint = InTint; }
	const FString& GetTexturePath() const { return TexturePath; }
	const FLinearColor& GetTint() const { return Tint; }

private:
	FString TexturePath;
	FLinearColor Tint{1.0f, 1.0f, 1.0f, 1.0f};
};

class UTextWidget : public UWidget
{
	DECLARE_CLASS(UTextWidget, UWidget)
	REFLECT_START(UTextWidget)
		PROPERTY(Text)
		PROPERTY(FontPath)
		PROPERTY(FontSize)
		PROPERTY(Color)
	REFLECT_END()

public:
	void SetText(const FString& InText) { Text = InText; }
	void SetFontPath(const FString& InPath) { FontPath = InPath; }
	void SetFontSize(float InSize) { FontSize = InSize; }
	void SetColor(const FLinearColor& InColor) { Color = InColor; }
	const FString& GetText() const { return Text; }
	virtual FString GetDisplayText() const { return Text; }
	const FString& GetFontPath() const { return FontPath; }
	float GetFontSize() const { return FontSize; }
	const FLinearColor& GetColor() const { return Color; }

private:
	FString Text;
	FString FontPath = "Assets/Fonts/Pretendard.json";
	float FontSize = 16.0f;
	FLinearColor Color{1.0f, 1.0f, 1.0f, 1.0f};
};

class UTimerWidget : public UTextWidget
{
	DECLARE_CLASS(UTimerWidget, UTextWidget)
	REFLECT_START(UTimerWidget)
		PROPERTY(bAutoStart)
	REFLECT_END()

public:
	UTimerWidget();
	void SetAutoStart(bool Value) { bAutoStart = Value; }
	bool GetAutoStart() const { return bAutoStart; }
	void Start();
	void Pause();
	void Resume();
	void Stop() { Pause(); }
	void Reset();
	bool IsRunning() const { return bRunning; }
	double GetElapsedSeconds() const;
	FString GetDisplayText() const override;

private:
	bool bAutoStart = true;
	bool bRunning = false;
	double AccumulatedSeconds = 0.0;
	std::chrono::steady_clock::time_point StartedAt{};
};

class UButtonWidget : public UWidget
{
	DECLARE_CLASS(UButtonWidget, UWidget)
	REFLECT_START(UButtonWidget)
		PROPERTY(Text)
		PROPERTY(FontPath)
		PROPERTY(FontSize)
		PROPERTY(BackgroundColor)
		PROPERTY(HoverBackgroundColor)
		PROPERTY(TextColor)
		PROPERTY(HoverTextColor)
	REFLECT_END()

public:
	void SetText(const FString& Value) { Text = Value; }
	void SetFontPath(const FString& Value) { FontPath = Value; }
	void SetFontSize(float Value) { FontSize = Value; }
	void SetBackgroundColor(const FLinearColor& Value) { BackgroundColor = Value; }
	void SetHoverBackgroundColor(const FLinearColor& Value) { HoverBackgroundColor = Value; }
	void SetTextColor(const FLinearColor& Value) { TextColor = Value; }
	void SetHoverTextColor(const FLinearColor& Value) { HoverTextColor = Value; }
	void SetOnClicked(std::function<void()> Callback) { OnClicked = std::move(Callback); }
	void Click() { auto Callback = OnClicked; if (Callback) Callback(); }
	void SetPressed(bool Value) { bPressed = Value; }
	void SetHovered(bool Value) { bHovered = Value; }
	const FString& GetText() const { return Text; }
	const FString& GetFontPath() const { return FontPath; }
	float GetFontSize() const { return FontSize; }
	const FLinearColor& GetBackgroundColor() const { return BackgroundColor; }
	const FLinearColor& GetHoverBackgroundColor() const { return HoverBackgroundColor; }
	const FLinearColor& GetTextColor() const { return TextColor; }
	const FLinearColor& GetHoverTextColor() const { return HoverTextColor; }
	bool IsPressed() const { return bPressed; }
	bool IsHovered() const { return bHovered; }

private:
	FString Text = "Button";
	FString FontPath = "Assets/Fonts/Pretendard.json";
	float FontSize = 20.0f;
	FLinearColor BackgroundColor{0.2f, 0.35f, 0.65f, 1.0f};
	FLinearColor HoverBackgroundColor{0.3f, 0.5f, 0.8f, 1.0f};
	FLinearColor TextColor{1.0f, 1.0f, 1.0f, 1.0f};
	FLinearColor HoverTextColor{1.0f, 0.9f, 0.4f, 1.0f};
	std::function<void()> OnClicked;
	bool bPressed = false;
	bool bHovered = false;
};

class UTextInputWidget : public UButtonWidget
{
	DECLARE_CLASS(UTextInputWidget, UButtonWidget)
	REFLECT_START(UTextInputWidget)
		PROPERTY(Placeholder)
		PROPERTY(MaxLength)
	REFLECT_END()

public:
	UTextInputWidget();
	const FString& GetValue() const { return GetText(); }
	void SetValue(const FString& Value) { SetText(Value); }
	const FString& GetPlaceholder() const { return Placeholder; }
	void SetPlaceholder(const FString& Value) { Placeholder = Value; }
	int32 GetMaxLength() const { return MaxLength; }
	void SetMaxLength(int32 Value) { MaxLength = std::max(1, Value); }
	FString GetDisplayText() const;
	void AppendText(const FString& Text);
	void Backspace();
	void SetFocused(bool Value) { bFocused = Value && !bReadOnly; }
	bool IsFocused() const { return bFocused; }
	void SetReadOnly(bool Value) { bReadOnly = Value; if (Value) bFocused = false; }
	void SetOnSubmitted(std::function<void()> Callback) { OnSubmitted = std::move(Callback); }
	void Submit() { auto Callback = OnSubmitted; if (Callback) Callback(); }

private:
	FString Placeholder = "Enter name";
	int32 MaxLength = 16;
	bool bFocused = false;
	bool bReadOnly = false;
	std::function<void()> OnSubmitted;
};

class UMinimapWidget : public UWidget
{
	DECLARE_CLASS(UMinimapWidget, UWidget)
	REFLECT_START(UMinimapWidget)
		PROPERTY(RadiusTiles)
	REFLECT_END()
public:
	UMinimapWidget();
	~UMinimapWidget() override;
	void SetMap(const AMapActor* InMap);
	void SetPlayer(const FVector2& UV, const FVector2& Direction);
	void ClearPlayer() { bHasPlayer = false; }
	void SetCoins(TArray<FVector2> InCoins) { Coins = std::move(InCoins); }
	const AMapActor* GetMap() const { return Map; }
	FTexture2D* GetTerrainTexture();
	FRect GetMapRect() const;
	bool HasPlayer() const { return bHasPlayer; }
	const FVector2& GetPlayerUV() const { return PlayerUV; }
	const FVector2& GetPlayerDirection() const { return PlayerDirection; }
	const TArray<FVector2>& GetCoins() const { return Coins; }
	float GetRadiusTiles() const { return RadiusTiles; }
	void SetRadiusTiles(float Value) { RadiusTiles = std::max(1.0f, Value); }
private:
	float RadiusTiles = 8.0f;
	const AMapActor* Map = nullptr;
	uint64 CachedGridRevision = static_cast<uint64>(-1);
	TUniquePtr<FTexture2D> TerrainTexture;
	FVector2 PlayerUV;
	FVector2 PlayerDirection{1.0f, 0.0f};
	TArray<FVector2> Coins;
	bool bHasPlayer = false;
};
