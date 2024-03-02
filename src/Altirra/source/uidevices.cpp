//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2014 Avery Lee
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
#include <windows.h>
#include <richedit.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/math.h>
#include <vd2/system/registry.h>
#include <vd2/Dita/services.h>
#include <at/atcore/device.h>
#include <at/atcore/devicemanager.h>
#include <at/atcore/propertyset.h>
#include <at/atnativeui/dialog.h>
#include "oshelper.h"
#include "resource.h"

///////////////////////////////////////////////////////////////////////////

class ATUIDialogDeviceNew : public VDResizableDialogFrameW32 {
public:
	ATUIDialogDeviceNew(const vdfunction<bool(const char *)>& filter);

	const char *GetDeviceTag() const { return mpDeviceTag; }

protected:
	bool OnLoaded();
	void OnDestroy();
	void OnDataExchange(bool write);
	void OnItemSelectionChanged(VDUIProxyTreeViewControl *sender, int);
	void UpdateHelpText();
	void AppendRTF(VDStringA& rtf, const wchar_t *text);

	const vdfunction<bool(const char *)>& mFilter;
	VDUIProxyTreeViewControl mTreeView;
	VDDelegate mDelItemSelChanged;
	const char *mpDeviceTag;
	const wchar_t *mpLastHelpText;

	struct TreeEntry {
		const char *mpTag;
		const wchar_t *mpText;
		const wchar_t *mpHelpText;
	};

	struct CategoryEntry {
		const wchar_t *mpText;
		const char *mpTag;
		std::initializer_list<TreeEntry> mDevices;
	};

	class TreeNode : public vdrefcounted<IVDUITreeViewVirtualItem> {
	public:
		TreeNode(const TreeEntry& node) : mNode(node) {}

		void *AsInterface(uint32 id) { return nullptr; }

		virtual void GetText(VDStringW& s) const {
			s = mNode.mpText;
		}

		const TreeEntry& mNode;
	};

	static const CategoryEntry kCategories[];
};

