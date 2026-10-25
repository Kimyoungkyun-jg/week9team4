#pragma once

#include "Core/EngineString.h"
#include "Containers/Array.h"

inline TArray<uint32> DecodeUTF8(const FString& Text)
{
	TArray<uint32> Result;
	const auto* Cursor = reinterpret_cast<const unsigned char*>(Text.c_str());
	const auto* End = Cursor + Text.size();
	while (Cursor < End)
	{
		uint32 CodePoint = 0;
		int Length = 0;
		if (*Cursor < 0x80) { CodePoint = *Cursor; Length = 1; }
		else if ((*Cursor & 0xE0) == 0xC0) { CodePoint = *Cursor & 0x1F; Length = 2; }
		else if ((*Cursor & 0xF0) == 0xE0) { CodePoint = *Cursor & 0x0F; Length = 3; }
		else if ((*Cursor & 0xF8) == 0xF0) { CodePoint = *Cursor & 0x07; Length = 4; }
		else { ++Cursor; continue; }
		if (Cursor + Length > End) break;
		for (int Index = 1; Index < Length; ++Index)
			CodePoint = (CodePoint << 6) | (Cursor[Index] & 0x3F);
		Result.Add(CodePoint);
		Cursor += Length;
	}
	return Result;
}
