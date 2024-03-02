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
#include <at/atui/dialog.h>
#include <at/atui/uiproxies.h>
#include "resource.h"
#include "simulator.h"
#include "rs232.h"

extern ATSimulator g_sim;

class ATUISerialPortsDialog : public VDDialogFrameW32 {
public:
	ATUISerialPortsDialog();

protected:
	bool OnLoaded();
	void OnDataExchange(bool write);
	bool OnCommand(uint32 id, uint32 extcode);
	void UpdateEnables();

	ATRS232Config	mConfig;
	bool mbEnabled;
	bool mbAccept;
	bool mbTelnet;
	VDUIProxyComboBoxControl mComboConnectSpeed;

	static const uint32 kConnectionSpeeds[];
};

const uint32 ATUISerialPortsDialog::kConnectionSpeeds[]={
	300,
	600,
	1200,
	2400,
	4800,
	7200,
	9600,
	12000,
	14400,
	19200,
	38400,
	57600,
	115200,
	230400
};

ATUISerialPortsDialog::ATUISerialPortsDialog()
	: VDDialogFrameW32(IDD_SERIAL_PORTS)
{
}

bool ATUISerialPortsDialog::OnLoaded() {
	AddProxy(&mComboConnectSpeed, IDC_CONNECTION_SPEED);

	VDStringW s;

	for(uint32 i=0; i<sizeof(kConnectionSpeeds)/sizeof(kConnectionSpeeds[0]); ++i) {
		s.sprintf(L"%u baud", kConnectionSpeeds[i]);
		mComboConnectSpeed.AddItem(s.c_str());
	}

	return VDDialogFrameW32::OnLoaded();
}

void ATUISerialPortsDialog::OnDataExchange(bool write) {
	if (write) {
		bool enabled = IsButtonChecked(IDC_ENABLE);

		if (enabled && IsButtonChecked(IDC_ACCEPT_CONNECTIONS)) {
			uint32 port = GetControlValueUint32(IDC_LISTEN_PORT);

			if (port < 1 || port > 65535) {
				FailValidation(IDC_LISTEN_PORT);
				return;
			}

			mConfig.mListenPort = port;
		} else {
			mConfig.mListenPort = 0;
		}

		mConfig.mbAllowOutbound = IsButtonChecked(IDC_ALLOW_OUTBOUND);
		mConfig.mbTelnetEmulation = IsButtonChecked(IDC_TELNET);
		mConfig.mbTelnetLFConversion = IsButtonChecked(IDC_TELNET_LFCONVERSION);
		mConfig.mbListenForIPv6 = IsButtonChecked(IDC_ACCEPT_IPV6);
		mConfig.mbDisableThrottling = IsButtonChecked(IDC_DISABLE_THROTTLING);
		mConfig.mbRequireMatchedDTERate = IsButtonChecked(IDC_REQUIRE_MATCHED_DTE_RATE);

		if (IsButtonChecked(IDC_DEVICE_1030))
			mConfig.mDeviceMode = kATRS232DeviceMode_1030;
		else
			mConfig.mDeviceMode = kATRS232DeviceMode_850;

		if (IsButtonChecked(IDC_SIOLEVEL_NONE))
			mConfig.m850SIOLevel = kAT850SIOEmulationLevel_None;
		else if (IsButtonChecked(IDC_SIOLEVEL_STUBLOADER))
			mConfig.m850SIOLevel = kAT850SIOEmulationLevel_StubLoader;

		int selIdx = mComboConnectSpeed.GetSelection();
		mConfig.mConnectionSpeed = selIdx >= 0 ? kConnectionSpeeds[selIdx] : 9600;

		VDStringW address;
		GetControlText(IDC_DIAL_ADDRESS, address);
		mConfig.mDialAddress = VDTextWToA(address);

		VDStringW service;
		GetControlText(IDC_DIAL_SERVICE, service);
		mConfig.mDialService = VDTextWToA(service);

		g_sim.SetRS232Enabled(enabled);

		if (enabled)
			g_sim.GetRS232()->SetConfig(mConfig);
	} else {
		bool enabled = g_sim.IsRS232Enabled();

		CheckButton(IDC_ENABLE, enabled);

		mbEnabled = enabled;

		if (enabled)
			g_sim.GetRS232()->GetConfig(mConfig);

		mbAccept = mConfig.mListenPort > 0;
		mbTelnet = mConfig.mbTelnetEmulation;
		CheckButton(IDC_TELNET, mbTelnet);
		CheckButton(IDC_TELNET_LFCONVERSION, mConfig.mbTelnetLFConversion);
		CheckButton(IDC_ALLOW_OUTBOUND, mConfig.mbAllowOutbound);
		CheckButton(IDC_ACCEPT_IPV6, mConfig.mbListenForIPv6);
		CheckButton(IDC_DISABLE_THROTTLING, mConfig.mbDisableThrottling);

		CheckButton(IDC_ACCEPT_CONNECTIONS, mConfig.mListenPort > 0);
		SetControlTextF(IDC_LISTEN_PORT, L"%u", mConfig.mListenPort ? mConfig.mListenPort : 9000);

		const uint32 *begin = kConnectionSpeeds;
		const uint32 *end = kConnectionSpeeds + sizeof(kConnectionSpeeds)/sizeof(kConnectionSpeeds[0]);
		const uint32 *it = std::lower_bound(begin, end, mConfig.mConnectionSpeed);

		if (it == end)
			--it;

		if (it != begin && mConfig.mConnectionSpeed - it[-1] < it[0] - mConfig.mConnectionSpeed)
			--it;

		mComboConnectSpeed.SetSelection(it - begin);

		CheckButton(IDC_REQUIRE_MATCHED_DTE_RATE, mConfig.mbRequireMatchedDTERate);

		CheckButton(IDC_DEVICE_1030, mConfig.mDeviceMode == kATRS232DeviceMode_1030);
		CheckButton(IDC_DEVICE_850, mConfig.mDeviceMode == kATRS232DeviceMode_850);

		CheckButton(IDC_SIOLEVEL_NONE, mConfig.m850SIOLevel == kAT850SIOEmulationLevel_None);
		CheckButton(IDC_SIOLEVEL_STUBLOADER, mConfig.m850SIOLevel == kAT850SIOEmulationLevel_StubLoader);

		SetControlText(IDC_DIAL_ADDRESS, VDTextAToW(mConfig.mDialAddress).c_str());
		SetControlText(IDC_DIAL_SERVICE, VDTextAToW(mConfig.mDialService).c_str());

		UpdateEnables();
	}
}

