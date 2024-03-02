//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2015 Avery Lee
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
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/strutil.h>
#include <vd2/Dita/accel.h>
#include <vd2/Dita/services.h>
#include <vd2/vdjson/jsonreader.h>
#include <vd2/vdjson/jsonwriter.h>
#include <vd2/vdjson/jsonvalue.h>
#include <at/atnativeui/dialog.h>
#include <at/atnativeui/hotkeyexcontrol.h>
#include <at/atnativeui/uiproxies.h>
#include "resource.h"
#include "uikeyboard.h"

class ATUIDialogKeyboardCustomize : public VDDialogFrameW32 {
public:
	ATUIDialogKeyboardCustomize();

protected:
	struct ScanCodeEntry {
		uint32 mScanCode;
		const wchar_t *mpLabel;
	};

	class MappingEntry;

	bool OnLoaded();
	void OnDataExchange(bool write);

	void OnScanCodeSelChanged(VDUIProxyListBoxControl *, int idx);
	void OnBindingSelChanged(VDUIProxyListView *, int idx);
	void OnHotKeyChanged(IVDUIHotKeyExControl *, VDUIAccelerator);

	void OnAddClicked();
	void OnRemoveClicked();
	void OnImportClicked();
	void OnExportClicked();
	void OnClearClicked();

	void SetCookedMode(bool enabled);
	void ReloadEmuKeyList();
	void SortMappings();
	void RebuildMappingList();

	bool CompareMappingOrder(uint32 a, uint32 b) const;
	uint32 GetCurrentAccelMapping() const;

	static uint32 GetMappingForAccel(const VDUIAccelerator& accel);
	static VDUIAccelerator GetAccelForMapping(uint32 mapping);

	bool IsScanCodeMapped(uint32 scanCode) const;
	void UpdateScanCodeIsMapped(uint32 scanCode);

	uint32 mSelChangeRecursionLock = 0;

	vdrefptr<IVDUIHotKeyExControl> mpHotKeyControl;
	vdfastvector<uint32> mMappings;
	vdfastvector<uint32> mScanCodeFilteredList;

	VDUIProxyEditControl mSearchControl;
	VDUIProxyListBoxControl mScanCodeList;
	VDUIProxyListView mBindingListControl;
	VDUIProxyButtonControl mCharModeControl;
	VDUIProxyButtonControl mVKModeControl;
	VDUIProxyButtonControl mAddControl;
	VDUIProxyButtonControl mRemoveControl;
	VDUIProxyButtonControl mImportControl;
	VDUIProxyButtonControl mExportControl;
	VDUIProxyButtonControl mClearButton;

	VDDelegate mDelScanCodeSelChanged;
	VDDelegate mDelBindingSelChanged;
	VDDelegate mDelHotKeyChanged;

	uint32 mScanCodeToEntry[0x200];

	static const ScanCodeEntry kScanCodeTable[];
};

