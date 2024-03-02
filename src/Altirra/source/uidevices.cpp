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

#include "stdafx.h"
#include <windows.h>
#include <richedit.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/math.h>
#include <vd2/system/registry.h>
#include <vd2/Dita/services.h>
#include <at/atcore/device.h>
#include <at/atcore/propertyset.h>
#include <at/atui/dialog.h>
#include "resource.h"
#include "devicemanager.h"

///////////////////////////////////////////////////////////////////////////

class ATUIDialogDeviceNew : public VDDialogFrameW32 {
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
				"mio", L"MIO",
				L"The ICD Multi-I/O (MIO) is a PBI device that adds SCSI hard disk, RAM disk, "
					L"parallel printer port, and RS-232 serial port functionality.\n"
					L"The MIO firmware's built-in menu is accessed with Select+Reset."
			}
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
			{ "rtime8", L"R-Time 8",
				L"A simple cartridge that provides a real-time clock. It was specifically designed "
					L"to work in the pass-through port of the SpartaDOS X cartridge. Separate software "
					L"is required to provide the Z: device handler."
			},
			{ "slightsid", L"SlightSID",
				L"A cartridge-shaped adapter for the C64's SID sound chip."
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
		}
	},
	{
		L"Controller port devices",
		"joyport",
		{
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
			{ "sx212", L"SX212 Modem",
				L"A 1200 baud modem that can be used either by RS-232 or SIO. This emulation is for the "
				L"SIO port connection.\n"
					L"An R: handler is needed to use the SX212. Depending on the emulation mode setting, the R: "
					L"device can be simulated, or in Full mode the tools from the the Additions disk can be used to load "
					L"a software R: handler."
			},
			{ "midimate", L"MidiMate",
				L"An SIO-based adapter for communicating with MIDI devices. This emulation links the MidiMate to the host "
					L"OS MIDI support. There is no connection to MIDI IN or to the SYNC inputs."
			},
			{ "pclink", L"PCLink",
				L"PC-based file server with SIO bus connection. Requires additional handler software to use, "
					L"included with recent versions of the SpartaDOS X Toolkit disk."
			},
			{ "sioclock", L"SIO Real-Time Clock",
				L"SIO-based real-time clock. This device implements APE, AspeQt, and SIO2USB RTC protocols."
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
	: VDDialogFrameW32(IDD_DEVICE_NEW)
	, mFilter(filter)
	, mpDeviceTag(nullptr)
	, mpLastHelpText(nullptr)
{
	mTreeView.OnItemSelectionChanged() += mDelItemSelChanged.Bind(this, &ATUIDialogDeviceNew::OnItemSelectionChanged);
}

bool ATUIDialogDeviceNew::OnLoaded() {
	AddProxy(&mTreeView, IDC_TREE);

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
	const VDStringA& texta = VDTextWToA(text);
	for(VDStringA::const_iterator it = texta.begin(), itEnd = texta.end();
		it != itEnd;
		++it)
	{
		const unsigned char c = *it;

		if (c < 0x20 || c > 0x80 || c == '{' || c == '}' || c == '\\')
			rtf.append_sprintf("\\'%02x", c);
		else if (c == '\n')
			rtf += "\\par ";
		else
			rtf += c;
	}
}

///////////////////////////////////////////////////////////////////////////

class ATUIDialogDevices : public VDDialogFrameW32 {
public:
	ATUIDialogDevices(ATDeviceManager& devMgr);

protected:
	bool OnLoaded();
	void OnDestroy();
	void OnDataExchange(bool write);
	bool OnCommand(uint32 id, uint32 extcode);
	void Add();
	void Remove();
	void Settings();
	void CreateDeviceNode(VDUIProxyTreeViewControl::NodeRef parentNode, IATDevice *dev);
	void SaveSettings(const char *configTag, const ATPropertySet& props);

	ATDeviceManager& mDevMgr;
	VDUIProxyTreeViewControl mTreeView;

	struct DeviceNode : public vdrefcounted<IVDUITreeViewVirtualItem> {
		DeviceNode(IATDevice *dev) : mpDev(dev) {
			dev->GetDeviceInfo(mInfo);
		}

		void *AsInterface(uint32 id) {
			return nullptr;
		}

		virtual void GetText(VDStringW& s) const {
			s = mInfo.mName;
		}

		IATDevice *mpDev;
		ATDeviceInfo mInfo;
	};
};

ATUIDialogDevices::ATUIDialogDevices(ATDeviceManager& devMgr)
	: VDDialogFrameW32(IDD_DEVICES)
	, mDevMgr(devMgr)
{
}

bool ATUIDialogDevices::OnLoaded() {
	AddProxy(&mTreeView, IDC_TREE);

	OnDataExchange(false);

	SetFocusToControl(IDC_LIST);
	return true;
}

void ATUIDialogDevices::OnDestroy() {
	mTreeView.Clear();

	VDDialogFrameW32::OnDestroy();
}

void ATUIDialogDevices::OnDataExchange(bool write) {
	if (write) {
	} else {
		mTreeView.SetRedraw(false);
		mTreeView.Clear();

		auto p = mTreeView.AddItem(mTreeView.kNodeRoot, mTreeView.kNodeLast, L"Computer");

		mDevMgr.ForEachDevice(true, [this, p](IATDevice *dev) { CreateDeviceNode(p, dev); });

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

		case IDC_SETTINGS:
			Settings();
			return true;
	}

	return false;
}

void ATUIDialogDevices::Add() {
	IATDeviceParent *devParent = nullptr;

	auto *p = static_cast<DeviceNode *>(mTreeView.GetSelectedVirtualItem());
	if (p)
		devParent = vdpoly_cast<IATDeviceParent *>(p->mpDev);

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
				IATDevice *dev = mDevMgr.AddDevice(dlg.GetDeviceTag(), props, devParent != nullptr);

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

			msg.append_sprintf(L"    %ls\n", info.mName.c_str());
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

void ATUIDialogDevices::Settings() {
	auto *p = static_cast<DeviceNode *>(mTreeView.GetSelectedVirtualItem());

	if (!p)
		return;

	IATDevice *dev = p->mpDev;
	vdrefptr<IATDevice> devholder(dev);
	if (!dev)
		return;

	ATDeviceInfo info;
	dev->GetDeviceInfo(info);

	const char *configTag = !info.mConfigTag.empty() ? info.mConfigTag.c_str() : info.mTag.c_str();
	ATDeviceConfigureFn fn = mDevMgr.GetDeviceConfigureFn(configTag);
	if (!fn)
		return;

	ATPropertySet pset;
	dev->GetSettings(pset);
	
	if (fn((VDGUIHandle)mhdlg, pset)) {
		SaveSettings(configTag, pset);

		if (!dev->SetSettings(pset)) {
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

			IATDevice *newChild = mDevMgr.AddDevice(info.mConfigTag.c_str(), pset, parent != nullptr);

			if (newChild)
				parent->AddChildDevice(newChild);

			childDevices.clear();
			mDevMgr.MarkAndSweep(NULL, 0, childDevices);

			while(!childDevices.empty()) {
				IATDevice *child = childDevices.back();
				childDevices.pop_back();

				mDevMgr.RemoveDevice(child);
			}

			OnDataExchange(false);
		}
	}
}

void ATUIDialogDevices::CreateDeviceNode(VDUIProxyTreeViewControl::NodeRef parentNode, IATDevice *dev) {
	auto devnode = mTreeView.AddVirtualItem(parentNode, mTreeView.kNodeLast, vdmakerefptr(new DeviceNode(dev)));

	IATDeviceParent *devParent = vdpoly_cast<IATDeviceParent *>(dev);
	if (devParent) {
		vdfastvector<IATDevice *> childDevs;

		devParent->GetChildDevices(childDevs);

		for(auto it = childDevs.begin(), itEnd = childDevs.end();
			it != itEnd;
			++it)
		{
			CreateDeviceNode(devnode, *it);
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

///////////////////////////////////////////////////////////////////////////

void ATUIShowDialogDevices(VDGUIHandle hParent, ATDeviceManager& devMgr) {
	ATUIDialogDevices dlg(devMgr);

	dlg.ShowDialog(hParent);
}
