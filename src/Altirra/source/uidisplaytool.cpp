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
#include "inputcontroller.h"
#include "simulator.h"
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
bool ATUIDisplayTool::HasPriorityOverInputManager() const { return false; }

void ATUIDisplayTool::OnInit() {}
void ATUIDisplayTool::OnShutdown() {}
bool ATUIDisplayTool::OnMouseDownL(sint32 x, sint32 y) { return false; }
bool ATUIDisplayTool::OnMouseMove(sint32 x, sint32 y) { return false; }
bool ATUIDisplayTool::OnMouseUpL(sint32 x, sint32 y) { return false; }

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