const ATUIDialogDeviceNew::CategoryEntry ATUIDialogDeviceNew::kCategories[]={
	{
		L"Parallel Bus Interface (PBI) devices",
		"pbi",
		{
			{
				"blackbox",
				L"Black Box",
				L"The CSS Black Box attaches to the PBI port to provide SCSI hard disk, RAM disk, "
					L"parallel printer port, and RS-232 serial port functionality.\n"
					L"The built-in firmware is configured by pressing the Black Box's Menu button."
			},
			{
				"kmkjzide",
				L"KMK/J\u017B IDE v1",
				L"A PBI-based hard disk interface for parallel ATA hard disk devices.",
			},
			{
				"kmkjzide2",
				L"KMK/J\u017B IDE v2 (IDEPlus 2.0)",
				L"A PBI-based hard disk interface for parallel ATA hard disk devices. Version 2 "
					L"adds expanded firmware and built-in SpartaDOS X capability."
			},
			{
				"mio", L"MIO",
				L"The ICD Multi-I/O (MIO) is a PBI device that adds SCSI hard disk, RAM disk, "
					L"parallel printer port, and RS-232 serial port functionality.\n"
					L"The MIO firmware's built-in menu is accessed with Select+Reset."
			},
		},
	},
	{
		L"Cartridge devices",
		"cartridge",
		{
			{ "dragoncart", L"DragonCart",
				L"Adds an Ethernet port to the computer. No firmware is built-in; "
					L"networking software must be run separately."
			},
			{ "myide-d5xx", L"MyIDE (cartridge)",
				L"IDE adapter attached to cartridge port, using the $D5xx address range."
			},
			{ "myide2", L"MyIDE-II",
				L"Enhanced version of the MyIDE interface, providing a CompactFlash interface with "
					L"hot-swap support and banked flash memory with three access windows."
			},
			{ "rtime8", L"R-Time 8",
				L"A simple cartridge that provides a real-time clock. It was specifically designed "
					L"to work in the pass-through port of the SpartaDOS X cartridge. Separate software "
					L"is required to provide the Z: device handler."
			},
			{ "side", L"SIDE",
				L"Cartridge with 512K of flash and a CompactFlash port for storage."
			},
			{ "side2", L"SIDE 2",
				L"Enhanced version of SIDE with enhanced banking, CompactFlash change detection support, "
					L"and improved Ultimate1MB compatibility."
			},
			{ "slightsid", L"SlightSID",
				L"A cartridge-shaped adapter for the C64's SID sound chip."
			},
			{ "veronica", L"Veronica",
				L"A cartridge-based 65C816 coprocessor with 128K of on-board memory.\n"
					L"Veronica does not have on-board firmware, so software must be externally loaded to use it."
			},
		}
	},
	{
		L"Internal devices",
		"internal",
		{
			{ "covox", L"Covox",
				L"Provides a simple DAC for 8-bit digital sound."
			},
			{ "soundboard", L"SoundBoard",
				L"Provides multi-channel, wavetable sound capabilities with 512K of internal memory."
			},
			{ "myide-d1xx", L"MyIDE (internal)",
				L"IDE adapter attached to internal port, using the $D1xx address range."
			},
		}
	},
	{
		L"Controller port devices",
		"joyport",
		{
			{ "corvus", L"Corvus Disk Interface",
				L"External hard drive connected to controller ports 3 and 4 of a 400/800. Additional handler software "
					L"is required to access the disk (not included)."
			},
			{ "dongle", L"Joystick port dongle",
				L"Small device attached to joystick port used to lock software operation to physical posesssion "
					L"of the dongle device. This form implements a simple mapping function where up to three bits output "
					L"by the computer produce up to four bits of output from the dongle."
			},
			{ "xep80", L"XEP80",
				L"External 80-column video output that attaches to the joystick port and drives a separate display. "
					L"Additional handler software is required (provided on Additions disk)."
			},
		}
	},
	{
		L"Hard disks",
		"harddisk",
		{
			{ "harddisk", L"Hard disk",
				L"Hard drive or solid state drive. This device can be added as a sub-device to any parent device "
					L"that has an IDE, CompactFlash, or SCSI interface."
			}
		},
	},
	{
		L"Serial devices",
		"serial",
		{
			{ "modem", L"Modem",
				L"Hayes compatible modem with connection simulated over TCP/IP."
			}
		}
	},
	{
		L"Disk drives",
		"sio",
		{
			{ "diskdrive810", L"810 disk drive (full emulation)",
				L"Full 810 disk drive emulation, including 6507 CPU.\n"
					L"The 810 has a 40-track drive and only reads single density disks. It is the lowest common denominator among all "
					L"compatible disk drives and will not read any larger disk formats, such as enhanced or double density."
			},

			{ "diskdrive810archiver", L"810 Archiver disk drive (full emulation)",
				L"Full emulation for the 810 Archiver disk drive, a.k.a. 810 with \"The Chip\", including 6507 CPU."
			},

			{ "diskdrivehappy810", L"Happy 810 disk drive (full emulation)",
				L"Full Happy 810 disk drive emulation, including 6507 CPU.\n"
					L"The Happy 810 adds track buffering and custom code execution capabilities to the 810 drive. It does not support "
					L"double density, however."
			},

			{ "diskdrive1050", L"1050 disk drive (full emulation)",
				L"Full 1050 disk drive emulation, including 6507 CPU.\n"
					L"The 1050 supports single density and enhanced density disks. True double density (256 bytes/sector) disks are not "
					L"supported."
			},

			{ "diskdrive1050duplicator", L"1050 Duplicator disk drive (full emulation)",
				L"Full 1050 Duplicator disk drive emulation, including 6507 CPU."
			},

			{ "diskdriveusdoubler", L"US Doubler disk drive (full emulation)",
				L"Full US Doubler disk drive emulation, including 6507 CPU.\n"
					L"The US Doubler is an enhanced 1050 drive that supports true double density and high speed operation."
			},

			{ "diskdrivespeedy1050", L"Speedy 1050 disk drive (full emulation)",
				L"Full Speedy 1050 disk drive emulation, including 65C02 CPU.\n"
					L"The Speedy 1050 is an enhanced 1050 that supports double density, track buffering, and high speed operation.\n"
					L"NOTE: The Speedy 1050 may operate erratically in high-speed mode with NTSC computers due to marginal write timing."
			},

			{ "diskdrivehappy1050", L"Happy 1050 disk drive (full emulation)",
				L"Full Happy 1050 disk drive emulation, including 6502 CPU."
			},

			{ "diskdrivesuperarchiver", L"Super Archiver disk drive (full emulation)",
				L"Full Super Archiver disk drive emulation, including 6507 CPU."
			},

			{ "diskdrivetoms1050", L"TOMS 1050 disk drive (full emulation)",
				L"Full TOMS 1050 disk drive emulation, including 6507 CPU."
			},

			{ "diskdrivetygrys1050", L"Tygrys 1050 disk drive (full emulation)",
				L"Full Tygrys 1050 disk drive emulation, including 6507 CPU."
			},

			{ "diskdrive1050turbo", L"1050 Turbo disk drive (full emulation)",
				L"Full 1050 Turbo disk drive emulation, including 6507 CPU."
			},

			{ "diskdrive1050turboii", L"1050 Turbo II disk drive (full emulation)",
				L"Full 1050 Turbo disk drive emulation, including 6507 CPU. This model is also known as Version 3.5 in some places."
			},

			{ "diskdriveindusgt", L"Indus GT disk drive (full emulation)",
				L"Full Indus GT disk drive emulation, including Z80 CPU and RamCharger. This will also work for TOMS Turbo Drive firmware.\n"
					L"The Indus GT supports single density, double density, and enhanced density formats. It does not, however, support double-sided disks."
			},

			{ "diskdrivexf551", L"XF551 disk drive (full emulation)",
				L"Full XF551 disk drive emulation, including 8048 CPU.\n"
					L"The XF551 supports single density, double density, enhanced density, and double-sided double density formats."
			},

			{ "diskdriveatr8000", L"ATR8000 disk drive (full emulation)",
				L"Full ATR8000 disk drive emulation, including Z80 CPU.\n"
					L"The ATR8000 supports up to four disk drives, either 5.25\" or 8\", as well as a built-in parallel "
					L"printer port and RS-232 serial port."
			},

			{ "diskdrivepercom", L"Percom RFD-40S1 disk drive (full emulation)",
				L"Full Percom RFD-40S1 disk drive emulation, including 6809 CPU.\n"
					L"The RFD-40S1 supports up to four disk drives, which may be either single-sided or double-sided "
					L"and 40 or 80 track. Both single-density and double-density are supported by the hardware."
			},
		},
	},
	{
		L"Serial I/O bus devices",
		"sio",
		{
			{ "850", L"850 Interface Module",
				L"A combination device with four RS-232 serial devices and a printer port. The serial ports "
					L"can be used to interface to a modem.\n"
					L"A R: handler is needed to use the serial ports of the 850. Depending on the emulation mode setting, "
					L"this can either be a simulated R: device or a real R: software handler. In Full emulation mode, "
					L"booting the computer without disk drives or with "
					L"the DOS 2.x AUTORUN.SYS will automatically load the R: handler from the 850; otherwise, "
					L"the tools in the Additions disk can be used to load the handler."
			},
			{ "1030", L"1030 Modem",
				L"A 300 baud modem that connects directly to the SIO port.\n"
					L"A T: handler is needed to use the 1030. Depending on the emulation mode setting, the T: "
					L"device can be simulated, or in Full mode the tools from the the Additions disk can be used to load "
					L"a software T: handler."
			},
			{ "midimate", L"MidiMate",
				L"An SIO-based adapter for communicating with MIDI devices. This emulation links the MidiMate to the host "
					L"OS MIDI support. There is no connection to MIDI IN or to the SYNC inputs."
			},
			{ "pclink", L"PCLink",
				L"PC-based file server with SIO bus connection. Requires additional handler software to use, "
					L"included with recent versions of the SpartaDOS X Toolkit disk."
			},
			{ "pocketmodem", L"Pocket Modem",
				L"SIO-based modem capable of 110, 210, 300, and 500 baud operation. Requires an M: handler to use"
					L" (not included)."
			},
			{ "rverter", L"R-Verter",
				L"An adapter cable connecting a regular RS-232 serial device to the computer's SIO port.\n"
				L"An R: handler is needed to use the R-Verter. The R-Verter itself has no bootstrap capability, "
				L"so this handler must be loaded from disk or another source."
			},
			{ "sdrive", L"SDrive",
				L"A hardware disk emulator based on images stored on SD cards.\n"
				L"Currently, only raw sector access is implemented."
			},
			{ "sio2sd", L"SIO2SD",
				L"A hardware disk emulator based on images stored on SD cards.\n"
				L"Currently, only minimal configuration commands are implemented."
			},
			{ "sioclock", L"SIO Real-Time Clock",
				L"SIO-based real-time clock. This device implements APE, AspeQt, and SIO2USB RTC protocols."
			},
			{ "sx212", L"SX212 Modem",
				L"A 1200 baud modem that can be used either by RS-232 or SIO. This emulation is for the "
				L"SIO port connection.\n"
					L"An R: handler is needed to use the SX212. Depending on the emulation mode setting, the R: "
					L"device can be simulated, or in Full mode the tools from the the Additions disk can be used to load "
					L"a software R: handler."
			},
			{ "testsiopoll3", L"SIO Type 3 Poll Test Device",
				L"A fictitious SIO device for testing that implements the XL/XE operating system's boot-time handler "
					L"auto-load protocol (type 3 poll). The handler installs a T: device that simply eats "
					L"anything printed to it."
			},
			{ "testsiopoll4", L"SIO Type 4 Poll Test Device",
				L"A fictitious SIO device for testing that implements the XL/XE operating system's on demand handler "
					L"auto-load protocol (type 4 poll). The handler installs a T: device that simply eats "
					L"anything printed to it."
			},
			{ "testsiohs", L"SIO High Speed Test Device",
				L"A fictitious SIO device for testing that implements an ultra-high speed SIO device with an external clock. "
					L"The device ID is $31 and supports a pseudo-disk protocol."
			}
		}
	},
	{
		L"High-level emulation (HLE) devices",
		"hle",
		{
			{ "hostfs", L"Host device (H:)",
				L"Adds an H: device to CIO to access files on the host computer. This can also be installed as D: for "
					L"programs that do disk access."
			},
			{ "printer", L"Printer (P:)",
				L"Reroutes text printed to P: to the Printer Output window in the emulator. 820 Printer emulation is also provided "
					L"for software that bypasses P:."
			},
		}
	}
};

