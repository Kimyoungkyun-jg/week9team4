#include "EnginePCH.h"
#include "UI/Widget.h"

#include <algorithm>

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
