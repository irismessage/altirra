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
#include <vd2/Dita/services.h>
#include <at/atcore/propertyset.h>
#include <at/atnativeui/dialog.h>
#include "oshelper.h"
#include "resource.h"

///////////////////////////////////////////////////////////////////////////

class ATUIDialogDeviceNetSerial : public VDDialogFrameW32 {
public:
	ATUIDialogDeviceNetSerial(ATPropertySet& props);

	void OnDataExchange(bool write) override;

protected:
	ATPropertySet& mProps;
};

ATUIDialogDeviceNetSerial::ATUIDialogDeviceNetSerial(ATPropertySet& props)
	: VDDialogFrameW32(IDD_DEVICE_NETSERIAL)
	, mProps(props)
{
}

void ATUIDialogDeviceNetSerial::OnDataExchange(bool write) {
	if (!write) {
		SetControlText(IDC_ADDRESS, mProps.GetString("connect_addr"));
		SetControlTextF(IDC_PORT, L"%u", mProps.GetUint32("port", 9000));
		SetControlTextF(IDC_BAUDRATE, L"%u", mProps.GetUint32("baud_rate", 31250));

		if (mProps.GetBool("listen"))
			CheckButton(IDC_MODE_LISTEN, true);
		else
			CheckButton(IDC_MODE_CONNECT, true);
	} else {
		VDStringW portStr;
		GetControlText(IDC_PORT, portStr);
		
		unsigned port = 0;
		wchar_t dummy;
		if (portStr.empty() || 1 != swscanf(portStr.c_str(), L"%u%c", &port, &dummy) || port < 1 || port > 65535) {
			FailValidation(IDC_PORT);
			return;
		}
		
		VDStringW baudRateStr;
		GetControlText(IDC_BAUDRATE, baudRateStr);

		unsigned baudRate = 0;
		if (baudRateStr.empty() || 1 != swscanf(baudRateStr.c_str(), L"%u%c", &baudRate, &dummy) || baudRate < 1 || baudRate > 1000000) {
			FailValidation(IDC_BAUDRATE);
			return;
		}

		mProps.Clear();

		VDStringW address;
		GetControlText(IDC_ADDRESS, address);

		if (!address.empty())
			mProps.SetString("connect_addr", address.c_str());

		mProps.SetUint32("port", port);
		mProps.SetUint32("baud_rate", baudRate);

		if (IsButtonChecked(IDC_MODE_LISTEN))
			mProps.SetBool("listen", true);
	}
}

bool ATUIConfDevNetSerial(VDGUIHandle hParent, ATPropertySet& props) {
	ATUIDialogDeviceNetSerial dlg(props);

	return dlg.ShowDialog(hParent) != 0;
}