ATUIDialogDeviceNew::ATUIDialogDeviceNew(const vdfunction<bool(const char *)>& filter)
	: VDResizableDialogFrameW32(IDD_DEVICE_NEW)
	, mFilter(filter)
	, mpDeviceTag(nullptr)
	, mpLastHelpText(nullptr)
{
	mTreeView.OnItemSelectionChanged() += mDelItemSelChanged.Bind(this, &ATUIDialogDeviceNew::OnItemSelectionChanged);
}

bool ATUIDialogDeviceNew::OnLoaded() {
	AddProxy(&mTreeView, IDC_TREE);

	mResizer.Add(IDC_HELP_INFO, mResizer.kML);
	mResizer.Add(IDC_TREE, mResizer.kMC);
	mResizer.Add(IDOK, mResizer.kBR);
	mResizer.Add(IDCANCEL, mResizer.kBR);

	ATUIRestoreWindowPlacement(mhdlg, "Add new device", SW_SHOW, true);

	HWND hwndHelp = GetDlgItem(mhdlg, IDC_HELP_INFO);
	if (hwndHelp) {
		SendMessage(hwndHelp, EM_SETBKGNDCOLOR, FALSE, GetSysColor(COLOR_3DFACE));

		VDStringA s("{\\rtf \\sa90 ");
		AppendRTF(s, L"Select a device to add.");
		s += "}";

		SETTEXTEX stex;
		stex.flags = ST_DEFAULT;
		stex.codepage = CP_ACP;
		SendMessageA(hwndHelp, EM_SETTEXTEX, (WPARAM)&stex, (LPARAM)s.c_str());
	}

	mTreeView.SetRedraw(false);

	for(const auto& category : kCategories) {
		if (mFilter && !mFilter(category.mpTag))
			continue;

		VDUIProxyTreeViewControl::NodeRef catNode = mTreeView.AddItem(mTreeView.kNodeRoot, mTreeView.kNodeLast, category.mpText);

		for(const auto& device : category.mDevices) {
			mTreeView.AddVirtualItem(catNode, mTreeView.kNodeLast, vdmakerefptr(new TreeNode(device)));
		}

		mTreeView.ExpandNode(catNode, true);
	}

	mTreeView.SetRedraw(true);
	SetFocusToControl(IDC_TREE);

	VDDialogFrameW32::OnLoaded();

	return true;
}