bool ATUISerialPortsDialog::OnCommand(uint32 id, uint32 extcode) {
	if (id == IDC_TELNET) {
		bool telnet = IsButtonChecked(IDC_TELNET);

		if (mbTelnet != telnet) {
			mbTelnet = telnet;

			UpdateEnables();
		}
	} else if (id == IDC_ENABLE) {
		bool enabled = IsButtonChecked(IDC_ENABLE);

		if (mbEnabled != enabled) {
			mbEnabled = enabled;

			UpdateEnables();
		}
	} else if (id == IDC_ACCEPT_CONNECTIONS) {
		bool accept = IsButtonChecked(IDC_ACCEPT_CONNECTIONS);

		if (mbAccept != accept) {
			mbAccept = accept;

			UpdateEnables();
		}
	}

	return false;
}

void ATUISerialPortsDialog::UpdateEnables() {
	bool enabled = mbEnabled;
	bool accept = mbAccept;
	bool telnet = mbTelnet;

	EnableControl(IDC_DEVICE_1030, enabled);
	EnableControl(IDC_DEVICE_850, enabled);
	EnableControl(IDC_TELNET, enabled);
	EnableControl(IDC_TELNET_LFCONVERSION, enabled && telnet);
	EnableControl(IDC_ALLOW_OUTBOUND, enabled);
	EnableControl(IDC_ACCEPT_CONNECTIONS, enabled);
	EnableControl(IDC_LISTEN_PORT, enabled && accept);
	EnableControl(IDC_ACCEPT_IPV6, enabled && accept);
	EnableControl(IDC_DISABLE_THROTTLING, enabled);
	EnableControl(IDC_CONNECTION_SPEED, enabled && accept);
	EnableControl(IDC_EXTENDED_BAUD_RATES, enabled);
	EnableControl(IDC_REQUIRE_MATCHED_DTE_RATE, enabled);
	EnableControl(IDC_SIOLEVEL_NONE, enabled);
	EnableControl(IDC_SIOLEVEL_STUBLOADER, enabled);
	EnableControl(IDC_DIAL_ADDRESS, enabled);
	EnableControl(IDC_DIAL_SERVICE, enabled);

	EnableControl(IDC_STATIC_DEVICE, enabled);
	EnableControl(IDC_STATIC_PROTOCOL, enabled);
	EnableControl(IDC_STATIC_PERMISSIONS, enabled);
	EnableControl(IDC_STATIC_BAUDRATE, enabled);
	EnableControl(IDC_STATIC_SIOLEVEL, enabled);
	EnableControl(IDC_STATIC_DIAL_ADDRESS, enabled);
	EnableControl(IDC_STATIC_DIAL_SERVICE, enabled);
}

void ATUIShowSerialPortsDialog(VDGUIHandle h) {
	ATUISerialPortsDialog dlg;

	dlg.ShowDialog(h);
}
