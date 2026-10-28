#include "EnginePCH.h"
#include "Editor/Sequencer/ImSequencer.h"
#include "imgui.h"
#include "imgui_internal.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>

namespace ImSequencer
{
	namespace
	{
		bool SequencerAddDelButton(ImDrawList* draw_list, ImVec2 pos, bool add)
		{
			ImGuiIO& io = ImGui::GetIO();
			ImRect btnRect(pos, ImVec2(pos.x + 16.0f, pos.y + 16.0f));
			const bool overBtn = btnRect.Contains(io.MousePos);
			const bool containedClick = overBtn && btnRect.Contains(io.MouseClickedPos[0]);
			const bool clickedBtn = containedClick && io.MouseReleased[0];
			const int btnColor = overBtn ? 0xAAEAFFAA : 0x77A3B2AA;
			if (containedClick && io.MouseDownDuration[0] > 0.0f)
			{
				btnRect.Expand(2.0f);
			}

			const float midy = pos.y + 8.0f - 0.5f;
			const float midx = pos.x + 8.0f - 0.5f;
			draw_list->AddRect(btnRect.Min, btnRect.Max, btnColor, 4.0f);
			draw_list->AddLine(ImVec2(btnRect.Min.x + 3.0f, midy), ImVec2(btnRect.Max.x - 3.0f, midy), btnColor, 2.0f);
			if (add)
			{
				draw_list->AddLine(ImVec2(midx, btnRect.Min.y + 3.0f), ImVec2(midx, btnRect.Max.y - 3.0f), btnColor, 2.0f);
			}
			return clickedBtn;
		}
	}