void ATUIDialogDeviceNew::OnDestroy() {
	ATUISaveWindowPlacement(mhdlg, "Add new device");
}

void ATUIDialogDeviceNew::OnDataExchange(bool write) {
	if (write) {
		TreeNode *node = static_cast<TreeNode *>(mTreeView.GetSelectedVirtualItem());

		if (!node) {
			FailValidation(IDC_TREE);
			return;
		}

		mpDeviceTag = node->mNode.mpTag;
	}
}

void ATUIDialogDeviceNew::OnItemSelectionChanged(VDUIProxyTreeViewControl *sender, int) {
	TreeNode *node = static_cast<TreeNode *>(mTreeView.GetSelectedVirtualItem());

	EnableControl(IDOK, node != nullptr);

	UpdateHelpText();
}

void ATUIDialogDeviceNew::UpdateHelpText() {
	TreeNode *node = static_cast<TreeNode *>(mTreeView.GetSelectedVirtualItem());
	if (!node) {
		if (mpLastHelpText) {
			mpLastHelpText = nullptr;

			SETTEXTEX stex;
			stex.flags = ST_DEFAULT;
			stex.codepage = CP_ACP;
			SendDlgItemMessageA(mhdlg, IDC_HELP_INFO, EM_SETTEXTEX, (WPARAM)&stex, (LPARAM)"");
		}
		return;
	}

	const TreeEntry& te = node->mNode;

	if (mpLastHelpText == te.mpHelpText)
		return;

	mpLastHelpText = te.mpHelpText;

	VDStringA s;

	s = "{\\rtf \\sa90 {\\b ";
	AppendRTF(s, te.mpText);
	s += "}\\par ";
	AppendRTF(s, te.mpHelpText);
	s += "}";

	SETTEXTEX stex;
	stex.flags = ST_DEFAULT;
	stex.codepage = CP_ACP;
	SendDlgItemMessageA(mhdlg, IDC_HELP_INFO, EM_SETTEXTEX, (WPARAM)&stex, (LPARAM)s.c_str());
}

