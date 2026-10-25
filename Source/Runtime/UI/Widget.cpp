#include "EnginePCH.h"
#include "UI/Widget.h"

#include <algorithm>
#include <cstdio>

UWidget::~UWidget()
{
	if (Parent)
		Parent->RemoveChild(this);
}

void UWidget::Layout(const FRect& ParentRect)
{
	if (!Parent)
	{
		LayoutRect = ParentRect;
		return;
	}
	LayoutRect = {
		ParentRect.X + ParentRect.Width * Anchor.X + Position.X - Size.X * Pivot.X,
		ParentRect.Y + ParentRect.Height * Anchor.Y + Position.Y - Size.Y * Pivot.Y,
		std::max(0.0f, Size.X),
		std::max(0.0f, Size.Y)
	};
}

UCanvasWidget::~UCanvasWidget()
{
	while (!Children.IsEmpty())
	{
		UWidget* Child = Children[Children.Num() - 1];
		Children.RemoveLast();
		Child->Parent = nullptr;
		delete Child;
	}
}

bool UCanvasWidget::AddChild(UWidget* Child)
{
	if (!Child || Child == this || Child->Parent)
		return false;
	for (const UWidget* Ancestor = this; Ancestor; Ancestor = Ancestor->GetParent())
		if (Ancestor == Child)
			return false;
	Child->Parent = this;
	Child->SetOuter(this);
	Children.Add(Child);
	return true;
}

bool UCanvasWidget::RemoveChild(UWidget* Child)
{
	for (uint32 Index = 0; Index < Children.Num(); ++Index)
	{
		if (Children[Index] != Child)
			continue;
		Children.RemoveAt(Index);
		Child->Parent = nullptr;
		Child->SetOuter(nullptr);
		return true;
	}
	return false;
}

TArray<UWidget*> UCanvasWidget::GetChildrenInPaintOrder() const
{
	TArray<UWidget*> Ordered = Children;
	std::stable_sort(Ordered.begin(), Ordered.end(), [](const UWidget* A, const UWidget* B)
	{
		return A->GetZOrder() < B->GetZOrder();
	});
	return Ordered;
}

void UCanvasWidget::Layout(const FRect& ParentRect)
{
	UWidget::Layout(ParentRect);
	for (UWidget* Child : Children)
		Child->Layout(GetLayoutRect());
}

UTimerWidget::UTimerWidget()
{
	SetFontPath("Assets/Fonts/Pretendard.json");
	SetFontSize(24.0f);
}

void UTimerWidget::Start()
{
	Reset();
	Resume();
}

void UTimerWidget::Pause()
{
	if (!bRunning) return;
	AccumulatedSeconds = GetElapsedSeconds();
	bRunning = false;
}

void UTimerWidget::Resume()
{
	if (bRunning) return;
	StartedAt = std::chrono::steady_clock::now();
	bRunning = true;
}

void UTimerWidget::Reset()
{
	AccumulatedSeconds = 0.0;
	bRunning = false;
}

double UTimerWidget::GetElapsedSeconds() const
{
	return AccumulatedSeconds + (bRunning
		? std::chrono::duration<double>(std::chrono::steady_clock::now() - StartedAt).count() : 0.0);
}

FString UTimerWidget::GetDisplayText() const
{
	const uint64 Centiseconds = static_cast<uint64>(GetElapsedSeconds() * 100.0);
	const uint64 TotalSeconds = Centiseconds / 100;
	char Buffer[64];
	if (TotalSeconds >= 3600)
		std::snprintf(Buffer, sizeof(Buffer), "%02llu:%02llu:%02llu.%02llu",
			static_cast<unsigned long long>(TotalSeconds / 3600),
			static_cast<unsigned long long>((TotalSeconds / 60) % 60),
			static_cast<unsigned long long>(TotalSeconds % 60),
			static_cast<unsigned long long>(Centiseconds % 100));
	else
		std::snprintf(Buffer, sizeof(Buffer), "%02llu:%02llu.%02llu",
			static_cast<unsigned long long>(TotalSeconds / 60),
			static_cast<unsigned long long>(TotalSeconds % 60),
			static_cast<unsigned long long>(Centiseconds % 100));
	return Buffer;
}
