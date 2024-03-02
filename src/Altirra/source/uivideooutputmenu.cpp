//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2021 Avery Lee
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
#include <at/atcore/devicevideo.h>
#include <at/atui/uimenulist.h>
#include "simulator.h"
#include "uiaccessors.h"
#include "uimenu.h"

extern ATSimulator g_sim;

class ATUIVideoOutputMenuProvider final : public IATUIDynamicMenuProvider {
public:
	void Init(IATDeviceVideoManager& vidMgr);

	bool IsRebuildNeeded() const override;
	void RebuildMenu(ATUIMenu& menu, uint32 pos, uint32 idbase) override;
	void UpdateMenu(ATUIMenu& menu, uint32 firstIndex, uint32 n) override;
	void HandleMenuCommand(uint32 index) override;

protected:
	uint32 mLastOutputListStateCount = 0;
	IATDeviceVideoManager *mpVidMgr = nullptr;
};

void ATUIVideoOutputMenuProvider::Init(IATDeviceVideoManager& vidMgr) {
	mpVidMgr = &vidMgr;
}

bool ATUIVideoOutputMenuProvider::IsRebuildNeeded() const {
	return mLastOutputListStateCount != mpVidMgr->GetOutputListChangeCount();
}

void ATUIVideoOutputMenuProvider::RebuildMenu(ATUIMenu& menu, uint32 pos, uint32 idbase) {
	uint32 n = mpVidMgr->GetOutputCount();

	ATUIMenuItem item;
	for(uint32 i = 0; i < n; ++i) {
		item.mId = idbase++;

		const wchar_t *name = mpVidMgr->GetOutput(i)->GetDisplayName();
		if (i < 9)
			item.mText.sprintf(L"&%d %ls", (i + 2) % 10, name);
		else
			item.mText = name;

		menu.InsertItem(pos + i, item);
	}

	mLastOutputListStateCount = mpVidMgr->GetOutputListChangeCount();
}

void ATUIVideoOutputMenuProvider::UpdateMenu(ATUIMenu& menu, uint32 firstIndex, uint32 n) {
	sint32 selIdx = ATUIGetCurrentAltViewIndex();

	for(uint32 i=0; i<n; ++i) {
		ATUIMenuItem *item = menu.GetItemByIndex(firstIndex + i);

		item->mbRadioChecked = ((sint32)i == selIdx);
	}
}

void ATUIVideoOutputMenuProvider::HandleMenuCommand(uint32 index) {
	ATUISetAltViewByIndex(index);
}

///////////////////////////////////////////////////////////////////////////

ATUIVideoOutputMenuProvider g_ATUIVideoOutputMenuProvider;

void ATUIInitVideoOutputMenuCallback(IATDeviceVideoManager& vidMgr) {
	g_ATUIVideoOutputMenuProvider.Init(vidMgr);
	ATUISetDynamicMenuProvider(kATUIDynamicMenu_VideoOutput, &g_ATUIVideoOutputMenuProvider);
}