const ATUIDialogKeyboardCustomize::ScanCodeEntry ATUIDialogKeyboardCustomize::kScanCodeTable[]={
	{ kATUIKeyScanCode_Start, L"Start" },
	{ kATUIKeyScanCode_Select, L"Select" },
	{ kATUIKeyScanCode_Option, L"Option" },
	{ kATUIKeyScanCode_Break, L"Break" },

	{ 0x3F, L"A" },
	{ 0x15, L"B" },
	{ 0x12, L"C" },
	{ 0x3A, L"D" },
	{ 0x2A, L"E" },
	{ 0x38, L"F" },
	{ 0x3D, L"G" },
	{ 0x39, L"H" },
	{ 0x0D, L"I" },
	{ 0x01, L"J" },
	{ 0x05, L"K" },
	{ 0x00, L"L" },
	{ 0x25, L"M" },
	{ 0x23, L"N" },
	{ 0x08, L"O" },
	{ 0x0A, L"P" },
	{ 0x2F, L"Q" },
	{ 0x28, L"R" },
	{ 0x3E, L"S" },
	{ 0x2D, L"T" },
	{ 0x0B, L"U" },
	{ 0x10, L"V" },
	{ 0x2E, L"W" },
	{ 0x16, L"X" },
	{ 0x2B, L"Y" },
	{ 0x17, L"Z" },
	{ 0x1F, L"1" },
	{ 0x1E, L"2" },
	{ 0x1A, L"3" },
	{ 0x18, L"4" },
	{ 0x1D, L"5" },
	{ 0x1B, L"6" },
	{ 0x33, L"7" },
	{ 0x35, L"8" },
	{ 0x30, L"9" },
	{ 0x32, L"0" },
	{ 0x03, L"F1" },
	{ 0x04, L"F2" },
	{ 0x13, L"F3" },
	{ 0x14, L"F4" },
	{ 0x22, L"." },
	{ 0x20, L"," },
	{ 0x02, L";" },
	{ 0x06, L"+" },
	{ 0x07, L"*" },
	{ 0x0E, L"-" },
	{ 0x0F, L"=" },
	{ 0x26, L"/" },
	{ 0x36, L"<" },
	{ 0x37, L">" },
	{ 0x21, L"Space" },
	{ 0x0C, L"Enter" },
	{ 0x34, L"Backspace" },
	{ 0x1C, L"Esc" },
	{ 0x2C, L"Tab" },
	{ 0x27, L"Invert (Fuji)" },
	{ 0x11, L"Help" },
	{ 0x3C, L"Caps" },

	{ 0x7F, L"Shift+A" },
	{ 0x55, L"Shift+B" },
	{ 0x52, L"Shift+C" },
	{ 0x7A, L"Shift+D" },
	{ 0x6A, L"Shift+E" },
	{ 0x78, L"Shift+F" },
	{ 0x7D, L"Shift+G" },
	{ 0x79, L"Shift+H" },
	{ 0x4D, L"Shift+I" },
	{ 0x41, L"Shift+J" },
	{ 0x45, L"Shift+K" },
	{ 0x40, L"Shift+L" },
	{ 0x65, L"Shift+M" },
	{ 0x63, L"Shift+N" },
	{ 0x48, L"Shift+O" },
	{ 0x4A, L"Shift+P" },
	{ 0x6F, L"Shift+Q" },
	{ 0x68, L"Shift+R" },
	{ 0x7E, L"Shift+S" },
	{ 0x6D, L"Shift+T" },
	{ 0x4B, L"Shift+U" },
	{ 0x50, L"Shift+V" },
	{ 0x6E, L"Shift+W" },
	{ 0x56, L"Shift+X" },
	{ 0x6B, L"Shift+Y" },
	{ 0x57, L"Shift+Z" },

	{ 0x5F, L"Shift+1 (!)" },
	{ 0x5E, L"Shift+2 (\")" },
	{ 0x5A, L"Shift+3 (#)" },
	{ 0x58, L"Shift+4 ($)" },
	{ 0x5D, L"Shift+5 (%)" },
	{ 0x5B, L"Shift+6 (&)" },
	{ 0x73, L"Shift+7 (')" },
	{ 0x75, L"Shift+8 (@)" },
	{ 0x70, L"Shift+9 (()" },
	{ 0x72, L"Shift+0 ())" },

	{ 0x43, L"Shift+F1" },
	{ 0x44, L"Shift+F2" },
	{ 0x53, L"Shift+F3" },
	{ 0x54, L"Shift+F4" },

	{ 0x60, L"Shift+, ([)" },
	{ 0x62, L"Shift+. (])" },
	{ 0x42, L"Shift+; (:)" },
	{ 0x46, L"Shift++ (\\)" },
	{ 0x47, L"Shift+* (^)" },
	{ 0x4E, L"Shift+- (_)" },
	{ 0x4F, L"Shift+= (|)" },
	{ 0x66, L"Shift+/ (?)" },
	{ 0x76, L"Shift+< (Clear)" },
	{ 0x77, L"Shift+> (Insert)" },
	{ 0x61, L"Shift+Space" },
	{ 0x4C, L"Shift+Enter" },
	{ 0x74, L"Shift+Backspace" },
	{ 0x5C, L"Shift+Esc" },
	{ 0x6C, L"Shift+Tab" },
	{ 0x67, L"Shift+Invert (Fuji)" },
	{ 0x51, L"Shift+Help" },
	{ 0x7C, L"Shift+Caps" },

	{ 0xBF, L"Ctrl+A" },
	{ 0x95, L"Ctrl+B" },
	{ 0x92, L"Ctrl+C" },
	{ 0xBA, L"Ctrl+D" },
	{ 0xAA, L"Ctrl+E" },
	{ 0xB8, L"Ctrl+F" },
	{ 0xBD, L"Ctrl+G" },
	{ 0xB9, L"Ctrl+H" },
	{ 0x8D, L"Ctrl+I" },
	{ 0x81, L"Ctrl+J" },
	{ 0x85, L"Ctrl+K" },
	{ 0x80, L"Ctrl+L" },
	{ 0xA5, L"Ctrl+M" },
	{ 0xA3, L"Ctrl+N" },
	{ 0x88, L"Ctrl+O" },
	{ 0x8A, L"Ctrl+P" },
	{ 0xAF, L"Ctrl+Q" },
	{ 0xA8, L"Ctrl+R" },
	{ 0xBE, L"Ctrl+S" },
	{ 0xAD, L"Ctrl+T" },
	{ 0x8B, L"Ctrl+U" },
	{ 0x90, L"Ctrl+V" },
	{ 0xAE, L"Ctrl+W" },
	{ 0x96, L"Ctrl+X" },
	{ 0xAB, L"Ctrl+Y" },
	{ 0x97, L"Ctrl+Z" },

	{ 0x9F, L"Ctrl+1" },
	{ 0x9E, L"Ctrl+2" },
	{ 0x9A, L"Ctrl+3" },
	{ 0x98, L"Ctrl+4" },
	{ 0x9D, L"Ctrl+5" },
	{ 0x9B, L"Ctrl+6" },
	{ 0xB3, L"Ctrl+7" },
	{ 0xB5, L"Ctrl+8" },
	{ 0xB0, L"Ctrl+9" },
	{ 0xB2, L"Ctrl+0" },

	{ 0x83, L"Ctrl+F1" },
	{ 0x84, L"Ctrl+F2" },
	{ 0x93, L"Ctrl+F3" },
	{ 0x94, L"Ctrl+F4" },

	{ 0xA0, L"Ctrl+," },
	{ 0xA2, L"Ctrl+." },
	{ 0x82, L"Ctrl+;" },
	{ 0x86, L"Ctrl++ (Left)" },
	{ 0x87, L"Ctrl+* (Right)" },
	{ 0x8E, L"Ctrl+- (Up)" },
	{ 0x8F, L"Ctrl+= (Down)" },
	{ 0xA6, L"Ctrl+/" },
	{ 0xB6, L"Ctrl+<" },
	{ 0xB7, L"Ctrl+>" },
	{ 0xA1, L"Ctrl+Space" },
	{ 0x8C, L"Ctrl+Enter" },
	{ 0xB4, L"Ctrl+Backspace" },
	{ 0x9C, L"Ctrl+Esc" },
	{ 0xAC, L"Ctrl+Tab" },
	{ 0xA7, L"Ctrl+Invert (Fuji)" },
	{ 0x91, L"Ctrl+Help" },
	{ 0xBC, L"Ctrl+Caps" },

	{ 0xFF, L"Ctrl+Shift+A" },
	{ 0xD5, L"Ctrl+Shift+B" },
	{ 0xD2, L"Ctrl+Shift+C" },
	{ 0xFA, L"Ctrl+Shift+D" },
	{ 0xEA, L"Ctrl+Shift+E" },
	{ 0xF8, L"Ctrl+Shift+F" },
	{ 0xFD, L"Ctrl+Shift+G" },
	{ 0xF9, L"Ctrl+Shift+H" },
	{ 0xCD, L"Ctrl+Shift+I" },
	{ 0xC1, L"Ctrl+Shift+J" },
	{ 0xC5, L"Ctrl+Shift+K" },
	{ 0xC0, L"Ctrl+Shift+L" },
	{ 0xE5, L"Ctrl+Shift+M" },
	{ 0xE3, L"Ctrl+Shift+N" },
	{ 0xC8, L"Ctrl+Shift+O" },
	{ 0xCA, L"Ctrl+Shift+P" },
	{ 0xEF, L"Ctrl+Shift+Q" },
	{ 0xE8, L"Ctrl+Shift+R" },
	{ 0xFE, L"Ctrl+Shift+S" },
	{ 0xED, L"Ctrl+Shift+T" },
	{ 0xCB, L"Ctrl+Shift+U" },
	{ 0xD0, L"Ctrl+Shift+V" },
	{ 0xEE, L"Ctrl+Shift+W" },
	{ 0xD6, L"Ctrl+Shift+X" },
	{ 0xEB, L"Ctrl+Shift+Y" },
	{ 0xD7, L"Ctrl+Shift+Z" },
	{ 0xDF, L"Ctrl+Shift+1" },
	{ 0xDE, L"Ctrl+Shift+2" },
	{ 0xDA, L"Ctrl+Shift+3" },
	{ 0xD8, L"Ctrl+Shift+4" },
	{ 0xDD, L"Ctrl+Shift+5" },
	{ 0xDB, L"Ctrl+Shift+6" },
	{ 0xF3, L"Ctrl+Shift+7" },
	{ 0xF5, L"Ctrl+Shift+8" },
	{ 0xF0, L"Ctrl+Shift+9" },
	{ 0xF2, L"Ctrl+Shift+0" },
	{ 0xC3, L"Ctrl+Shift+F1" },
	{ 0xC4, L"Ctrl+Shift+F2" },
	{ 0xD3, L"Ctrl+Shift+F3" },
	{ 0xD4, L"Ctrl+Shift+F4" },
	{ 0xE0, L"Ctrl+Shift+," },
	{ 0xE2, L"Ctrl+Shift+." },
	{ 0xC2, L"Ctrl+Shift+;" },
	{ 0xC6, L"Ctrl+Shift++" },
	{ 0xC7, L"Ctrl+Shift+*" },
	{ 0xCE, L"Ctrl+Shift+-" },
	{ 0xCF, L"Ctrl+Shift+=" },
	{ 0xE6, L"Ctrl+Shift+/" },
	{ 0xF6, L"Ctrl+Shift+<" },
	{ 0xF7, L"Ctrl+Shift+>" },
	{ 0xE1, L"Ctrl+Shift+Space" },
	{ 0xCC, L"Ctrl+Shift+Enter" },
	{ 0xF4, L"Ctrl+Shift+Backspace" },
	{ 0xDC, L"Ctrl+Shift+Esc" },
	{ 0xEC, L"Ctrl+Shift+Tab" },
	{ 0xE7, L"Ctrl+Shift+Invert (Fuji)" },
	{ 0xD1, L"Ctrl+Help" },
	{ 0xFC, L"Ctrl+Shift+Caps" },
};

