//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2013 Avery Lee
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
#include <at/atui/dialog.h>
#include "simulator.h"
#include "dragoncart.h"
#include "resource.h"

extern ATSimulator g_sim;

class ATUIDragonCartDialog : public VDDialogFrameW32 {
public:
	ATUIDragonCartDialog();
	~ATUIDragonCartDialog();

	void OnDestroy();
	void OnDataExchange(bool write);

protected:
	bool OnCommand(uint32 id, uint32 extcode);
	void UpdateEnables();
};

ATUIDragonCartDialog::ATUIDragonCartDialog()
	: VDDialogFrameW32(IDD_DRAGONCART)
{
}

ATUIDragonCartDialog::~ATUIDragonCartDialog() {
}

void ATUIDragonCartDialog::OnDestroy() {
}

void ATUIDragonCartDialog::OnDataExchange(bool write) {
	if (!write) {
		ATDragonCartEmulator *dc = g_sim.GetDragonCart();
		ATDragonCartSettings settings;

		if (dc) {
			CheckButton(IDC_ENABLE, true);
			settings = dc->GetSettings();
		} else {
			CheckButton(IDC_ENABLE, false);
			settings.SetDefault();
		}

		SetControlTextF(IDC_NETADDR, L"%u.%u.%u.%u"
			, (settings.mNetAddr >> 24) & 0xff
			, (settings.mNetAddr >> 16) & 0xff
			, (settings.mNetAddr >>  8) & 0xff
			, (settings.mNetAddr >>  0) & 0xff
			);

		SetControlTextF(IDC_NETMASK, L"%u.%u.%u.%u"
			, (settings.mNetMask >> 24) & 0xff
			, (settings.mNetMask >> 16) & 0xff
			, (settings.mNetMask >>  8) & 0xff
			, (settings.mNetMask >>  0) & 0xff
			);

		switch(settings.mAccessMode) {
			case ATDragonCartSettings::kAccessMode_None:
				CheckButton(IDC_ACCESS_NONE, true);
				break;

			case ATDragonCartSettings::kAccessMode_HostOnly:
				CheckButton(IDC_ACCESS_HOSTONLY, true);
				break;

			case ATDragonCartSettings::kAccessMode_NAT:
				CheckButton(IDC_ACCESS_NAT, true);
				break;
		}

		UpdateEnables();
	} else {
		if (IsButtonChecked(IDC_ENABLE)) {
			ATDragonCartSettings settings;
			VDStringW s;

			unsigned a0, a1, a2, a3;
			wchar_t c;
			if (!GetControlText(IDC_NETADDR, s) || 4 != swscanf(s.c_str(), L"%u.%u.%u.%u%c", &a0, &a1, &a2, &a3, &c)) {
				FailValidation(IDC_NETADDR);
				return;
			}

			if ((a0 | a1 | a2 | a3) >= 256) {
				FailValidation(IDC_NETADDR);
				return;
			}

			settings.mNetAddr = (a0 << 24) + (a1 << 16) + (a2 << 8) + a3;

			if (!GetControlText(IDC_NETMASK, s) || 4 != swscanf(s.c_str(), L"%u.%u.%u.%u%c", &a0, &a1, &a2, &a3, &c)) {
				FailValidation(IDC_NETMASK);
				return;
			}

			if ((a0 | a1 | a2 | a3) >= 256) {
				FailValidation(IDC_NETMASK);
				return;
			}

			settings.mNetMask = (a0 << 24) + (a1 << 16) + (a2 << 8) + a3;

			if (settings.mNetAddr & ~settings.mNetMask) {
				FailValidation(IDC_NETADDR);
				return;
			}

			// Netmask must have contiguous 1 bits on high end: (-mask) must be power of two
			uint32 test = 0 - settings.mNetMask;
			if (test & (test - 1)) {
				FailValidation(IDC_NETMASK);
				return;
			}

			if (IsButtonChecked(IDC_ACCESS_NAT))
				settings.mAccessMode = ATDragonCartSettings::kAccessMode_NAT;
			else if (IsButtonChecked(IDC_ACCESS_HOSTONLY))
				settings.mAccessMode = ATDragonCartSettings::kAccessMode_HostOnly;
			else
				settings.mAccessMode = ATDragonCartSettings::kAccessMode_None;

			g_sim.SetDragonCartEnabled(&settings);
		} else {
			g_sim.SetDragonCartEnabled(NULL);
		}
	}
}

bool ATUIDragonCartDialog::OnCommand(uint32 id, uint32 extcode) {
	if (id == IDC_ENABLE)
		UpdateEnables();

	return false;
}

void ATUIDragonCartDialog::UpdateEnables() {
	static const uint32 kIds[]={
		IDC_STATIC_NETADDR,
		IDC_STATIC_NETMASK,
		IDC_STATIC_BRIDGING,
		IDC_NETADDR,
		IDC_NETMASK,
		IDC_ACCESS_NONE,
		IDC_ACCESS_HOSTONLY,
		IDC_ACCESS_NAT
	};

	const bool enabled = IsButtonChecked(IDC_ENABLE);

	for(size_t i=0; i<vdcountof(kIds); ++i)
		EnableControl(kIds[i], enabled);
}

///////////////////////////////////////////////////////////////////////////

void ATUIShowDialogDragonCart(VDGUIHandle hParent) {
	ATUIDragonCartDialog().ShowDialog(hParent);
}
