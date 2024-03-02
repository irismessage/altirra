//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2011 Avery Lee
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
#include <at/atnativeui/dialog.h>
#include "verifier.h"
#include "resource.h"
#include "simulator.h"

class ATUIVerifierDialog : public VDDialogFrameW32 {
public:
	ATUIVerifierDialog(ATSimulator& sim);

protected:
	void OnDataExchange(bool write);

	ATSimulator& mSim;

	static const uint32 kVerifierFlags[][2];
};

const uint32 ATUIVerifierDialog::kVerifierFlags[][2] = {
	{ kATVerifierFlag_UndocumentedKernelEntry, IDC_FLAG_UNDOCOSENTRY },
	{ kATVerifierFlag_RecursiveNMI, IDC_FLAG_RECURSIVENMI },
	{ kATVerifierFlag_InterruptRegs, IDC_FLAG_INTERRUPTREGS },
	{ kATVerifierFlag_64KWrap, IDC_FLAG_WRAP64K },
	{ kATVerifierFlag_AbnormalDMA, IDC_FLAG_ABNORMALDMA },
};

ATUIVerifierDialog::ATUIVerifierDialog(ATSimulator& sim)
	: VDDialogFrameW32(IDD_VERIFIER)
	, mSim(sim)
{
}

void ATUIVerifierDialog::OnDataExchange(bool write) {
	if (write) {
		uint32 flags = 0;

		for(uint32 i=0; i<sizeof(kVerifierFlags)/sizeof(kVerifierFlags[0]); ++i) {
			if (IsButtonChecked(kVerifierFlags[i][1]))
				flags |= kVerifierFlags[i][0];
		}

		if (!flags)
			mSim.SetVerifierEnabled(false);
		else {
			mSim.SetVerifierEnabled(true);

			ATCPUVerifier *ver = mSim.GetVerifier();
			if (ver)
				ver->SetFlags(flags);
		}
	} else {
		ATCPUVerifier *ver = mSim.GetVerifier();

		if (ver) {
			uint32 flags = ver->GetFlags();
			for(uint32 i=0; i<sizeof(kVerifierFlags)/sizeof(kVerifierFlags[0]); ++i)
				CheckButton(kVerifierFlags[i][1], (flags & kVerifierFlags[i][0]) != 0);
		}
	}
}

void ATUIShowDialogVerifier(VDGUIHandle h, ATSimulator& sim) {
	ATUIVerifierDialog dlg(sim);

	dlg.ShowDialog(h);
}
