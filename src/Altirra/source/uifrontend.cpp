//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2023 Avery Lee
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
#include <at/atcore/devicesnapshot.h>
#include <at/atcore/snapshotimpl.h>
#include "uiaccessors.h"

class ATUISaveStateFrontEnd final : public ATSnapExchangeObject<ATUISaveStateFrontEnd, "ATUISaveStateFrontEnd"> {
public:
	template<ATExchanger T>
	void Exchange(T& ex);

	VDStringA mOutputName;
};

template<ATExchanger T>
void ATUISaveStateFrontEnd::Exchange(T& ex) {
	ex.Transfer("output_name", &mOutputName);
}

///////////////////////////////////////////////////////////////////////////////

class ATUIFrontEnd final : public IATDeviceSnapshot {
public:
	void LoadState(const IATObjectState *state, ATSnapshotContext& ctx) override;
	vdrefptr<IATObjectState> SaveState(ATSnapshotContext& ctx) const override;
};

void ATUIFrontEnd::LoadState(const IATObjectState *state, ATSnapshotContext& ctx) {
	if (!state) {
		const ATUISaveStateFrontEnd kDefaultState {};

		return LoadState(&kDefaultState, ctx);
	}

	const ATUISaveStateFrontEnd& festate = atser_cast<const ATUISaveStateFrontEnd&>(*state);

	ATUISetCurrentAltOutputName(festate.mOutputName.c_str());
}

vdrefptr<IATObjectState> ATUIFrontEnd::SaveState(ATSnapshotContext& ctx) const {
	vdrefptr state { new ATUISaveStateFrontEnd };

	state->mOutputName = ATUIGetCurrentAltOutputName();

	return state;
}

ATUIFrontEnd g_ATUIFrontEnd;

IATDeviceSnapshot *ATUIGetFrontEnd() {
	return &g_ATUIFrontEnd;
}