///////////////////////////////////////////////////////////////////////////

class ATUIDialogKeyboardCustomize::MappingEntry final : public vdrefcounted<IVDUIListViewVirtualItem> {
public:
	MappingEntry(uint32 scanCodeEntryIndex, uint32 mapping);

	uint32 GetMapping() const { return mMapping; }
	void GetText(int subItem, VDStringW& s) const override;

private:
	uint32 mScanCodeEntryIndex;
	uint32 mMapping;
};

ATUIDialogKeyboardCustomize::MappingEntry::MappingEntry(uint32 scanCodeEntryIndex, uint32 mapping)
	: mScanCodeEntryIndex(scanCodeEntryIndex)
	, mMapping(mapping)
{
}

void ATUIDialogKeyboardCustomize::MappingEntry::GetText(int subItem, VDStringW& s) const {
	if (subItem == 0) {
		if (mScanCodeEntryIndex < vdcountof(kScanCodeTable))
			s = kScanCodeTable[mScanCodeEntryIndex].mpLabel;
		else
			s = L"?";
	} else {
		VDUIAccelerator accel = GetAccelForMapping(mMapping);

		VDUIGetAcceleratorString(accel, s);

		VDASSERT(!wcsstr(s.c_str(), L"Num"));
	}
}

///////////////////////////////////////////////////////////////////////////

