#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Camera/MinimalViewInfo.h"

class UCameraModifier : public UObject
{
	DECLARE_CLASS(UCameraModifier, UObject)
	REFLECT_START(ClassName)
	REFLECT_END()
public:
	// Return true to stop applying later modifiers this frame.
	virtual bool ModifyCamera(float, FMinimalViewInfo&) { return false; }
	int32 GetPriority() const { return Priority; }
	void SetPriority(int32 InPriority) { Priority = InPriority; }
	bool IsActive() const { return bActive; }
	void SetActive(bool bInActive) { bActive = bInActive; }

private:
	int32 Priority = 0;
	bool bActive = true;
};