void ATUIDialogDeviceNew::AppendRTF(VDStringA& rtf, const wchar_t *text) {
	while(const wchar_t c = *text++) {
		if (c == '\n')
			rtf += "\\par ";
		else if (c < 0x20 || c >= 0x7f)
			rtf.append_sprintf("\\u%d?", (sint16)c);	// yes, this needs to be negative at times
		else if (c == '{' || c == '}' || c == '\\')
			rtf.append_sprintf("\\'%02x", c);
		else
			rtf += (char)c;
	}
}

///////////////////////////////////////////////////////////////////////////

class ATUIDialogDevices : public VDResizableDialogFrameW32 {
public:
	ATUIDialogDevices(ATDeviceManager& devMgr);

protected:
	bool OnLoaded();
	void OnDestroy();
	void OnDataExchange(bool write);
	bool OnCommand(uint32 id, uint32 extcode);
	void Add();
	void Remove();
	void RemoveAll();
	void Settings();
	void CreateDeviceNode(VDUIProxyTreeViewControl::NodeRef parentNode, IATDevice *dev);
	void SaveSettings(const char *configTag, const ATPropertySet& props);

	void OnItemSelectionChanged(VDUIProxyTreeViewControl *sender, int idx);
	void OnItemDoubleClicked(VDUIProxyTreeViewControl *sender, bool *handled);
	void OnItemGetDisplayAttributes(VDUIProxyTreeViewControl *sender, VDUIProxyTreeViewControl::GetDispAttrEvent *event);

	ATDeviceManager& mDevMgr;
	VDUIProxyTreeViewControl mTreeView;

