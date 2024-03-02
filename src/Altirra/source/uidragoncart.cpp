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
#include "resource.h"
#include "dragoncart.h"

class ATUIDragonCartDialog : public VDDialogFrameW32 {
public:
	ATUIDragonCartDialog(ATPropertySet& props);
	~ATUIDragonCartDialog();

	void OnDestroy();
	void OnDataExchange(bool write);

protected:
	ATPropertySet& mProps;
};

ATUIDragonCartDialog::ATUIDragonCartDialog(ATPropertySet& props)
	: VDDialogFrameW32(IDD_DRAGONCART)
	, mProps(props)
{
}

ATUIDragonCartDialog::~ATUIDragonCartDialog() {
}

void ATUIDragonCartDialog::OnDestroy() {
}

void ATUIDragonCartDialog::OnDataExchange(bool write) {
	if (!write) {
		ATDragonCartSettings settings;

		settings.LoadFromProps(mProps);

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
	} else {
		ATDragonCartSettings settings;
		VDStringW s;

		unsigned a0, a1, a2, a3;
		wchar_t c;
		if (!GetControlText(IDC_NETADDR, s) ||
			4 != swscanf(s.c_str(), L"%u.%u.%u.%u %c", &a0, &a1, &a2, &a3, &c) ||
			(a0 | a1 | a2 | a3) >= 256)
		{
			FailValidation(IDC_NETADDR, L"The network address must be an IPv4 address of the form A.B.C.D and different than your actual network address. Example: 192.168.10.0");
			return;
		}

		settings.mNetAddr = (a0 << 24) + (a1 << 16) + (a2 << 8) + a3;

		if (!GetControlText(IDC_NETMASK, s) ||
			4 != swscanf(s.c_str(), L"%u.%u.%u.%u %c", &a0, &a1, &a2, &a3, &c) ||
			(a0 | a1 | a2 | a3) >= 256)
		{
			FailValidation(IDC_NETMASK, L"The network mask must be of the form A.B.C.D. Example: 255.255.255.0");
			return;
		}

		// Netmask must have contiguous 1 bits on high end: (-mask) must be power of two
		settings.mNetMask = (a0 << 24) + (a1 << 16) + (a2 << 8) + a3;
		uint32 test = 0 - settings.mNetMask;
		if (test & (test - 1)) {
			FailValidation(IDC_NETMASK, L"The network mask is invalid. It must have contiguous 1 bits followed by contiguous 0 bits.");
			return;
		}

		if (settings.mNetAddr & ~settings.mNetMask) {
			FailValidation(IDC_NETADDR, L"The network mask is invalid for the given network address. For a class C network, the address must end in .0 and the mask must be 255.255.255.0.");
			return;
		}

		if (IsButtonChecked(IDC_ACCESS_NAT))
			settings.mAccessMode = ATDragonCartSettings::kAccessMode_NAT;
		else if (IsButtonChecked(IDC_ACCESS_HOSTONLY))
			settings.mAccessMode = ATDragonCartSettings::kAccessMode_HostOnly;
		else
			settings.mAccessMode = ATDragonCartSettings::kAccessMode_None;

		settings.SaveToProps(mProps);
	}
}

///////////////////////////////////////////////////////////////////////////

bool ATUIConfDevDragonCart(VDGUIHandle hParent, ATPropertySet& props) {
	ATUIDragonCartDialog dlg(props);

	return dlg.ShowDialog(hParent) != 0;
}