ATUIDialogKeyboardCustomize::ATUIDialogKeyboardCustomize()
	: VDDialogFrameW32(IDD_KEYBOARD_CUSTOMIZE)
{
	for(auto& e : mScanCodeToEntry)
		e = (uint8)vdcountof(kScanCodeTable);

	for(size_t i=0; i<vdcountof(kScanCodeTable); ++i) {
		mScanCodeToEntry[kScanCodeTable[i].mScanCode] = (uint8)i;
	}

	mScanCodeList.OnSelectionChanged() += mDelScanCodeSelChanged.Bind(this, &ATUIDialogKeyboardCustomize::OnScanCodeSelChanged);
	mBindingListControl.OnItemSelectionChanged() += mDelBindingSelChanged.Bind(this, &ATUIDialogKeyboardCustomize::OnBindingSelChanged);
	mSearchControl.SetOnTextChanged([this](VDUIProxyEditControl*) { ReloadEmuKeyList(); });
	mCharModeControl.SetOnClicked([this](VDUIProxyButtonControl*) { SetCookedMode(true); });
	mVKModeControl.SetOnClicked([this](VDUIProxyButtonControl*) { SetCookedMode(false); });
	mAddControl.SetOnClicked([this](VDUIProxyButtonControl*) { OnAddClicked(); });
	mRemoveControl.SetOnClicked([this](VDUIProxyButtonControl*) { OnRemoveClicked(); });
	mImportControl.SetOnClicked([this](VDUIProxyButtonControl*) { OnImportClicked(); });
	mExportControl.SetOnClicked([this](VDUIProxyButtonControl*) { OnExportClicked(); });
	mClearButton.SetOnClicked([this](VDUIProxyButtonControl*) { OnClearClicked(); });
}

bool ATUIDialogKeyboardCustomize::OnLoaded() {
	AddProxy(&mScanCodeList, IDC_EMUKEY_LIST);
	AddProxy(&mSearchControl, IDC_SEARCH);
	AddProxy(&mBindingListControl, IDC_HOSTKEY_BINDINGS);
	AddProxy(&mCharModeControl, IDC_KEYMODE_CHAR);
	AddProxy(&mVKModeControl, IDC_KEYMODE_VK);
	AddProxy(&mAddControl, IDC_ADD);
	AddProxy(&mRemoveControl, IDC_REMOVE);
	AddProxy(&mImportControl, IDC_IMPORT);
	AddProxy(&mExportControl, IDC_EXPORT);
	AddProxy(&mClearButton, IDC_CLEAR);

	mBindingListControl.InsertColumn(0, L"Emulation Key", 0);
	mBindingListControl.InsertColumn(1, L"Host Key", 0);

	mBindingListControl.SetFullRowSelectEnabled(true);

	mVKModeControl.SetChecked(true);

	auto h = GetControl(IDC_HOTKEY);
	if (h) {
		auto *p = VDGetUIHotKeyExControl((VDGUIHandle)h);

		if (p) {
			mpHotKeyControl = p;
			mpHotKeyControl->OnChange() += mDelHotKeyChanged.Bind(this, &ATUIDialogKeyboardCustomize::OnHotKeyChanged);
		}
	}

	return VDDialogFrameW32::OnLoaded();
}

