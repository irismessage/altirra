//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2022 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#include "stdafx.h"
#include <at/atui/uimanager.h>
#include "inputcontroller.h"
#include "simulator.h"
#include "uiaccessors.h"
#include "uidisplaytool.h"
#include "uirender.h"
#include "uivideodisplaywindow.h"

extern ATSimulator g_sim;

void ATUIDisplayTool::InitTool(ATUIManager& mgr, ATUIVideoDisplayWindow& parent) {
	mpManager = &mgr;
	mpParent = &parent;

	OnInit();
}

void ATUIDisplayTool::ShutdownTool() {
	ClearPrompt();
	OnShutdown();

	mpManager = nullptr;
	mpParent = nullptr;
}

bool ATUIDisplayTool::IsMainTool() const { return false; }
bool ATUIDisplayTool::HasPriorityOverInputManager() const { return IsMainTool(); }

ATUICursorImage ATUIDisplayTool::GetCursorImage(sint32 x, sint32 y) const {
	return IsMainTool() ? kATUICursorImage_Cross : kATUICursorImage_None;
}

void ATUIDisplayTool::OnInit() {}
void ATUIDisplayTool::OnShutdown() {}

bool ATUIDisplayTool::OnMouseDown(sint32 x, sint32 y, uint32 vk) {
	if (vk == kATUIVK_LButton)
		return OnMouseDownL(x, y);
	else if (vk == kATUIVK_RButton)
		return OnMouseDownR(x, y);
	else if (vk == kATUIVK_MButton)
		return OnMouseDownM(x, y);

	return false;
}

bool ATUIDisplayTool::OnMouseDownL(sint32 x, sint32 y) { return false; }
bool ATUIDisplayTool::OnMouseDownR(sint32 x, sint32 y) { return false; }
bool ATUIDisplayTool::OnMouseDownM(sint32 x, sint32 y) { return false; }
bool ATUIDisplayTool::OnMouseMove(sint32 x, sint32 y) { return false; }

bool ATUIDisplayTool::OnMouseUp(sint32 x, sint32 y, uint32 vk) {
	if (vk == kATUIVK_LButton)
		return OnMouseUpL(x, y);
	else if (vk == kATUIVK_RButton)
		return OnMouseUpR(x, y);
	else if (vk == kATUIVK_MButton)
		return OnMouseUpM(x, y);

	return false;
}
bool ATUIDisplayTool::OnMouseUpL(sint32 x, sint32 y) { return false; }
bool ATUIDisplayTool::OnMouseUpR(sint32 x, sint32 y) { return false; }
bool ATUIDisplayTool::OnMouseUpM(sint32 x, sint32 y) { return false; }
bool ATUIDisplayTool::OnMouseWheel(sint32 x, sint32 y, float delta, bool pages) { return false; }

void ATUIDisplayTool::OnCancelMode() {}

void ATUIDisplayTool::SetPrompt(const wchar_t *msg) {
	mbPromptSet = true;
	g_sim.GetUIRenderer()->SetMessage(IATUIRenderer::StatusPriority::Prompt, msg);
}

void ATUIDisplayTool::ClearPrompt() {
	if (mbPromptSet) {
		mbPromptSet = false;

		g_sim.GetUIRenderer()->ClearMessage(IATUIRenderer::StatusPriority::Prompt);
	}
}

void ATUIDisplayTool::Destroy() {
	if (mpParent)
		mpParent->RemoveTool(*this);
}

///////////////////////////////////////////////////////////////////////////

bool ATUIDisplayToolRecalibrateLightPen::IsMainTool() const { return true; }

void ATUIDisplayToolRecalibrateLightPen::OnInit() {
	SetPrompt(L"Recalibrate light pen/gun: Click/fire on reference point");

	mOnTriggerCorrection = std::bind_front(std::mem_fn(&ATUIDisplayToolRecalibrateLightPen::OnTriggerCorrection), this);
	g_sim.GetLightPenPort()->OnTriggerCorrectionEvent().Add(&mOnTriggerCorrection);
}

void ATUIDisplayToolRecalibrateLightPen::OnShutdown() {
	g_sim.GetLightPenPort()->OnTriggerCorrectionEvent().Remove(&mOnTriggerCorrection);
}

void ATUIDisplayToolRecalibrateLightPen::OnTriggerCorrection(bool state, int x, int y) {
	switch(mState) {
		case 0:
			SetPrompt(L"Recalibrate light pen/gun: Click/fire where reference point was shown by running program");
			mState = state ? 1 : 2;
			mReferenceX = x;
			mReferenceY = y;
			break;

		case 1:
		case 2:
			if (mState == (state ? 1 : 2)) {
				g_sim.GetLightPenPort()->ApplyCorrection(mReferenceX - x, mReferenceY - y);

				Destroy();
			}
			break;
	}
}