	struct DeviceNode : public vdrefcounted<IVDUITreeViewVirtualItem> {
		DeviceNode(IATDevice *dev) : mpDev(dev) {
			ATDeviceInfo info;
			dev->GetDeviceInfo(info);

			mName = info.mpDef->mpName;
			mbHasSettings = info.mpDef->mpConfigTag != nullptr;
		}

		DeviceNode(DeviceNode *parent, const wchar_t *label) {
			mName = label;
			mpParent = parent;
		}

		void *AsInterface(uint32 id) {
			return nullptr;
		}

		virtual void GetText(VDStringW& s) const {
			s = mName;

			if (mpDev) {
				VDStringW buf;
				mpDev->GetSettingsBlurb(buf);

				if (!buf.empty()) {
					s += L" - ";
					s += buf;
				}
			}
		}

		IATDevice *mpDev = nullptr;
		VDStringW mName;
		bool mbHasSettings = false;
		VDUIProxyTreeViewControl::NodeRef mNode = {};
		VDUIProxyTreeViewControl::NodeRef mChildNode = {};
		DeviceNode *mpParent = nullptr;
	};

	VDDelegate mDelSelectionChanged;
	VDDelegate mDelDoubleClicked;
	VDDelegate mDelGetDisplayAttributes;
};

ATUIDialogDevices::ATUIDialogDevices(ATDeviceManager& devMgr)
	: VDResizableDialogFrameW32(IDD_DEVICES)
	, mDevMgr(devMgr)
{
	mTreeView.OnItemSelectionChanged() += mDelSelectionChanged.Bind(this, &ATUIDialogDevices::OnItemSelectionChanged);
	mTreeView.OnItemDoubleClicked() += mDelDoubleClicked.Bind(this, &ATUIDialogDevices::OnItemDoubleClicked);
	mTreeView.OnItemGetDisplayAttributes() += mDelGetDisplayAttributes.Bind(this, &ATUIDialogDevices::OnItemGetDisplayAttributes);
}

bool ATUIDialogDevices::OnLoaded() {
	AddProxy(&mTreeView, IDC_TREE);

	mResizer.Add(IDC_TREE, mResizer.kMC);
	mResizer.Add(IDC_ADD, mResizer.kBL);
	mResizer.Add(IDC_REMOVE, mResizer.kBL);
	mResizer.Add(IDC_REMOVEALL, mResizer.kBL);
	mResizer.Add(IDC_SETTINGS, mResizer.kBL);
	mResizer.Add(IDOK, mResizer.kBR);

	ATUIRestoreWindowPlacement(mhdlg, "Devices", -1, true);

	OnDataExchange(false);

	SetFocusToControl(IDC_LIST);
	return true;
}

void ATUIDialogDevices::OnDestroy() {
	ATUISaveWindowPlacement(mhdlg, "Devices");

	mTreeView.Clear();

	VDDialogFrameW32::OnDestroy();
}

void ATUIDialogDevices::OnDataExchange(bool write) {
	if (write) {
	} else {
		mTreeView.SetRedraw(false);
		mTreeView.Clear();

		auto p = mTreeView.AddItem(mTreeView.kNodeRoot, mTreeView.kNodeLast, L"Computer");

		for(IATDevice *dev : mDevMgr.GetDevices(true, true))
			CreateDeviceNode(p, dev);

		mTreeView.ExpandNode(p, true);

		mTreeView.SetRedraw(true);
	}
}

bool ATUIDialogDevices::OnCommand(uint32 id, uint32 extcode) {
	switch(id) {
		case IDC_ADD:
			Add();
			return true;

		case IDC_REMOVE:
			Remove();
			return true;

		case IDC_REMOVEALL:
			RemoveAll();
			return true;

		case IDC_SETTINGS:
			Settings();
			return true;
	}

	return false;
}