void ATUIDialogKeyboardCustomize::OnDataExchange(bool write) {
	if (write) {
		ATUISetCustomKeyMap(mMappings.data(), mMappings.size());
	} else {
		mMappings.clear();
		ATUIGetCustomKeyMap(mMappings);

		SortMappings();
		RebuildMappingList();
		ReloadEmuKeyList();
	}
}

void ATUIDialogKeyboardCustomize::OnScanCodeSelChanged(VDUIProxyListBoxControl *, int idx) {
	if (mSelChangeRecursionLock)
		return;

	if ((unsigned)idx >= mScanCodeFilteredList.size())
		return;

	const uint8 scanCode = kScanCodeTable[mScanCodeFilteredList[idx]].mScanCode;
	auto it = std::find_if(mMappings.begin(), mMappings.end(), 
		[=](uint32 mapping) { return (mapping & 0x1FF) == scanCode; });

	if (it != mMappings.end()) {
		const int bindingIdx = (int)(it - mMappings.begin());

		++mSelChangeRecursionLock;
		mBindingListControl.SetSelectedIndex(bindingIdx);
		mBindingListControl.EnsureItemVisible(bindingIdx);
		--mSelChangeRecursionLock;
	}
}

void ATUIDialogKeyboardCustomize::OnBindingSelChanged(VDUIProxyListView *, int idx) {
	if (mSelChangeRecursionLock)
		return;

	auto *p = static_cast<MappingEntry *>(mBindingListControl.GetSelectedVirtualItem());

	if (!p)
		return;

	++mSelChangeRecursionLock;

	const uint32 mapping = p->GetMapping();
	const uint8 scanCodeIndex = mScanCodeToEntry[mapping & 0x1FF];

	if (scanCodeIndex < vdcountof(kScanCodeTable)) {
		auto it = std::lower_bound(mScanCodeFilteredList.begin(), mScanCodeFilteredList.end(), scanCodeIndex);

		if (it != mScanCodeFilteredList.end() && *it == scanCodeIndex)
			mScanCodeList.SetSelection((int)(it - mScanCodeFilteredList.begin()));
	}

	if (mapping & kATUIKeyboardMappingModifier_Cooked) {
		mCharModeControl.SetChecked(true);
		mVKModeControl.SetChecked(false);
	} else {
		mCharModeControl.SetChecked(false);
		mVKModeControl.SetChecked(true);
	}

	if (mpHotKeyControl) {
		VDUIAccelerator accel = GetAccelForMapping(mapping);

		mpHotKeyControl->SetAccelerator(accel);
	}

	--mSelChangeRecursionLock;
}

void ATUIDialogKeyboardCustomize::OnHotKeyChanged(IVDUIHotKeyExControl *, VDUIAccelerator) {
	if (mSelChangeRecursionLock)
		return;

	uint32 mappingBase = GetCurrentAccelMapping();
	if (!mappingBase)
		return;

	// try to find a mapping
	for(int i=0; i<2; ++i) {
		auto it = std::find_if(mMappings.begin(), mMappings.end(), [=](uint32 mapping) { return (mapping & 0xFFFFFE00) == mappingBase; });
		if (it != mMappings.end()) {
			++mSelChangeRecursionLock;
			int selIdx = (int)(it - mMappings.begin());
			mBindingListControl.SetSelectedIndex(selIdx);
			mBindingListControl.EnsureItemVisible(selIdx);
			--mSelChangeRecursionLock;
		}

		// couldn't find mapping of current type -- try flipping the mapping
		VDUIAccelerator accel;
		if (mappingBase & kATUIKeyboardMappingModifier_Cooked) {
			if (!VDUIGetVkAcceleratorForChar(accel, (wchar_t)((mappingBase >> 9) & 0xFFFF)))
				break;
		} else {
			accel = GetAccelForMapping(mappingBase);
			if (!VDUIGetCharAcceleratorForVk(accel))
				break;
		}

		mappingBase = GetMappingForAccel(accel);
		if (!mappingBase)
			break;
	}
}

void ATUIDialogKeyboardCustomize::OnAddClicked() {
	int scanCodeSelIndex = mScanCodeList.GetSelection();
	if (scanCodeSelIndex < 0)
		return;

	int scanCodeIndex = (int)mScanCodeList.GetItemData(scanCodeSelIndex);
	if ((unsigned)scanCodeIndex >= vdcountof(kScanCodeTable))
		return;

	uint32 mapping = GetCurrentAccelMapping();
	if (!mapping)
		return;

	mapping += kScanCodeTable[scanCodeIndex].mScanCode;

	// delete all items that have either the same source key
	size_t n = mMappings.size();
	for(size_t i = n; i; --i) {
		uint32 existingMapping = mMappings[i - 1];
		uint32 diff = existingMapping ^ mapping;

		if (!(diff & 0xFFFFFE00)) {
			mMappings.erase(mMappings.begin() + i - 1);
			--n;

			mBindingListControl.DeleteItem((int)(i - 1));
		}
	}

	auto it2 = std::lower_bound(mMappings.begin(), mMappings.end(), mapping, [this](uint32 a, uint32 b) { return CompareMappingOrder(a, b); });

	int insertIdx = (int)(it2 - mMappings.begin());
	mMappings.insert(it2, mapping);

	++mSelChangeRecursionLock;
	mBindingListControl.InsertVirtualItem(insertIdx, vdmakerefptr(new MappingEntry((uint32)scanCodeIndex, mapping)));
	mBindingListControl.SetSelectedIndex(insertIdx);
	mBindingListControl.EnsureItemVisible(insertIdx);
	mBindingListControl.AutoSizeColumns(true);
	--mSelChangeRecursionLock;
}