	bool Sequencer(
		SequenceInterface* sequence,
		int* currentFrame,
		bool* expanded,
		int* selectedEntry,
		int* firstFrame,
		int sequenceOptions)
	{
		bool ret = false;
		ImGuiIO& io = ImGui::GetIO();
		const int cx = static_cast<int>(io.MousePos.x);
		const int cy = static_cast<int>(io.MousePos.y);
		static float framePixelWidth = 10.0f;
		static float framePixelWidthTarget = 10.0f;
		int legendWidth = 220;

		static int movingEntry = -1;
		static int movingPos = -1;
		static int movingPart = -1;
		int delEntry = -1;
		int dupEntry = -1;
		int ItemHeight = 22;

		bool popupOpened = false;
		const int sequenceCount = sequence->GetItemCount();
		if (expanded && !*expanded)
		{
			ImGui::InvisibleButton("canvas", ImVec2(ImGui::GetContentRegionAvail().x, static_cast<float>(ItemHeight)));
			ImDrawList* draw_list = ImGui::GetWindowDrawList();
			const ImVec2 canvas_pos = ImGui::GetItemRectMin();
			const ImVec2 canvas_size = ImGui::GetItemRectSize();
			draw_list->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + ItemHeight), 0xFF3D3837, 0.0f);
			char tmps[256];
			std::snprintf(tmps, sizeof(tmps), "%d Frames / %d Tracks", sequence->GetFrameMax() - sequence->GetFrameMin(), sequenceCount);
			draw_list->AddText(ImVec2(canvas_pos.x + 26.0f, canvas_pos.y + 3.0f), 0xFFFFFFFF, tmps);
			if (SequencerAddDelButton(draw_list, ImVec2(canvas_pos.x + 4.0f, canvas_pos.y + 3.0f), true))
			{
				*expanded = true;
			}
			return false;
		}

		ImGui::BeginGroup();

		ImDrawList* draw_list = ImGui::GetWindowDrawList();
		const ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
		ImVec2 canvas_size = ImGui::GetContentRegionAvail();
		if (canvas_size.x < 100.0f) canvas_size.x = 100.0f;
		if (canvas_size.y < 120.0f) canvas_size.y = 120.0f;

		const int firstFrameUsed = firstFrame ? *firstFrame : 0;
		int controlHeight = sequenceCount * ItemHeight;
		for (int i = 0; i < sequenceCount; i++)
		{
			controlHeight += static_cast<int>(sequence->GetCustomHeight(i));
		}
		const int frameCount = ImMax(sequence->GetFrameMax() - sequence->GetFrameMin(), 1);

		static bool MovingScrollBar = false;
		static bool MovingCurrentFrame = false;

		struct CustomDrawItem
		{
			int index;
			ImRect customRect;
			ImRect legendRect;
			ImRect clippingRect;
			ImRect legendClippingRect;
		};
		ImVector<CustomDrawItem> customDraws;
		ImVector<CustomDrawItem> compactCustomDraws;

		const int visibleFrameCount = static_cast<int>(ImFloor((canvas_size.x - legendWidth) / framePixelWidth));
		const float barWidthRatio = ImMin(visibleFrameCount / static_cast<float>(frameCount), 1.0f);
		const float barWidthInPixels = barWidthRatio * (canvas_size.x - legendWidth);

		const ImRect regionRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y));

		static bool panningView = false;
		static ImVec2 panningViewSource;
		static int panningViewFrame;
		if (ImGui::IsWindowFocused() && io.KeyAlt && io.MouseDown[2])
		{
			if (!panningView)
			{
				panningViewSource = io.MousePos;
				panningView = true;
				panningViewFrame = *firstFrame;
			}
			*firstFrame = panningViewFrame - static_cast<int>((io.MousePos.x - panningViewSource.x) / framePixelWidth);
			*firstFrame = ImClamp(*firstFrame, sequence->GetFrameMin(), ImMax(sequence->GetFrameMin(), sequence->GetFrameMax() - visibleFrameCount));
		}
		if (panningView && !io.MouseDown[2])
		{
			panningView = false;
		}
		framePixelWidthTarget = ImClamp(framePixelWidthTarget, 1.0f, 50.0f);
		framePixelWidth = ImLerp(framePixelWidth, framePixelWidthTarget, 0.33f);

		const int maxFirstFrame = ImMax(sequence->GetFrameMin(), sequence->GetFrameMax() - visibleFrameCount);
		if (firstFrame && *firstFrame > maxFirstFrame)
		{
			*firstFrame = maxFirstFrame;
		}
		if (firstFrame && *firstFrame < sequence->GetFrameMin())
		{
			*firstFrame = sequence->GetFrameMin();
		}

		// 상단 타임라인 눈금자 영역
		const bool hasScrollBar = true;
		const ImVec2 headerSize(canvas_size.x, static_cast<float>(ItemHeight));
		const ImVec2 scrollBarSize(canvas_size.x, 14.0f);
		ImGui::InvisibleButton("topBar", headerSize);
		draw_list->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + headerSize.x, canvas_pos.y + headerSize.y), 0xFF282828, 0.0f);
		const ImVec2 childFramePos = ImGui::GetCursorScreenPos();
		const ImVec2 childFrameSize(canvas_size.x, canvas_size.y - 8.0f - headerSize.y - (hasScrollBar ? scrollBarSize.y : 0.0f));
		ImGui::PushStyleColor(ImGuiCol_FrameBg, 0);
		ImGui::BeginChild(889, childFrameSize, ImGuiChildFlags_FrameStyle);
		sequence->focused = ImGui::IsWindowFocused();
		ImGui::InvisibleButton("contentBar", ImVec2(canvas_size.x, static_cast<float>(ImMax(controlHeight, static_cast<int>(childFrameSize.y)))));
		const ImVec2 contentMin = ImGui::GetItemRectMin();
		const ImVec2 contentMax = ImGui::GetItemRectMax();
		const ImRect contentRect(contentMin, contentMax);
		const float contentHeight = contentMax.y - contentMin.y;

		// 배경 그리드
		draw_list->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), 0xFF202020, 0.0f);

		// 상단 좌측 트랙 헤더
		const ImRect topRect(ImVec2(canvas_pos.x + legendWidth, canvas_pos.y), ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + ItemHeight));

		if (!MovingCurrentFrame && !MovingScrollBar && movingEntry == -1 && (sequenceOptions & SEQUENCER_CHANGE_FRAME) && currentFrame && *currentFrame >= 0 && topRect.Contains(io.MousePos) && io.MouseDown[0])
		{
			MovingCurrentFrame = true;
		}
		if (MovingCurrentFrame)
		{
			if (frameCount)
			{
				*currentFrame = static_cast<int>((io.MousePos.x - topRect.Min.x) / framePixelWidth) + firstFrameUsed;
				if (*currentFrame < sequence->GetFrameMin())
					*currentFrame = sequence->GetFrameMin();
				if (*currentFrame >= sequence->GetFrameMax())
					*currentFrame = sequence->GetFrameMax();
				ret = true;
			}
			if (!io.MouseDown[0])
				MovingCurrentFrame = false;
		}

		// 헤더 좌측 버튼 및 라벨
		draw_list->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + legendWidth, canvas_pos.y + ItemHeight), 0xFF323232, 0.0f);
		if (expanded)
		{
			if (SequencerAddDelButton(draw_list, ImVec2(canvas_pos.x + 4.0f, canvas_pos.y + 3.0f), false))
				*expanded = false;
		}
		draw_list->AddText(ImVec2(canvas_pos.x + 26.0f, canvas_pos.y + 3.0f), 0xFFDDDDDD, "Tracks");

		if (sequenceOptions & SEQUENCER_ADD)
		{
			if (SequencerAddDelButton(draw_list, ImVec2(canvas_pos.x + legendWidth - ItemHeight + 2.0f, canvas_pos.y + 3.0f), true))
				ImGui::OpenPopup("addEntry");

			if (ImGui::BeginPopup("addEntry"))
			{
				for (int i = 0; i < sequence->GetItemTypeCount(); i++)
					if (ImGui::Selectable(sequence->GetItemTypeName(i)))
					{
						sequence->Add(i);
						*selectedEntry = sequence->GetItemCount() - 1;
						ret = true;
					}

				ImGui::EndPopup();
				popupOpened = true;
			}
		}

		// 프레임 눈금선
		int modFrameCount = 10;
		int frameStep = 1;
		while ((modFrameCount * framePixelWidth) < 100.0f)
		{
			modFrameCount *= 2;
			frameStep *= 2;
		}
		const int halfModFrameCount = modFrameCount / 2;

		auto drawLine = [&](int i, int regionHeight) {
			const bool baseIndex = ((i % modFrameCount) == 0) || (i == sequence->GetFrameMax() || i == sequence->GetFrameMin());
			const bool halfIndex = (i % halfModFrameCount) == 0;
			const int px = static_cast<int>(canvas_pos.x) + static_cast<int>(i * framePixelWidth) + legendWidth - static_cast<int>(firstFrameUsed * framePixelWidth);
			const int tiretStart = baseIndex ? 4 : (halfIndex ? 10 : 14);
			const int tiretEnd = baseIndex ? regionHeight : ItemHeight;

			if (px <= (canvas_pos.x + canvas_size.x) && px >= (canvas_pos.x + legendWidth))
			{
				draw_list->AddLine(ImVec2(static_cast<float>(px), canvas_pos.y + static_cast<float>(tiretStart)), ImVec2(static_cast<float>(px), canvas_pos.y + static_cast<float>(tiretEnd) - 1.0f), 0xFF555555, 1.0f);
				draw_list->AddLine(ImVec2(static_cast<float>(px), canvas_pos.y + static_cast<float>(ItemHeight)), ImVec2(static_cast<float>(px), canvas_pos.y + static_cast<float>(regionHeight) - 1.0f), 0x33444444, 1.0f);
			}

			if (baseIndex && px > (canvas_pos.x + legendWidth))
			{
				char tmps[64];
				std::snprintf(tmps, sizeof(tmps), "%d", i);
				draw_list->AddText(ImVec2(static_cast<float>(px) + 3.0f, canvas_pos.y + 2.0f), 0xFFBBBBBB, tmps);
			}
		};

		auto drawLineContent = [&](int i) {
			const int px = static_cast<int>(canvas_pos.x) + static_cast<int>(i * framePixelWidth) + legendWidth - static_cast<int>(firstFrameUsed * framePixelWidth);
			if (px <= (canvas_pos.x + canvas_size.x) && px >= (canvas_pos.x + legendWidth))
			{
				draw_list->AddLine(ImVec2(static_cast<float>(px), contentMin.y), ImVec2(static_cast<float>(px), contentMax.y), 0x22555555, 1.0f);
			}
		};

		for (int i = sequence->GetFrameMin(); i <= sequence->GetFrameMax(); i += frameStep)
		{
			drawLine(i, ItemHeight);
		}
		drawLine(sequence->GetFrameMin(), ItemHeight);
		drawLine(sequence->GetFrameMax(), ItemHeight);

		draw_list->PushClipRect(childFramePos, ImVec2(childFramePos.x + childFrameSize.x, childFramePos.y + childFrameSize.y), true);

		// 좌측 트랙 라벨 그리기
		size_t customHeight = 0;
		for (int i = 0; i < sequenceCount; i++)
		{
			int type = 0;
			int* start = nullptr;
			int* end = nullptr;
			unsigned int color = 0;
			sequence->Get(i, &start, &end, &type, &color);
			const ImVec2 tpos(contentMin.x + 3.0f, contentMin.y + i * ItemHeight + 3.0f + customHeight);
			draw_list->AddRectFilled(
				ImVec2(contentMin.x, contentMin.y + i * ItemHeight + customHeight),
				ImVec2(contentMin.x + legendWidth - 1.0f, contentMin.y + (i + 1) * ItemHeight + customHeight - 1.0f),
				(selectedEntry && *selectedEntry == i) ? 0xFF4A3B28 : ((i & 1) ? 0xFF2B2B2B : 0xFF252525));
			draw_list->AddText(tpos, 0xFFFFFFFF, sequence->GetItemLabel(i));

			if (sequenceOptions & SEQUENCER_DELETE)
			{
				if (SequencerAddDelButton(draw_list, ImVec2(contentMin.x + legendWidth - ItemHeight + 2.0f, contentMin.y + i * ItemHeight + 3.0f + customHeight), false))
					delEntry = i;

				if (SequencerAddDelButton(draw_list, ImVec2(contentMin.x + legendWidth - ItemHeight - ItemHeight + 4.0f, contentMin.y + i * ItemHeight + 3.0f + customHeight), true))
					dupEntry = i;
			}
			customHeight += sequence->GetCustomHeight(i);
		}

		// 트랙 행 배경 및 클립 바 그리기
		customHeight = 0;
		for (int i = 0; i < sequenceCount; i++)
		{
			const unsigned int col = (i & 1) ? 0xFF262626 : 0xFF202020;

			const size_t localCustomHeight = sequence->GetCustomHeight(i);
			const ImVec2 pos = ImVec2(contentMin.x + legendWidth, contentMin.y + ItemHeight * i + 1 + customHeight);
			const ImVec2 sz = ImVec2(canvas_size.x + canvas_pos.x, pos.y + ItemHeight - 1 + localCustomHeight);
			if (!popupOpened && cy >= pos.y && cy < pos.y + (ItemHeight + localCustomHeight) && movingEntry == -1 && (cy > canvas_pos.y + ItemHeight) && cy < (childFramePos.y + childFrameSize.y))
			{
				if (io.MouseClicked[0] && selectedEntry)
				{
					*selectedEntry = i;
				}
				if (io.MouseDoubleClicked[0])
				{
					sequence->DoubleClick(i);
				}
			}
			draw_list->AddRectFilled(pos, sz, col, 0.0f);
			customHeight += localCustomHeight;
		}

		draw_list->PushClipRect(ImVec2(childFramePos.x + legendWidth, childFramePos.y), ImVec2(childFramePos.x + childFrameSize.x, childFramePos.y + childFrameSize.y), true);

		for (int i = sequence->GetFrameMin(); i <= sequence->GetFrameMax(); i += frameStep)
		{
			drawLineContent(i);
		}
		drawLineContent(sequence->GetFrameMin());
		drawLineContent(sequence->GetFrameMax());

		// 선택된 트랙 하이라이트
		if (selectedEntry && *selectedEntry >= 0 && *selectedEntry < sequenceCount)
		{
			customHeight = 0;
			for (int i = 0; i < *selectedEntry; i++)
				customHeight += sequence->GetCustomHeight(i);
			draw_list->AddRectFilled(
				ImVec2(contentMin.x, contentMin.y + ItemHeight * *selectedEntry + customHeight),
				ImVec2(contentMin.x + canvas_size.x, contentMin.y + ItemHeight * (*selectedEntry + 1) + customHeight),
				0x33EA8020, 0.0f);
		}

		// 각 트랙의 구간 바와 커스텀 영역 등록
		customHeight = 0;
		for (int i = 0; i < sequenceCount; i++)
		{
			int* start = nullptr;
			int* end = nullptr;
			unsigned int color = 0;
			int type = 0;
			sequence->Get(i, &start, &end, &type, &color);
			const size_t localCustomHeight = sequence->GetCustomHeight(i);

			const ImVec2 pos = ImVec2(contentMin.x + legendWidth - firstFrameUsed * framePixelWidth, contentMin.y + ItemHeight * i + 1 + customHeight);
			const ImVec2 slotP1(pos.x + *start * framePixelWidth, pos.y + 2.0f);
			const ImVec2 slotP2(pos.x + *end * framePixelWidth + framePixelWidth, pos.y + ItemHeight - 2.0f);
			const ImVec2 slotP3(pos.x + *end * framePixelWidth + framePixelWidth, pos.y + ItemHeight - 2.0f + localCustomHeight);
			const unsigned int slotColor = color | 0xFF000000;
			const unsigned int slotColorHalf = (color & 0xFFFFFF) | 0x40000000;

			if (slotP1.x <= (canvas_size.x + contentMin.x) && slotP2.x >= (contentMin.x + legendWidth))
			{
				draw_list->AddRectFilled(slotP1, slotP3, slotColorHalf, 2.0f);
				draw_list->AddRectFilled(slotP1, slotP2, slotColor, 2.0f);
			}
			if (ImRect(slotP1, slotP2).Contains(io.MousePos) && io.MouseDoubleClicked[0])
			{
				sequence->DoubleClick(i);
			}

			const float maxHandleWidth = (slotP2.x - slotP1.x) / 3.0f;
			const float handleWidth = ImMin(10.0f, ImMax(4.0f, maxHandleWidth));
			ImRect rects[3] = {
				ImRect(slotP1, ImVec2(slotP1.x + handleWidth, slotP2.y)),
				ImRect(ImVec2(slotP2.x - handleWidth, slotP1.y), slotP2),
				ImRect(slotP1, slotP2)
			};

			const unsigned int quadColor[] = { 0xFFFFFFFF, 0xFFFFFFFF, slotColor + (selectedEntry && *selectedEntry == i ? 0 : 0x202020) };
			if (movingEntry == -1 && (sequenceOptions & SEQUENCER_EDIT_STARTEND))
			{
				for (int j = 2; j >= 0; j--)
				{
					ImRect& rc = rects[j];
					if (!rc.Contains(io.MousePos))
						continue;
					draw_list->AddRectFilled(rc.Min, rc.Max, quadColor[j], 2.0f);
				}

				for (int j = 0; j < 3; j++)
				{
					ImRect& rc = rects[j];
					if (!rc.Contains(io.MousePos))
						continue;
					if (!ImRect(childFramePos, ImVec2(childFramePos.x + childFrameSize.x, childFramePos.y + childFrameSize.y)).Contains(io.MousePos))
						continue;
					if (ImGui::IsMouseClicked(0) && !MovingScrollBar && !MovingCurrentFrame)
					{
						movingEntry = i;
						movingPos = cx;
						movingPart = j + 1;
						sequence->BeginEdit(movingEntry);
						break;
					}
				}
			}

			if (localCustomHeight > 0)
			{
				const ImVec2 rp(canvas_pos.x, contentMin.y + ItemHeight * i + 1 + customHeight);
				const ImRect customRect(
					ImVec2(rp.x + legendWidth - (firstFrameUsed - sequence->GetFrameMin() - 0.5f) * framePixelWidth, rp.y + static_cast<float>(ItemHeight)),
					ImVec2(rp.x + legendWidth + (sequence->GetFrameMax() - firstFrameUsed - 0.5f + 2.0f) * framePixelWidth, rp.y + static_cast<float>(localCustomHeight + ItemHeight)));
				const ImRect clippingRect(
					ImVec2(rp.x + static_cast<float>(legendWidth), rp.y + static_cast<float>(ItemHeight)),
					ImVec2(rp.x + canvas_size.x, rp.y + static_cast<float>(localCustomHeight + ItemHeight)));

				const ImRect legendRect(
					ImVec2(rp.x, rp.y + static_cast<float>(ItemHeight)),
					ImVec2(rp.x + static_cast<float>(legendWidth), rp.y + static_cast<float>(localCustomHeight)));
				const ImRect legendClippingRect(
					ImVec2(canvas_pos.x, canvas_pos.y + static_cast<float>(ItemHeight)),
					ImVec2(canvas_pos.x + static_cast<float>(legendWidth), canvas_pos.y + static_cast<float>(localCustomHeight + ItemHeight)));
				customDraws.push_back({ i, customRect, legendRect, clippingRect, legendClippingRect });
			}
			else
			{
				const ImVec2 rp(canvas_pos.x, contentMin.y + ItemHeight * i + customHeight);
				const ImRect customRect(
					ImVec2(rp.x + legendWidth - (firstFrameUsed - sequence->GetFrameMin() - 0.5f) * framePixelWidth, rp.y),
					ImVec2(rp.x + legendWidth + (sequence->GetFrameMax() - firstFrameUsed - 0.5f + 2.0f) * framePixelWidth, rp.y + static_cast<float>(ItemHeight)));
				const ImRect clippingRect(
					ImVec2(rp.x + static_cast<float>(legendWidth), rp.y),
					ImVec2(rp.x + canvas_size.x, rp.y + static_cast<float>(ItemHeight)));
				compactCustomDraws.push_back({ i, customRect, ImRect(), clippingRect, ImRect() });
			}
			customHeight += localCustomHeight;
		}

		// 클립 드래그 처리
		if (movingEntry >= 0)
		{
			ImGui::SetNextFrameWantCaptureMouse(true);
			const int diffFrame = static_cast<int>((cx - movingPos) / framePixelWidth);
			if (std::abs(cx - movingPos) >= framePixelWidth)
			{
				int* start = nullptr;
				int* end = nullptr;
				unsigned int color = 0;
				int type = 0;
				sequence->Get(movingEntry, &start, &end, &type, &color);
				if (selectedEntry)
					*selectedEntry = movingEntry;
				int& l = *start;
				int& r = *end;
				if (movingPart & 1)
					l += diffFrame;
				if (movingPart & 2)
					r += diffFrame;
				if (l < sequence->GetFrameMin())
				{
					if (movingPart & 2)
						r -= l - sequence->GetFrameMin();
					l = sequence->GetFrameMin();
				}
				if (movingPart & 1 && l > r)
					l = r;
				if (movingPart & 2 && r < l)
					r = l;
				movingPos += static_cast<int>(diffFrame * framePixelWidth);
				ret = true;
			}
			if (!io.MouseDown[0])
			{
				if (!diffFrame && movingPart && selectedEntry)
				{
					*selectedEntry = movingEntry;
					ret = true;
				}
				movingEntry = -1;
				sequence->EndEdit();
			}
		}

		// 커스텀 드로우 콜백 실행
		for (int d = 0; d < customDraws.Size; ++d)
		{
			const CustomDrawItem& draw = customDraws[d];
			sequence->CustomDraw(draw.index, draw_list, draw.customRect, draw.legendRect, draw.clippingRect, draw.legendClippingRect);
		}
		for (int d = 0; d < compactCustomDraws.Size; ++d)
		{
			const CustomDrawItem& draw = compactCustomDraws[d];
			sequence->CustomDrawCompact(draw.index, draw_list, draw.customRect, draw.clippingRect);
		}

		// 현재 프레임 빨간색 플레이헤드 선
		if (currentFrame && firstFrame && *currentFrame >= *firstFrame && *currentFrame <= sequence->GetFrameMax())
		{
			const float cursorOffset = contentMin.x + legendWidth + (*currentFrame - firstFrameUsed) * framePixelWidth + framePixelWidth / 2.0f - 1.0f;
			draw_list->AddLine(ImVec2(cursorOffset, canvas_pos.y), ImVec2(cursorOffset, contentMax.y), 0xFFE04040, 2.5f);
			char tmps[64];
			std::snprintf(tmps, sizeof(tmps), "%d", *currentFrame);
			draw_list->AddRectFilled(ImVec2(cursorOffset - 4.0f, canvas_pos.y), ImVec2(cursorOffset + ImGui::CalcTextSize(tmps).x + 6.0f, canvas_pos.y + ItemHeight - 2.0f), 0xFFE04040, 3.0f);
			draw_list->AddText(ImVec2(cursorOffset + 2.0f, canvas_pos.y + 2.0f), 0xFFFFFFFF, tmps);
		}

		draw_list->PopClipRect();
		draw_list->PopClipRect();

		if (delEntry != -1)
		{
			sequence->Del(delEntry);
			if (selectedEntry && (*selectedEntry == delEntry || *selectedEntry >= sequence->GetItemCount()))
				*selectedEntry = -1;
			ret = true;
		}

		if (dupEntry != -1)
		{
			sequence->Duplicate(dupEntry);
			ret = true;
		}

		ImGui::EndChild();
		ImGui::PopStyleColor();

		// 하단 가로 스크롤 및 줌 바
		if (hasScrollBar)
		{
			ImGui::InvisibleButton("scrollBar", scrollBarSize);
			const ImVec2 scrollBarMin = ImGui::GetItemRectMin();
			const ImVec2 scrollBarMax = ImGui::GetItemRectMax();

			const float startFrameOffset = (static_cast<float>(firstFrameUsed - sequence->GetFrameMin()) / static_cast<float>(frameCount)) * (canvas_size.x - legendWidth);
			const ImVec2 scrollBarA(scrollBarMin.x + legendWidth, scrollBarMin.y - 2.0f);
			const ImVec2 scrollBarB(scrollBarMin.x + canvas_size.x, scrollBarMax.y - 1.0f);
			draw_list->AddRectFilled(scrollBarA, scrollBarB, 0xFF1A1A1A, 0.0f);

			const bool inScrollBar = ImRect(scrollBarA, scrollBarB).Contains(io.MousePos);
			draw_list->AddRectFilled(scrollBarA, scrollBarB, 0xFF141414, 6.0f);

			const ImVec2 scrollBarC(scrollBarMin.x + legendWidth + startFrameOffset, scrollBarMin.y);
			const ImVec2 scrollBarD(scrollBarMin.x + legendWidth + barWidthInPixels + startFrameOffset, scrollBarMax.y - 2.0f);
			draw_list->AddRectFilled(scrollBarC, scrollBarD, (inScrollBar || MovingScrollBar) ? 0xFF686868 : 0xFF505050, 6.0f);

			const ImRect barHandleLeft(scrollBarC, ImVec2(scrollBarC.x + 14.0f, scrollBarD.y));
			const ImRect barHandleRight(ImVec2(scrollBarD.x - 14.0f, scrollBarC.y), scrollBarD);

			const bool onLeft = barHandleLeft.Contains(io.MousePos);
			const bool onRight = barHandleRight.Contains(io.MousePos);

			static bool sizingRBar = false;
			static bool sizingLBar = false;

			draw_list->AddRectFilled(barHandleLeft.Min, barHandleLeft.Max, (onLeft || sizingLBar) ? 0xFFAAAAAA : 0xFF777777, 6.0f);
			draw_list->AddRectFilled(barHandleRight.Min, barHandleRight.Max, (onRight || sizingRBar) ? 0xFFAAAAAA : 0xFF777777, 6.0f);

			const ImRect scrollBarThumb(scrollBarC, scrollBarD);
			static constexpr float MinBarWidth = 44.0f;
			if (sizingRBar)
			{
				if (!io.MouseDown[0])
				{
					sizingRBar = false;
				}
				else
				{
					const float barNewWidth = ImMax(barWidthInPixels + io.MouseDelta.x, MinBarWidth);
					const float barRatio = barNewWidth / (canvas_size.x - legendWidth);
					framePixelWidthTarget = framePixelWidth = (canvas_size.x - legendWidth) / (static_cast<float>(frameCount) * barRatio);
					int newVisibleFrameCount = static_cast<int>((canvas_size.x - legendWidth) / framePixelWidthTarget);
					int lastFrame = *firstFrame + newVisibleFrameCount;
					if (lastFrame > sequence->GetFrameMax())
					{
						framePixelWidthTarget = framePixelWidth = (canvas_size.x - legendWidth) / static_cast<float>(sequence->GetFrameMax() - *firstFrame);
					}
				}
			}
			else if (sizingLBar)
			{
				if (!io.MouseDown[0])
				{
					sizingLBar = false;
				}
				else if (fabsf(io.MouseDelta.x) > FLT_EPSILON)
				{
					const float barNewWidth = ImMax(barWidthInPixels - io.MouseDelta.x, MinBarWidth);
					const float barRatio = barNewWidth / (canvas_size.x - legendWidth);
					const float previousFramePixelWidthTarget = framePixelWidthTarget;
					framePixelWidthTarget = framePixelWidth = (canvas_size.x - legendWidth) / (static_cast<float>(frameCount) * barRatio);
					int newVisibleFrameCount = static_cast<int>(visibleFrameCount / barRatio);
					int newFirstFrame = *firstFrame + newVisibleFrameCount - visibleFrameCount;
					newFirstFrame = ImClamp(newFirstFrame, sequence->GetFrameMin(), ImMax(sequence->GetFrameMax() - visibleFrameCount, sequence->GetFrameMin()));
					if (newFirstFrame == *firstFrame)
					{
						framePixelWidth = framePixelWidthTarget = previousFramePixelWidthTarget;
					}
					else
					{
						*firstFrame = newFirstFrame;
					}
				}
			}
			else
			{
				if (MovingScrollBar)
				{
					if (!io.MouseDown[0])
					{
						MovingScrollBar = false;
					}
					else
					{
						const float framesPerPixelInBar = barWidthInPixels / static_cast<float>(visibleFrameCount);
						if (framesPerPixelInBar > 0.0001f)
						{
							*firstFrame = static_cast<int>((io.MousePos.x - panningViewSource.x) / framesPerPixelInBar) - panningViewFrame;
							*firstFrame = ImClamp(*firstFrame, sequence->GetFrameMin(), ImMax(sequence->GetFrameMax() - visibleFrameCount, sequence->GetFrameMin()));
						}
					}
				}
				else
				{
					if (scrollBarThumb.Contains(io.MousePos) && ImGui::IsMouseClicked(0) && firstFrame && !MovingCurrentFrame && movingEntry == -1)
					{
						if (onRight)
						{
							sizingRBar = true;
						}
						else if (onLeft)
						{
							sizingLBar = true;
						}
						else
						{
							MovingScrollBar = true;
							panningViewSource = io.MousePos;
							panningViewFrame = -*firstFrame;
						}
					}
				}
			}
		}

		ImGui::EndGroup();
		return ret;
	}
}