void ATUIDialogDevices::Add() {
	IATDeviceParent *devParent = nullptr;

	auto *p = static_cast<DeviceNode *>(mTreeView.GetSelectedVirtualItem());
	if (p) {
		if (p->mpParent)
			p = p->mpParent;

		devParent = vdpoly_cast<IATDeviceParent *>(p->mpDev);
	}

	static const char *const kBaseCategories[]={
		"pbi",
		"cartridge",
		"internal",
		"joyport",
		"sio",
		"hle"
	};

	vdfunction<bool(const char *)> filter;

	if (devParent) {
		filter = [devParent](const char *tag) -> bool {
			uint32 i=0;

			while(const char *s = devParent->GetSupportedType(i++)) {
				if (!strcmp(tag, s))
					return true;
			}

			return false;
		};
	} else {
		filter = [](const char *tag) -> bool {
			for(const char *s : kBaseCategories) {
				if (!strcmp(s, tag))
					return true;
			}

			return false;
		};
	}

	ATUIDialogDeviceNew dlg(filter);
	if (dlg.ShowDialog(this)) {
		ATPropertySet props;

		{
			VDRegistryAppKey key("Device config history", false);
			VDStringW propstr;
			key.getString(dlg.GetDeviceTag(), propstr);
			if (!propstr.empty())
				mDevMgr.DeserializeProps(props, propstr.c_str());
		}

		ATDeviceConfigureFn cfn = mDevMgr.GetDeviceConfigureFn(dlg.GetDeviceTag());

		if (!cfn || cfn((VDGUIHandle)mhdlg, props)) {
			try {
				IATDevice *dev = mDevMgr.AddDevice(dlg.GetDeviceTag(), props, devParent != nullptr, false);

				try {
					if (devParent)
						devParent->AddChildDevice(dev);

					SaveSettings(dlg.GetDeviceTag(), props);
				} catch(...) {
					mDevMgr.RemoveDevice(dev);
					throw;
				}
			} catch(const MyError& err) {
				err.post(mhdlg, "Error");
				return;
			}

			OnDataExchange(false);
		}
	}
}

void ATUIDialogDevices::Remove() {
	auto *p = static_cast<DeviceNode *>(mTreeView.GetSelectedVirtualItem());

	if (!p || !p->mpDev)
		return;

	IATDevice *dev = p->mpDev;
	vdrefptr<IATDevice> devholder(dev);

	vdfastvector<IATDevice *> childDevices;
	mDevMgr.MarkAndSweep(&dev, 1, childDevices);

	if (!childDevices.empty()) {
		VDStringW msg;

		msg = L"The following attached devices will also be removed:\n\n";

		for(auto it = childDevices.begin(), itEnd = childDevices.end();
			it != itEnd;
			++it)
		{
			ATDeviceInfo info;
			(*it)->GetDeviceInfo(info);

			msg.append_sprintf(L"    %ls\n", info.mpDef->mpName);
		}

		msg += L"\nProceed?";

		if (!Confirm(msg.c_str(), L"Warning"))
			return;
	}
	
	p->mpDev = nullptr;

	IATDeviceParent *parent = dev->GetParent();
	if (parent)
		parent->RemoveChildDevice(dev);

	mDevMgr.RemoveDevice(dev);

	while(!childDevices.empty()) {
		IATDevice *child = childDevices.back();
		childDevices.pop_back();

		mDevMgr.RemoveDevice(child);
	}

	OnDataExchange(false);
}

void ATUIDialogDevices::RemoveAll() {
	if (!Confirm(L"This will remove all devices in the device tree. Are you sure?"))
		return;

	mDevMgr.RemoveAllDevices(false);

	OnDataExchange(false);
}