void ATUIDialogKeyboardCustomize::OnRemoveClicked() {
	auto *p = static_cast<MappingEntry *>(mBindingListControl.GetSelectedVirtualItem());

	if (!p)
		return;

	uint32 mapping = p->GetMapping();

	auto it = std::find(mMappings.begin(), mMappings.end(), mapping);
	if (it != mMappings.end())
		mMappings.erase(it);

	int idx = mBindingListControl.GetSelectedIndex();

	++mSelChangeRecursionLock;
	mBindingListControl.DeleteItem(idx);
	mBindingListControl.SetSelectedIndex(idx);
	mBindingListControl.EnsureItemVisible(idx);
	--mSelChangeRecursionLock;

	UpdateScanCodeIsMapped((uint8)(mapping & 0xFF));
}

void ATUIDialogKeyboardCustomize::OnImportClicked() {
	const auto& s = VDGetLoadFileName('kmap', (VDGUIHandle)mhdlg, L"Load custom keyboard map", L"Altirra keyboard map (*.atkmap)\0*.atkmap\0All files\0*.*\0", L"atkmap");
	if (s.empty())
		return;

	struct InvalidKeyboardMapFileException {};
	try {
		VDFile f(s.c_str());
		sint64 size = f.size();

		if (size > 0x1000000)
			throw InvalidKeyboardMapFileException();

		vdblock<char> buf((uint32)size);
		f.read(buf.data(), (long)size);
		f.close();

		VDJSONDocument doc;
		{
			VDJSONReader reader;
			if (!reader.Parse(buf.data(), buf.size(), doc))
				throw InvalidKeyboardMapFileException();
		}

		const auto& rootNode = doc.Root();
		if (!rootNode.IsObject())
			throw InvalidKeyboardMapFileException();

		if (wcscmp(rootNode[".type"].AsString(), L"keymap"))
			throw InvalidKeyboardMapFileException();

		const auto& mappingsNode = rootNode["mappings"];
		if (!mappingsNode.IsArray())
			throw InvalidKeyboardMapFileException();

		vdfastvector<uint32> mappings;
		size_t n = mappingsNode.GetArrayLength();
		for(size_t i=0; i<n; ++i) {
			const auto& mappingNode = mappingsNode[i];

			if (!mappingNode.IsObject())
				throw InvalidKeyboardMapFileException();

			const auto& scanCodeNode = mappingNode["scancode"];
			if (!scanCodeNode.IsValid())
				throw InvalidKeyboardMapFileException();

			sint64 scanCode = scanCodeNode.AsInt64();
			if (scanCode < 0 || scanCode > 511 || !ATIsValidScanCode((uint32)scanCode))
				continue;

			const auto& charNode = mappingNode["char"];
			if (charNode.IsValid()) {
				uint32 charCode = 0;

				if (charNode.IsString()) {
					const wchar_t *s = charNode.AsString();
					if (!*s || s[1])
						throw InvalidKeyboardMapFileException();

					charCode = (uint32)(uint16)*s;
				} else if (charNode.IsInt()) {
					sint64 cc = charNode.AsInt64();
					if (cc < 0 || cc >= 65535)
						continue;

					charCode = (uint16)cc;
				} else
					throw InvalidKeyboardMapFileException();

				mappings.push_back(ATUIPackKeyboardMapping((uint32)scanCode, charCode, kATUIKeyboardMappingModifier_Cooked));
			} else {
				const auto& vkNode = mappingNode["vk"];
				sint64 vk;

				if (vkNode.IsInt()) {
					vk = vkNode.AsInt64();
					if (vk <= 0 || vk > 65535)
						continue;
				} else if (vkNode.IsString()) {
					const wchar_t *s = vkNode.AsString();

					if (!s[0] || s[1])
						throw InvalidKeyboardMapFileException();

					uint32 ch = (uint32)s[0];

					if ((ch - (uint32)'0') >= 10 && (ch - (uint32)'A') >= 26)
						throw InvalidKeyboardMapFileException();

					vk = ch;
				} else
					throw InvalidKeyboardMapFileException();

				const auto& modifiersNode = mappingNode["modifiers"];
				sint64 mods = 0;
				if (modifiersNode.IsValid()) {
					if (!modifiersNode.IsInt())
						throw InvalidKeyboardMapFileException();

					mods = modifiersNode.AsInt64();
					if (mods <= 0 || mods > 15)
						continue;
				}

				mappings.push_back(ATUIPackKeyboardMapping((uint32)scanCode, (uint32)vk, (uint32)mods << 25));
			}
		}

		// strip duplicates
		std::sort(mappings.begin(), mappings.end());
		mappings.erase(std::unique(mappings.begin(), mappings.end()), mappings.end());

		// all good!
		mMappings.swap(mappings);
		SortMappings();
		ReloadEmuKeyList();
		RebuildMappingList();
	} catch(const MyError& e) {
		ShowError(e);
	} catch(const InvalidKeyboardMapFileException&) {
		VDStringW err;
		err.sprintf(L"\"%ls\" is not a valid keyboard map file.", s.c_str());
		ShowError(s.c_str());
	}
}

