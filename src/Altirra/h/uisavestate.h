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

#ifndef f_AT_UISAVESTATE_H
#define f_AT_UISAVESTATE_H

#include <at/atui/uicontainer.h>
#include <at/atuicontrols/uiimage.h>
#include <at/atuicontrols/uilabel.h>
#include "autosavemanager.h"

class IATAutoSaveManager;

class ATUIDialogSaveState final : public ATUIContainer {
public:
	ATUIDialogSaveState(IATAutoSaveManager& asMgr);

	void OnCreate() override;

private:
	enum : uint32 {
		kActionPrev = kActionCustom,
		kActionNext,
		kActionCancel,
		kActionAccept,
	};

	void OnActionStart(uint32 id) override;
	void OnActionRepeat(uint32 id) override;
	void OnActionStop(uint32 id) override;
	void UpdateLabel();
	void AppendTimeDuration(VDStringW& s, float seconds);
	void UpdateImage();

	IATAutoSaveManager& mAutoSaveMgr;
	VDDate mCurrentDate;
	double mCurrentRunTime = 0;

	vdrefptr<ATUILabel> mpCurrentSlotLabel;
	vdrefptr<ATUILabel> mpTimestampLabel;
	vdrefptr<ATUILabel> mpRunTimeLabel;
	vdrefptr<ATUIImage> mpImageWidget;
	vdrefptr<ATUIContainer> mpInnerContainer;
	vdvector<vdrefptr<IATAutoSaveView>> mSaves;
	int mCurrentIndex = 0;
	int mCount = 9;
};

#endif