void ATUIDialogDevices::Settings() {
	auto *p = static_cast<DeviceNode *>(mTreeView.GetSelectedVirtualItem());

	if (!p || !p->mpDev)
		return;

	IATDevice *dev = p->mpDev;
	vdrefptr<IATDevice> devholder(dev);
	if (!dev)
		return;

	ATDeviceInfo info;
	dev->GetDeviceInfo(info);

	if (!info.mpDef->mpConfigTag)
		return;

	const VDStringA configTag(info.mpDef->mpConfigTag);
	ATDeviceConfigureFn fn = mDevMgr.GetDeviceConfigureFn(configTag.c_str());
	if (!fn)
		return;

	ATPropertySet pset;
	dev->GetSettings(pset);
	
	if (fn((VDGUIHandle)mhdlg, pset)) {
		try {
			if (dev->SetSettings(pset)) {
				mDevMgr.IncrementChangeCounter();

				mTreeView.RefreshNode(p->mNode);
			} else {
				vdfastvector<IATDevice *> childDevices;
				mDevMgr.MarkAndSweep(&dev, 1, childDevices);

				IATDeviceParent *parent = dev->GetParent();
				if (parent)
					parent->RemoveChildDevice(dev);

				mDevMgr.RemoveDevice(dev);
				dev = nullptr;
				devholder.clear();

				while(!childDevices.empty()) {
					IATDevice *child = childDevices.back();
					childDevices.pop_back();

					mDevMgr.RemoveDevice(child);
				}

				IATDevice *newChild = mDevMgr.AddDevice(configTag.c_str(), pset, parent != nullptr, false);

				if (parent && newChild)
					parent->AddChildDevice(newChild);

				mDevMgr.MarkAndSweep(NULL, 0, childDevices);

				while(!childDevices.empty()) {
					IATDevice *child = childDevices.back();
					childDevices.pop_back();

					mDevMgr.RemoveDevice(child);
				}

				OnDataExchange(false);
			}

			SaveSettings(configTag.c_str(), pset);
		} catch(const MyError& err) {
			err.post(mhdlg, "Error");

			// We may have lost the previous device, so we need to reinit the tree.
			OnDataExchange(false);
			return;
		}
	}
}

void ATUIDialogDevices::CreateDeviceNode(VDUIProxyTreeViewControl::NodeRef parentNode, IATDevice *dev) {
	auto nodeObject = vdmakerefptr(new DeviceNode(dev));
	auto devnode = mTreeView.AddVirtualItem(parentNode, mTreeView.kNodeLast, nodeObject);

	nodeObject->mNode = devnode;

	IATDeviceParent *devParent = vdpoly_cast<IATDeviceParent *>(dev);
	if (devParent) {
		vdfastvector<IATDevice *> childDevs;

		devParent->GetChildDevices(childDevs);

		if (childDevs.empty()) {
			auto emptyNode = vdmakerefptr(new DeviceNode(nodeObject, L"(No attached devices)"));
			auto emptyTreeNode = mTreeView.AddVirtualItem(devnode, mTreeView.kNodeLast, emptyNode);

			emptyNode->mNode = emptyTreeNode;
			nodeObject->mChildNode = emptyTreeNode;
		} else {
			for(auto it = childDevs.begin(), itEnd = childDevs.end();
				it != itEnd;
				++it)
			{
				CreateDeviceNode(devnode, *it);
			}
		}
	}

	mTreeView.ExpandNode(devnode, true);
}

void ATUIDialogDevices::SaveSettings(const char *configTag, const ATPropertySet& props) {
	VDRegistryAppKey key("Device config history", true);
	if (props.IsEmpty()) {
		key.removeValue(configTag);
	} else {
		VDStringW s;
		mDevMgr.SerializeProps(props, s);
		key.setString(configTag, s.c_str());
	}
}

void ATUIDialogDevices::OnItemSelectionChanged(VDUIProxyTreeViewControl *sender, int idx) {
	const auto *node = static_cast<DeviceNode *>(mTreeView.GetSelectedVirtualItem());
	bool enabled = node && node->mbHasSettings;

	EnableControl(IDC_SETTINGS, enabled);
	EnableControl(IDC_REMOVE, node != nullptr);
}

void ATUIDialogDevices::OnItemDoubleClicked(VDUIProxyTreeViewControl *sender, bool *handled) {
	Settings();

	*handled = true;
}

void ATUIDialogDevices::OnItemGetDisplayAttributes(VDUIProxyTreeViewControl *sender, VDUIProxyTreeViewControl::GetDispAttrEvent *event) {
	const auto *node = static_cast<DeviceNode *>(event->mpItem);

	if (node && node->mpParent)
		event->mbIsMuted = true;
}

///////////////////////////////////////////////////////////////////////////

void ATUIShowDialogDevices(VDGUIHandle hParent, ATDeviceManager& devMgr) {
	ATUIDialogDevices dlg(devMgr);

	dlg.ShowDialog(hParent);
}