void ATUIDialogKeyboardCustomize::OnExportClicked() {
	const auto& s = VDGetSaveFileName('kmap', (VDGUIHandle)mhdlg, L"Save custom keyboard map", L"Altirra keyboard map (*.atkmap)\0*.atkmap\0", L"atkmap");
	if (s.empty())
		return;

	try {
		struct Output final : public IVDJSONWriterOutput {
			void WriteChars(const wchar_t *src, uint32 len) override {
				while(len--) {
					wchar_t c = *src++;

					if (c == '\n')
						s += '\r';

					s += c;
				}
			}

			VDStringW s;
		} output;

		VDJSONWriter writer;

		writer.Begin(&output);
		writer.OpenObject();
		writer.WriteMemberName(L".comment");
		writer.WriteString(L"Altirra keyboard map");
		writer.WriteMemberName(L".type");
		writer.WriteString(L"keymap");
		writer.WriteMemberName(L"mappings");

		writer.OpenArray();

		auto sortedMappings = mMappings;

		std::sort(sortedMappings.begin(), sortedMappings.end());

		for(uint32 mapping : sortedMappings) {
			writer.OpenObject();

			writer.WriteMemberName(L"scancode");
			writer.WriteInt(mapping & 0x1FF);

			if (mapping & kATUIKeyboardMappingModifier_Cooked) {
				writer.WriteMemberName(L"char");
				
				uint32 ch = (mapping >> 9) & 0xFFFF;
				if (ch >= 0x20 && ch < 0x7F) {
					wchar_t buf[2] = { (wchar_t)ch, 0 };

					writer.WriteString(buf);
				} else
					writer.WriteInt(ch);
			} else {
				writer.WriteMemberName(L"vk");

				uint32 vk = (mapping >> 9) & 0xFFFF;
				if ((vk - 0x30) < 10 || (vk - 0x41) < 26) {
					wchar_t buf[2] = { (wchar_t)vk, 0 };

					writer.WriteString(buf);
				} else
					writer.WriteInt(vk);

				if (mapping >> 25) {
					writer.WriteMemberName(L"modifiers");
					writer.WriteInt(mapping >> 25);
				}
			}

			writer.Close();
		}
		writer.Close();

		writer.Close();
		writer.End();

		VDFileStream fs(s.c_str(), nsVDFile::kWrite | nsVDFile::kCreateAlways | nsVDFile::kDenyAll | nsVDFile::kSequential);
		VDStringA u8s(VDTextWToU8(output.s));

		fs.write(u8s.data(), u8s.size());
	} catch(const MyError& e) {
		ShowError(e);
	}
}

void ATUIDialogKeyboardCustomize::OnClearClicked() {
	if (Confirm(L"Are you sure you want to clear all key mappings?")) {
		mMappings.clear();
		ReloadEmuKeyList();
		RebuildMappingList();
	}
}

void ATUIDialogKeyboardCustomize::SetCookedMode(bool enabled) {
	if (mpHotKeyControl)
		mpHotKeyControl->SetCookedMode(enabled);
}

void ATUIDialogKeyboardCustomize::ReloadEmuKeyList() {
	mScanCodeList.SetRedraw(false);
	mScanCodeList.Clear();

	mScanCodeFilteredList.clear();

	VDStringW searchText;
	GetControlText(IDC_SEARCH, searchText);
	std::transform(searchText.begin(), searchText.end(), searchText.begin(), towlower);

	unsigned int index = 0;
	VDStringW temp;
	for(const auto& entry : kScanCodeTable) {
		if (!searchText.empty()) {
			temp = entry.mpLabel;

			std::transform(temp.begin(), temp.end(), temp.begin(), towlower);
			if (!wcsstr(temp.c_str(), searchText.c_str())) {
				++index;
				continue;
			}
		}

		mScanCodeFilteredList.push_back(index);

		if (IsScanCodeMapped(entry.mScanCode)) {
			mScanCodeList.AddItem(entry.mpLabel, index);
		} else {
			temp = entry.mpLabel;
			temp += L" [not mapped]";

			mScanCodeList.AddItem(temp.c_str(), index);
		}

		++index;
	}

	mScanCodeList.SetRedraw(true);
}

