//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2021 Avery Lee
//	Debugger module - debug link device
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

#include <stdafx.h>
#include <vd2/system/binary.h>
#include <at/atdebugger/internal/debugdevice.h>

void ATCreateDeviceDebug(const ATPropertySet& pset, IATDevice **dev) {
	*dev = new ATDebugDevice;
	(*dev)->AddRef();
}

extern constexpr ATDeviceDefinition g_ATDeviceDefDebug = { "debug", "debug", L"Debug Link Device", ATCreateDeviceDebug };

void *ATDebugDevice::AsInterface(uint32 id) {
	if (id == IATDeviceSIO::kTypeID)
		return static_cast<IATDeviceSIO *>(this);
	else if (id == IATDebugDevice::kTypeID)
		return static_cast<IATDebugDevice *>(this);

	return ATDevice::AsInterface(id);
}

void ATDebugDevice::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefDebug;
}

void ATDebugDevice::Init() {
	mpSIOMgr->AddDevice(this);
}

void ATDebugDevice::Shutdown() {
	if (mpSIOMgr) {
		mpSIOMgr->RemoveDevice(this);
		mpSIOMgr = nullptr;
	}
}

void ATDebugDevice::InitSIO(IATDeviceSIOManager *mgr) {
	mpSIOMgr = mgr;
}

ATDebugDevice::CmdResponse ATDebugDevice::OnSerialAccelCommand(const ATDeviceSIORequest& request) {
	// We _only_ accept accelerated requests for the debug device.
	if (request.mDevice != 0x7E)
		return kCmdResponse_NotHandled;

	uint8 buf[256];

	switch(request.mCommand) {
		case 0x20:		// IDENTIFY
			// AUX1 = maximum number of bytes to return (0 = 256)
			// Returned data frame is length in bytes followed by ATASCII
			// string, no terminator
			{
				const uint32 len = request.mAUX[0] ? request.mAUX[0] : 256;
				const uint32 buflen = len - 1;
				const uint32 msglen = std::min<size_t>(buflen, mDebuggerName.size());

				memset(buf + 1, 0x20, buflen);
				memcpy(buf + 1, mDebuggerName.data(), msglen);
				buf[0] = (uint8)msglen;

				mpSIOMgr->BeginCommand();
				mpSIOMgr->SendACK();
				mpSIOMgr->SendComplete();
				mpSIOMgr->SendData(buf, len, true);
				mpSIOMgr->EndCommand();
			}

			if (mpDebugger)
				mpDebugger->OnDebugLinkIdentify();

			return kCmdResponse_Start;

		case 0x24:		// START SDX SYMBOLS
			if (mpDebugger)
				mpDebugger->OnSDXSymbolStart();
			return kCmdResponse_Send_ACK_Complete;

		case 0x25:		// ADD SDX SYMBOL
			mpSIOMgr->BeginCommand();
			mpSIOMgr->SendACK();
			mpSIOMgr->ReceiveData(kReceiveId_AddSDXSymbol, 13, true);
			mpSIOMgr->SendComplete();
			mpSIOMgr->EndCommand();
			return kCmdResponse_Start;

		case 0x26:		// END SDX SYMBOLS
			if (mpDebugger)
				mpDebugger->OnSDXSymbolEnd();
			return kCmdResponse_Send_ACK_Complete;

		case 0x27:		// PROCESS START
			{
				const uint32 len = request.mAUX[0] ? request.mAUX[0] : 256;

				mpSIOMgr->BeginCommand();
				mpSIOMgr->SendACK();
				mpSIOMgr->ReceiveData(kReceiveId_ProcessStartName, len, true);
				mpSIOMgr->SendComplete();
				mpSIOMgr->EndCommand();
			}
			return kCmdResponse_Start;

		default:
			return kCmdResponse_Fail_NAK;
	}
}

void ATDebugDevice::OnSerialReceiveComplete(uint32 id, const void *data, uint32 len, bool checksumOK) {
	const uint8 *data8 = (const uint8 *)data;

	if (id == kReceiveId_AddSDXSymbol) {
		char name[9];
		int namelen = 8;

		for(int i=0; i<8; ++i) {
			uint8 c = data8[i + 2];

			if (c < 0x21 || c >= 0x7f) {
				namelen = i;
				break;
			}

			name[i] = (char)c;
		}

		if (!namelen)
			return;

		name[namelen] = 0;

		if (mpDebugger)
			mpDebugger->OnSDXSymbolAdd(VDReadUnalignedLEU16(&data8[11]), name);

	} else if (id == kReceiveId_ProcessStartName) {
		VDStringA path;
		for(uint32 i=0; i<len; ++i) {
			const uint8 c = data8[i];
			
			if (c < 0x20 || c >= 0x7f)
				break;

			path += (char)c;
		}

		if (mpDebugger)
			mpDebugger->OnSDXProcessStart(path.c_str());
	}
}

void ATDebugDevice::SetDebugger(const char *name, IATDebugDeviceCallback& debugger) {
	mDebuggerName = name;
	mpDebugger = &debugger;
}

