//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
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

#include <stdafx.h>
#include <at/atui/uimanager.h>
#include "uisavestate.h"
#include "uiaccessors.h"

ATUIDialogSaveState::ATUIDialogSaveState(IATAutoSaveManager& asMgr)
	: mAutoSaveMgr(asMgr)
{
	mCurrentDate = VDGetCurrentDate();
	mCurrentRunTime = asMgr.GetCurrentRunTimeSeconds();
}

void ATUIDialogSaveState::OnCreate() {
	SetAlphaFillColor(0xC0000000);

	mpInnerContainer = new ATUIContainer;
	AddChild(mpInnerContainer);
	mpInnerContainer->SetAnchors(vdrect32f(0.25f, 0.25f, 0.75f, 0.75f));
	mpInnerContainer->SetSizeOffset(vdsize32(0, 0));

	mpImageWidget = new ATUIImage;
	mpImageWidget->SetDockMode(kATUIDockMode_Fill);
	mpInnerContainer->AddChild(mpImageWidget);

	IVDDisplayFont *largeFont = GetManager()->GetThemeFont(kATUIThemeFont_Header);

	mpRunTimeLabel = new ATUILabel;
	mpRunTimeLabel->SetTextOffset(2, 2);
	mpRunTimeLabel->SetFont(largeFont);
	mpRunTimeLabel->SetDockMode(kATUIDockMode_Top);
	mpInnerContainer->AddChild(mpRunTimeLabel);

	mpTimestampLabel = new ATUILabel;
	mpTimestampLabel->SetTextOffset(2, 2);
	mpTimestampLabel->SetFont(largeFont);
	mpTimestampLabel->SetDockMode(kATUIDockMode_Top);
	mpInnerContainer->AddChild(mpTimestampLabel);

	mpCurrentSlotLabel = new ATUILabel;
	mpCurrentSlotLabel->SetTextOffset(2, 2);
	mpCurrentSlotLabel->SetFont(largeFont);
	mpCurrentSlotLabel->SetDockMode(kATUIDockMode_Top);
	mpInnerContainer->AddChild(mpCurrentSlotLabel);

	mSaves.clear();
	mAutoSaveMgr.GetRewindStates(mSaves);

	mCount = (int)mSaves.size();
	mCurrentIndex = mCount ? mCount - 1 : 0;

	UpdateLabel();
	UpdateImage();

	BindAction(kATUIVK_Left, kActionPrev);
	BindAction(kATUIVK_UILeft, kActionPrev);
	BindAction(kATUIVK_Right, kActionNext);
	BindAction(kATUIVK_UIRight, kActionNext);
	BindAction(kATUIVK_Escape, kActionCancel);
	BindAction(kATUIVK_UIReject, kActionCancel);
	BindAction(kATUIVK_Return, kActionAccept);
	BindAction(kATUIVK_UIAccept, kActionAccept);
}

void ATUIDialogSaveState::OnActionStart(uint32 id) {
	OnActionRepeat(id);
}

void ATUIDialogSaveState::OnActionRepeat(uint32 id) {
	if (id == kActionPrev) {
		if (mCount > 0 && mCurrentIndex > 0) {
			--mCurrentIndex;
			UpdateLabel();
			UpdateImage();
		}
	} else if (id == kActionNext) {
		if (mCount > 0 && mCurrentIndex < mCount - 1) {
			++mCurrentIndex;
			UpdateLabel();
			UpdateImage();
		}
	}
}

void ATUIDialogSaveState::OnActionStop(uint32 id) {
	// these are only handled on stop to avoid unwanted actions in emulation
	// when the dialog clears while buttons are held down
	if (id == kActionCancel) {
		GetManager()->EndModal();
		Destroy();
	} else if (id == kActionAccept) {
		if (mCurrentIndex < mCount)
			mSaves[mCurrentIndex]->ApplyState();

		GetManager()->EndModal();
		Destroy();
	}
}

void ATUIDialogSaveState::UpdateLabel() {
	VDStringW s;
	s.sprintf(L"Rewind %d/%d", mCurrentIndex + 1, mCount);

	mpCurrentSlotLabel->SetText(s.c_str());

	if (mCurrentIndex < mCount) {
		const IATAutoSaveView& save = *mSaves[mCurrentIndex];
		VDDate nowDate = mCurrentDate;
		VDDate saveDate = save.GetTimestamp();

		const VDExpandedDate saveLocalDate = VDGetLocalDate(saveDate);

		s.clear();
		VDAppendLocalDateString(s, saveLocalDate);
		s += L" ";
		VDAppendLocalTimeString(s, saveLocalDate);

		if (saveDate < nowDate)
			AppendTimeDuration(s, (nowDate - saveDate).ToSeconds());

		mpTimestampLabel->SetText(s.c_str());

		double runTime = save.GetSimulatedTimeSeconds();
		int totalSeconds = (int)lrintf(runTime);
		int seconds = totalSeconds % 60;
		int minutes = totalSeconds / 60;
		int hours = minutes / 60;
		minutes = minutes % 60;

		s.sprintf(L"Simulation time: %02d:%02d:%02d", hours, minutes, seconds);

		if (mAutoSaveMgr.GetCurrentColdStartId() != save.GetColdStartId())
			s += L" [past cold reset]";
		else if (runTime < mCurrentRunTime)
			AppendTimeDuration(s, mCurrentRunTime - runTime);

		mpRunTimeLabel->SetText(s.c_str());
	}
}

void ATUIDialogSaveState::AppendTimeDuration(VDStringW& s, float seconds) {
	static constexpr struct TimeUnit {
		float mDuration;
		const wchar_t *mpName;
	} kTimeUnits[] {
		{ 86400.0f, L"day" },
		{ 3600.0f, L"hour" },
		{ 60.0f, L"minute" },
		{ 1.0f, L"second" },
	};

	for(const auto& unit : kTimeUnits) {
		if (seconds >= unit.mDuration) {
			int n = (int)lrint(seconds / unit.mDuration);
			s.append_sprintf(L" (%d %ls%ls ago)", n, unit.mpName, n != 1 ? L"s" : L"");
			break;
		}
	}
}

void ATUIDialogSaveState::UpdateImage() {
	const VDPixmap *px = nullptr;
	IVDRefUnknown *owner = nullptr;
	float par = 1.0f;

	if (mCurrentIndex < mCount) {
		IATAutoSaveView& view = *mSaves[mCurrentIndex];

		px = view.GetImage();
		owner = &view;

		par = view.GetImagePAR();
	}

	if (px) {
		mpImageWidget->SetImage(*px, owner);
		mpImageWidget->SetPixelAspectRatio(par);
	} else
		mpImageWidget->SetImage();
}

////////////////////////////////////////////////////////////////////////////////

void ATUIShowDialogRewind(IATAutoSaveManager& autoSaveMgr) {
	if (!autoSaveMgr.GetRewindEnabled())
		return;

	vdrefptr<ATUIDialogSaveState> dlg(new ATUIDialogSaveState(autoSaveMgr));
	auto& mgr = ATUIGetManager();

	dlg->SetPlacementFill();
	mgr.GetMainWindow()->AddChild(dlg);
	mgr.BeginModal(dlg);
}