void ATUIDialogKeyboardCustomize::SortMappings() {
	std::sort(mMappings.begin(), mMappings.end(), [this](uint32 a, uint32 b) { return CompareMappingOrder(a, b); });
}

void ATUIDialogKeyboardCustomize::RebuildMappingList() {
	mBindingListControl.SetRedraw(false);
	mBindingListControl.Clear();

	for(const auto& mapping : mMappings) {
		auto p = vdmakerefptr(new MappingEntry(mScanCodeToEntry[mapping & 0x1FF], mapping));

		mBindingListControl.InsertVirtualItem(-1, p);
	}

	mBindingListControl.SetRedraw(true);
	mBindingListControl.AutoSizeColumns(true);
}

bool ATUIDialogKeyboardCustomize::CompareMappingOrder(uint32 a, uint32 b) const {
	if ((a^b) & 0x1FF) {
		uint8 scanCodeOrderA = mScanCodeToEntry[a & 0x1FF];
		uint8 scanCodeOrderB = mScanCodeToEntry[b & 0x1FF];

		return scanCodeOrderA < scanCodeOrderB;
	}

	return a < b;
}

uint32 ATUIDialogKeyboardCustomize::GetCurrentAccelMapping() const {
	if (!mpHotKeyControl)
		return 0;

	VDUIAccelerator acc;
	mpHotKeyControl->GetAccelerator(acc);

	return GetMappingForAccel(acc);
}

uint32 ATUIDialogKeyboardCustomize::GetMappingForAccel(const VDUIAccelerator& acc) {
	if (!acc.mVirtKey)
		return 0;

	uint32 modifiers = 0;

	if (acc.mModifiers & acc.kModShift)
		modifiers += kATUIKeyboardMappingModifier_Shift;

	if (acc.mModifiers & acc.kModCtrl)
		modifiers += kATUIKeyboardMappingModifier_Ctrl;

	if (acc.mModifiers & acc.kModAlt)
		modifiers += kATUIKeyboardMappingModifier_Alt;

	if (acc.mModifiers & acc.kModExtended)
		modifiers += kATUIKeyboardMappingModifier_Extended;

	if (acc.mModifiers & acc.kModCooked)
		modifiers += kATUIKeyboardMappingModifier_Cooked;

	return ATUIPackKeyboardMapping(0, acc.mVirtKey, modifiers);
}

VDUIAccelerator ATUIDialogKeyboardCustomize::GetAccelForMapping(uint32 mapping) {
	VDUIAccelerator accel = {};
	accel.mVirtKey = (mapping >> 9) & 0xFFFF;

	if (mapping & kATUIKeyboardMappingModifier_Shift)
		accel.mModifiers += VDUIAccelerator::kModShift;

	if (mapping & kATUIKeyboardMappingModifier_Ctrl)
		accel.mModifiers += VDUIAccelerator::kModCtrl;

	if (mapping & kATUIKeyboardMappingModifier_Alt)
		accel.mModifiers += VDUIAccelerator::kModAlt;

	if (mapping & kATUIKeyboardMappingModifier_Extended)
		accel.mModifiers += VDUIAccelerator::kModExtended;

	if (mapping & kATUIKeyboardMappingModifier_Cooked)
		accel.mModifiers += VDUIAccelerator::kModCooked;

	return accel;
}

bool ATUIDialogKeyboardCustomize::IsScanCodeMapped(uint32 scanCode) const {
	return std::find_if(mMappings.begin(), mMappings.end(), [=](uint32 mapping) { return (mapping & 0x1FF) == scanCode; }) != mMappings.end();
}

void ATUIDialogKeyboardCustomize::UpdateScanCodeIsMapped(uint32 scanCode) {
	if (scanCode > kATUIKeyScanCodeLast)
		return;

	uint8 scanCodeIndex = mScanCodeToEntry[scanCode];

	if (scanCodeIndex >= vdcountof(kScanCodeTable))
		return;

	auto it = std::lower_bound(mScanCodeFilteredList.begin(), mScanCodeFilteredList.end(), scanCodeIndex);
	if (it == mScanCodeFilteredList.end() || *it != scanCodeIndex)
		return;

	int scanCodeFilteredIndex = (int)(it - mScanCodeFilteredList.begin());

	VDStringW s(kScanCodeTable[scanCodeIndex].mpLabel);

	if (!IsScanCodeMapped(scanCode))
		s += L" [not mapped]";

	mScanCodeList.SetItemText(scanCodeFilteredIndex, s.c_str());
}

///////////////////////////////////////////////////////////////////////////

bool ATUIShowDialogKeyboardCustomize(VDGUIHandle hParent) {
	ATUIDialogKeyboardCustomize dlg;

	if (!dlg.ShowDialog(hParent))
		return false;

	return true;
}
