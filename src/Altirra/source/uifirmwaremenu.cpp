//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2010 Avery Lee
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include "stdafx.h"
#include "oshelper.h"
#include "firmwaremanager.h"
#include "uimenu.h"
#include "uimenulist.h"
#include "simulator.h"

extern ATSimulator g_sim;
extern void ATUISwitchKernel(uint64 id);
extern void ATUISwitchBasic(uint64 id);

namespace {
	struct SortFirmwarePtrsByName {
		bool operator()(const ATFirmwareInfo *x, const ATFirmwareInfo *y) const {
			return x->mName.comparei(y->mName) < 0;
		}
	};
}

class ATUIFirmwareMenuProvider : public IATUIDynamicMenuProvider {
public:
	void Init(bool basic);

	virtual void RebuildMenu(ATUIMenu& menu, uint32 idbase);
	virtual void UpdateMenu(ATUIMenu& menu, uint32 firstIndex, uint32 n);
	virtual void HandleMenuCommand(uint32 index);

protected:
	bool mbIsBasic;
	vdfastvector<uint64> mFirmwareIds;
};

void ATUIFirmwareMenuProvider::Init(bool basic) {
	mbIsBasic = basic;
}

void ATUIFirmwareMenuProvider::RebuildMenu(ATUIMenu& menu, uint32 idbase) {
	ATFirmwareManager *fwmgr = g_sim.GetFirmwareManager();

	typedef vdvector<ATFirmwareInfo> Firmwares;
	Firmwares fws;

	fwmgr->GetFirmwareList(fws);

	typedef vdfastvector<const ATFirmwareInfo *> SortedFirmwares;
	SortedFirmwares sortedFirmwares;

	for(Firmwares::const_iterator it(fws.begin()), itEnd(fws.end());
		it != itEnd;
		++it)
	{
		const ATFirmwareInfo& fwi = *it;

		if (!fwi.mbVisible)
			continue;

		switch(fwi.mType) {
			case kATFirmwareType_Kernel800_OSA:
			case kATFirmwareType_Kernel800_OSB:
			case kATFirmwareType_KernelXL:
			case kATFirmwareType_KernelXEGS:
			case kATFirmwareType_Kernel5200:
			case kATFirmwareType_Kernel1200XL:
				if (!mbIsBasic)
					sortedFirmwares.push_back(&fwi);
				break;

			case kATFirmwareType_Basic:
				if (mbIsBasic)
					sortedFirmwares.push_back(&fwi);
				break;
		}
	}

	std::sort(sortedFirmwares.begin(), sortedFirmwares.end(), SortFirmwarePtrsByName());

	mFirmwareIds.clear();

	for(SortedFirmwares::const_iterator it(sortedFirmwares.begin()), itEnd(sortedFirmwares.end());
		it != itEnd;
		++it)
	{
		const ATFirmwareInfo& fwi = **it;

		mFirmwareIds.push_back(fwi.mId);

		ATUIMenuItem item;
		item.mId = idbase++;
		item.mText = fwi.mName;
		menu.AddItem(item);
	}
}

void ATUIFirmwareMenuProvider::UpdateMenu(ATUIMenu& menu, uint32 firstIndex, uint32 n) {
	uint64 id = mbIsBasic ? g_sim.GetBasicId() : g_sim.GetKernelId();

	for(uint32 i=0; i<n; ++i) {
		ATUIMenuItem *item = menu.GetItemByIndex(firstIndex + i);

		if (i < mFirmwareIds.size())
			item->mbRadioChecked = (mFirmwareIds[i] == id);
	}
}

void ATUIFirmwareMenuProvider::HandleMenuCommand(uint32 index) {
	if (index < mFirmwareIds.size()) {
		if (mbIsBasic)
			ATUISwitchBasic(mFirmwareIds[index]);
		else
			ATUISwitchKernel(mFirmwareIds[index]);
	}
}

///////////////////////////////////////////////////////////////////////////

ATUIFirmwareMenuProvider g_ATUIFirmwareMenuProviders[2];

void ATUIInitFirmwareMenuCallbacks(ATFirmwareManager *fwmgr) {
	g_ATUIFirmwareMenuProviders[0].Init(false);
	ATUISetDynamicMenuProvider(0, &g_ATUIFirmwareMenuProviders[0]);

	g_ATUIFirmwareMenuProviders[1].Init(true);
	ATUISetDynamicMenuProvider(1, &g_ATUIFirmwareMenuProviders[1]);
}