///////////////////////////////////////////////////////////////////////////

ATUIDisplayToolPanAndZoom::ATUIDisplayToolPanAndZoom(bool main)
	: mbMainTool(main)
{
}

bool ATUIDisplayToolPanAndZoom::IsMainTool() const {
	return mbMainTool;
}

ATUICursorImage ATUIDisplayToolPanAndZoom::GetCursorImage(sint32 x, sint32 y) const {
	return mbDragZoomActive ? kATUICursorImage_SizeVert
		: mbMainTool || mbDragPanActive ? kATUICursorImage_Move
		: kATUICursorImage_None;
}

void ATUIDisplayToolPanAndZoom::OnInit() {
	if (mbMainTool)
		SetPrompt(L"LMB to pan, Ctrl+LMB or wheel to zoom, Esc to exit");
}

bool ATUIDisplayToolPanAndZoom::OnMouseDownL(sint32 x, sint32 y) {
	if (mbMainTool) {
		if (mpParent->GetManager()->IsKeyDown(kATUIVK_Control))
			mbDragZoomActive = true;
		else
			mbDragPanActive = true;

		mDragAnchorX = x;
		mDragAnchorY = y;
		mDragLastX = x;
		mDragLastY = y;
		return true;
	}

	return false;
}

bool ATUIDisplayToolPanAndZoom::OnMouseDownM(sint32 x, sint32 y) {
	if (!mbMainTool) {
		mbDragPanActive = true;
		mDragAnchorX = x;
		mDragAnchorY = y;
		mDragLastX = x;
		mDragLastY = y;
		return true;
	}

	return false;
}

bool ATUIDisplayToolPanAndZoom::OnMouseMove(sint32 x, sint32 y) {
	if (mbDragPanActive || mbDragZoomActive) {
		const auto& outputRect = mpParent->GetCurrentOutputArea();
		if (!outputRect.empty()) {
			if (mbDragZoomActive)
				OnMouseWheel(mDragAnchorX, mDragAnchorY, (float)(y - mDragLastY) / outputRect.height() * -20.0f, false);
			else
				ATUISetDisplayPanOffset(ATUIGetDisplayPanOffset() - vdfloat2((float)(x - mDragLastX) / (float)outputRect.width(), (float)(y - mDragLastY) / (float)outputRect.height()));
		}

		mDragLastX = x;
		mDragLastY = y;
		return true;
	}

	return false;
}

bool ATUIDisplayToolPanAndZoom::OnMouseUpL(sint32 x, sint32 y) {
	if (mbDragPanActive || mbDragZoomActive) {
		OnCancelMode();
		return true;
	}

	return false;
}

bool ATUIDisplayToolPanAndZoom::OnMouseUpM(sint32 x, sint32 y) {
	return OnMouseUpL(x, y);
}

bool ATUIDisplayToolPanAndZoom::OnMouseWheel(sint32 x, sint32 y, float delta, bool pages) {
	if (!mbMainTool && !mpParent->GetManager()->IsKeyDown(kATUIVK_LAlt))
		return false;

	// compute display rect relative origin for the zoom
	const vdfloat2 offset = ATUIGetDisplayPanOffset();

	// The offset is the normalized point in the display rect that is pinned to the center
	// of the viewport, around which all other points are scaled. Convert the desired
	// point to normalized display coordinates and compute how much it will move by the
	// zoom change, then apply the opposite to the pan offset.
	//
	// Note that changing the zoom will recompute the display rect, so we need to do this
	// in a specific order.

	const float oldZoom = ATUIGetDisplayZoom();
	const float newZoom = oldZoom * powf(2.0f, delta / 20.0f);

	const auto& outputRect = mpParent->GetCurrentOutputArea();
	if (!outputRect.empty()) {
		vdfloat2 origin;
		origin.x = (float)(x - outputRect.left) / (float)outputRect.width () - 0.5f;
		origin.y = (float)(y - outputRect.top ) / (float)outputRect.height() - 0.5f;

		ATUISetDisplayPanOffset(offset + (origin - offset) * (newZoom / oldZoom - 1.0f));
	}

	ATUISetDisplayZoom(newZoom);
	return true;
}

void ATUIDisplayToolPanAndZoom::OnCancelMode() {
	mbDragPanActive = false;
	mbDragZoomActive = false;
}

