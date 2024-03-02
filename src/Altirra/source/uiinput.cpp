//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2009 Avery Lee
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
#include <vd2/system/strutil.h>
#include "Dialog.h"
#include "inputmanager.h"
#include "inputcontroller.h"
#include "uiproxies.h"
#include "resource.h"
#include "joystick.h"

class ATUIDialogEditInputMapping : public VDDialogFrameW32 {
public:
	ATUIDialogEditInputMapping(ATInputManager& iman, IATJoystickManager *ijoy, uint32 inputCode, ATInputControllerType ctype, int cindex, uint32 targetCode);

	uint32 GetInputCode() const { return mInputCode; }
	ATInputControllerType GetControllerType() const { return mControllerType; }
	int GetControllerIndex() { return mControllerIndex; }
	uint32 GetTargetCode() { return mTargetCode; }

protected:
	enum { kTimerId_JoyPoll = 100 };

	bool OnLoaded();
	void OnDestroy();
	void OnDataExchange(bool write);
	bool OnTimer(uint32 id);

	uint32 mInputCode;
	ATInputControllerType mControllerType;
	int mControllerIndex;
	uint32 mTargetCode;

	ATInputManager& mInputMan;
	IATJoystickManager *const mpJoyMan;

	static const uint32 kInputCodes[];
	static const uint32 kTargetCodes[];
};

const uint32 ATUIDialogEditInputMapping::kInputCodes[] = {
	kATInputCode_KeyLShift,
	kATInputCode_KeyRShift,
	kATInputCode_KeyLControl,
	kATInputCode_KeyRControl,

	kATInputCode_KeyLeft,
	kATInputCode_KeyUp,
	kATInputCode_KeyRight,
	kATInputCode_KeyDown,

	kATInputCode_0 + 0,
	kATInputCode_0 + 1,
	kATInputCode_0 + 2,
	kATInputCode_0 + 3,
	kATInputCode_0 + 4,
	kATInputCode_0 + 5,
	kATInputCode_0 + 6,
	kATInputCode_0 + 7,
	kATInputCode_0 + 8,
	kATInputCode_0 + 9,

	kATInputCode_A + 0,
	kATInputCode_A + 1,
	kATInputCode_A + 2,
	kATInputCode_A + 3,
	kATInputCode_A + 4,
	kATInputCode_A + 5,
	kATInputCode_A + 6,
	kATInputCode_A + 7,
	kATInputCode_A + 8,
	kATInputCode_A + 9,
	kATInputCode_A + 10,
	kATInputCode_A + 11,
	kATInputCode_A + 12,
	kATInputCode_A + 13,
	kATInputCode_A + 14,
	kATInputCode_A + 15,
	kATInputCode_A + 16,
	kATInputCode_A + 17,
	kATInputCode_A + 18,
	kATInputCode_A + 19,
	kATInputCode_A + 20,
	kATInputCode_A + 21,
	kATInputCode_A + 22,
	kATInputCode_A + 23,
	kATInputCode_A + 24,
	kATInputCode_A + 25,


	kATInputCode_KeyBack,
	kATInputCode_KeyTab,
	kATInputCode_KeyReturn,
	kATInputCode_KeyEscape,
	kATInputCode_KeySpace,
	kATInputCode_KeyPrior,
	kATInputCode_KeyNext,
	kATInputCode_KeyEnd,
	kATInputCode_KeyHome,
	kATInputCode_KeyInsert,
	kATInputCode_KeyDelete,
	kATInputCode_KeyNumpad0,
	kATInputCode_KeyNumpad1,
	kATInputCode_KeyNumpad2,
	kATInputCode_KeyNumpad3,
	kATInputCode_KeyNumpad4,
	kATInputCode_KeyNumpad5,
	kATInputCode_KeyNumpad6,
	kATInputCode_KeyNumpad7,
	kATInputCode_KeyNumpad8,
	kATInputCode_KeyNumpad9,
	kATInputCode_KeyNumpadEnter,
	kATInputCode_KeyMultiply,
	kATInputCode_KeyAdd,
	kATInputCode_KeySubtract,
	kATInputCode_KeyDecimal,
	kATInputCode_KeyDivide,
	kATInputCode_KeyF1,
	kATInputCode_KeyF2,
	kATInputCode_KeyF3,
	kATInputCode_KeyF4,
	kATInputCode_KeyF5,
	kATInputCode_KeyF6,
	kATInputCode_KeyF7,
	kATInputCode_KeyF8,
	kATInputCode_KeyF9,
	kATInputCode_KeyF10,
	kATInputCode_KeyF11,
	kATInputCode_KeyF12,
	kATInputCode_KeyOem1,
	kATInputCode_KeyOemPlus,
	kATInputCode_KeyOemComma,
	kATInputCode_KeyOemMinus,
	kATInputCode_KeyOemPeriod,
	kATInputCode_KeyOem2,
	kATInputCode_KeyOem3,
	kATInputCode_KeyOem4,
	kATInputCode_KeyOem5,
	kATInputCode_KeyOem6,
	kATInputCode_KeyOem7,

	kATInputCode_MouseHoriz,
	kATInputCode_MouseVert,
	kATInputCode_MouseLeft,
	kATInputCode_MouseRight,
	kATInputCode_MouseUp,
	kATInputCode_MouseDown,
	kATInputCode_MouseLMB,
	kATInputCode_MouseMMB,
	kATInputCode_MouseRMB,
	kATInputCode_MouseX1B,
	kATInputCode_MouseX2B,
	kATInputCode_JoyHoriz1,
	kATInputCode_JoyVert1,
	kATInputCode_JoyVert2,
	kATInputCode_JoyHoriz3,
	kATInputCode_JoyVert3,
	kATInputCode_JoyVert4,
	kATInputCode_JoyPOVHoriz,
	kATInputCode_JoyPOVVert,
	kATInputCode_JoyStick1Left,
	kATInputCode_JoyStick1Right,
	kATInputCode_JoyStick1Up,
	kATInputCode_JoyStick1Down,
	kATInputCode_JoyStick2Up,
	kATInputCode_JoyStick2Down,
	kATInputCode_JoyStick3Left,
	kATInputCode_JoyStick3Right,
	kATInputCode_JoyStick3Up,
	kATInputCode_JoyStick3Down,
	kATInputCode_JoyStick4Up,
	kATInputCode_JoyStick4Down,
	kATInputCode_JoyPOVLeft,
	kATInputCode_JoyPOVRight,
	kATInputCode_JoyPOVUp,
	kATInputCode_JoyPOVDown,
	kATInputCode_JoyButton0+0,
	kATInputCode_JoyButton0+1,
	kATInputCode_JoyButton0+2,
	kATInputCode_JoyButton0+3,
	kATInputCode_JoyButton0+4,
	kATInputCode_JoyButton0+5,
	kATInputCode_JoyButton0+6,
	kATInputCode_JoyButton0+7,
	kATInputCode_JoyButton0+8,
	kATInputCode_JoyButton0+9,
	kATInputCode_JoyButton0+10,
	kATInputCode_JoyButton0+11,
	kATInputCode_JoyButton0+12,
	kATInputCode_JoyButton0+13,
	kATInputCode_JoyButton0+14,
	kATInputCode_JoyButton0+15,
	kATInputCode_JoyButton0+16,
	kATInputCode_JoyButton0+17,
	kATInputCode_JoyButton0+18,
	kATInputCode_JoyButton0+19,
	kATInputCode_JoyButton0+20,
	kATInputCode_JoyButton0+21,
	kATInputCode_JoyButton0+22,
	kATInputCode_JoyButton0+23,
	kATInputCode_JoyButton0+24,
	kATInputCode_JoyButton0+25,
	kATInputCode_JoyButton0+26,
	kATInputCode_JoyButton0+27,
	kATInputCode_JoyButton0+28,
	kATInputCode_JoyButton0+29,
	kATInputCode_JoyButton0+30,
	kATInputCode_JoyButton0+31,
};

const uint32 ATUIDialogEditInputMapping::kTargetCodes[] = {
	kATInputTrigger_Button0,
	kATInputTrigger_Up,
	kATInputTrigger_Down,
	kATInputTrigger_Left,
	kATInputTrigger_Right,
	kATInputTrigger_Axis0,
	kATInputTrigger_Axis0+1,
	kATInputTrigger_Start,
	kATInputTrigger_Select,
	kATInputTrigger_Option,
	kATInputTrigger_Turbo,
	kATInputTrigger_ColdReset,
	kATInputTrigger_WarmReset,
	kATInputTrigger_KeySpace,
};

ATUIDialogEditInputMapping::ATUIDialogEditInputMapping(ATInputManager& iman, IATJoystickManager *ijoy, uint32 inputCode, ATInputControllerType ctype, int cindex, uint32 targetCode)
	: VDDialogFrameW32(IDD_INPUTMAP_ADD)
	, mInputCode(inputCode)
	, mControllerType(ctype)
	, mControllerIndex(cindex)
	, mTargetCode(targetCode)
	, mInputMan(iman)
	, mpJoyMan(ijoy)
{
}

bool ATUIDialogEditInputMapping::OnLoaded() {
	VDStringW name;
	for(uint32 i=0; i<sizeof(kInputCodes)/sizeof(kInputCodes[0]); ++i) {
		mInputMan.GetNameForInputCode(kInputCodes[i], name);
		CBAddString(IDC_SOURCE, name.c_str());
	}

	for(uint32 i=0; i<sizeof(kTargetCodes)/sizeof(kTargetCodes[0]); ++i) {
		mInputMan.GetNameForTargetCode(kTargetCodes[i], name);
		CBAddString(IDC_TARGET, name.c_str());
	}

	CBAddString(IDC_CONTROLLER, L"Joystick");
	CBAddString(IDC_CONTROLLER, L"Mouse");
	CBAddString(IDC_CONTROLLER, L"Paddle A");
	CBAddString(IDC_CONTROLLER, L"Paddle B");
	CBAddString(IDC_CONTROLLER, L"Console");

	CBAddString(IDC_PORT, L"Port 1");
	CBAddString(IDC_PORT, L"Port 2");
	CBAddString(IDC_PORT, L"Port 3 (800 only)");
	CBAddString(IDC_PORT, L"Port 4 (800 only)");

	OnDataExchange(false);
	SetFocusToControl(IDC_SOURCE);

	if (mpJoyMan) {
		mpJoyMan->SetCaptureMode(true);
		SetPeriodicTimer(kTimerId_JoyPoll, 20);
	}
	return true;
}

void ATUIDialogEditInputMapping::OnDestroy() {
	if (mpJoyMan)
		mpJoyMan->SetCaptureMode(false);
}

void ATUIDialogEditInputMapping::OnDataExchange(bool write) {
	if (write) {
		mInputCode = kInputCodes[CBGetSelectedIndex(IDC_SOURCE)];
		mTargetCode = kTargetCodes[CBGetSelectedIndex(IDC_TARGET)];

		int index = CBGetSelectedIndex(IDC_PORT);

		switch(CBGetSelectedIndex(IDC_CONTROLLER)) {
			case 0:
				mControllerType = kATInputControllerType_Joystick;
				mControllerIndex = index;
				break;
			case 1:
				mControllerType = kATInputControllerType_Mouse;
				mControllerIndex = index;
				break;
			case 2:
				mControllerType = kATInputControllerType_Paddle;
				mControllerIndex = index*2;
				break;
			case 3:
				mControllerType = kATInputControllerType_Paddle;
				mControllerIndex = index*2+1;
				break;
			case 4:
				mControllerType = kATInputControllerType_Console;
				mControllerIndex = 0;
				break;
		}
	} else {
		int selIdx = 0;
		for(uint32 i=0; i<sizeof(kInputCodes)/sizeof(kInputCodes[0]); ++i) {
			if (kInputCodes[i] == (mInputCode & 0xffff)) {
				selIdx = i;
				break;
			}
		}

		CBSetSelectedIndex(IDC_SOURCE, selIdx);

		selIdx = 0;
		for(uint32 i=0; i<sizeof(kTargetCodes)/sizeof(kTargetCodes[0]); ++i) {
			if (kTargetCodes[i] == mTargetCode) {
				selIdx = i;
				break;
			}
		}

		CBSetSelectedIndex(IDC_TARGET, selIdx);

		switch(mControllerType) {
			case kATInputControllerType_Joystick:
				CBSetSelectedIndex(IDC_CONTROLLER, 0);
				CBSetSelectedIndex(IDC_PORT, mControllerIndex);
				break;
			case kATInputControllerType_Mouse:
				CBSetSelectedIndex(IDC_CONTROLLER, 1);
				CBSetSelectedIndex(IDC_PORT, mControllerIndex);
				break;
			case kATInputControllerType_Paddle:
				CBSetSelectedIndex(IDC_CONTROLLER, mControllerIndex & 1 ? 3 : 2);
				CBSetSelectedIndex(IDC_PORT, mControllerIndex >> 1);
				break;
			case kATInputControllerType_Console:
				CBSetSelectedIndex(IDC_CONTROLLER, 4);
				CBSetSelectedIndex(IDC_PORT, 0);
				break;
		}
	}
}

bool ATUIDialogEditInputMapping::OnTimer(uint32 id) {
	if (id == kTimerId_JoyPoll) {
		if (mpJoyMan) {
			int unit;
			uint32 inputCode;
			if (mpJoyMan->PollForCapture(unit, inputCode)) {
				for(uint32 i=0; i<sizeof(kInputCodes)/sizeof(kInputCodes[0]); ++i) {
					if (kInputCodes[i] == inputCode) {
						CBSetSelectedIndex(IDC_SOURCE, i);
						break;
					}
				}
			}
		}
		return true;
	}

	return false;
}

///////////////////////////////////////////////////////////////////////////

class ATUIDialogInputMapListItem : public vdrefcounted<IVDUIListViewVirtualItem> {
public:
	ATUIDialogInputMapListItem(ATInputManager& iman, uint32 inputCode, ATInputControllerType controllerType, int unit, uint32 targetCode);
	void GetText(int subItem, VDStringW& s) const;

	uint32 GetInputCode() const { return mInputCode; }
	ATInputControllerType GetControllerType() const { return mControllerType; }
	int GetControllerUnit() { return mControllerUnit; }
	uint32 GetTargetCode() const { return mTargetCode; }

protected:
	uint32	mInputCode;
	ATInputControllerType	mControllerType;
	int		mControllerUnit;
	uint32	mTargetCode;
	ATInputManager& mInputMan;
};

ATUIDialogInputMapListItem::ATUIDialogInputMapListItem(ATInputManager& iman, uint32 inputCode, ATInputControllerType controllerType, int unit, uint32 targetCode)
	: mInputCode(inputCode)
	, mControllerType(controllerType)
	, mControllerUnit(unit)
	, mTargetCode(targetCode)
	, mInputMan(iman)
{
}

void ATUIDialogInputMapListItem::GetText(int subItem, VDStringW& s) const {
	switch(subItem) {
		case 0:
			mInputMan.GetNameForInputCode(mInputCode, s);
			break;

		case 1:
			switch(mControllerType) {
				case kATInputControllerType_Joystick:
					s.sprintf(L"Joystick (port %d)", mControllerUnit + 1);
					break;

				case kATInputControllerType_Paddle:
					s.sprintf(L"Paddle %c (port %d)", mControllerUnit & 1 ? 'B' : 'A', (mControllerUnit >> 1) + 1);
					break;

				case kATInputControllerType_Mouse:
					s.sprintf(L"Mouse (port %d)", mControllerUnit + 1);
					break;

				case kATInputControllerType_Console:
					s = L"Console";
					break;
			}
			break;

		case 2:
			mInputMan.GetNameForTargetCode(mTargetCode, s);
			break;
	}
}

///////////////////////////////////////////////////////////////////////////

class ATUIDialogEditInputMap : public VDDialogFrameW32 {
public:
	ATUIDialogEditInputMap(ATInputManager& iman, IATJoystickManager *ijoy, ATInputMap& imap);
	~ATUIDialogEditInputMap();

protected:
	bool OnLoaded();
	void OnDataExchange(bool write);
	bool OnCommand(uint32 id, uint32 extcode);
	void OnItemSelectionChanged(VDUIProxyListView *source, int index); 

	VDUIProxyListView mListView;

	ATInputManager& mInputMan;
	IATJoystickManager *const mpJoyMan;
	ATInputMap& mInputMap;

	ATUIDialogEditInputMapping mEditMappingDialog;
	VDDelegate mItemSelectedDelegate;
};

ATUIDialogEditInputMap::ATUIDialogEditInputMap(ATInputManager& iman, IATJoystickManager *ijoy, ATInputMap& imap)
	: VDDialogFrameW32(IDD_INPUTMAP_EDIT)
	, mInputMan(iman)
	, mpJoyMan(ijoy)
	, mInputMap(imap)
	, mEditMappingDialog(iman, ijoy, kATInputCode_JoyButton0, kATInputControllerType_Joystick, 0, kATInputTrigger_Button0)
{
}

ATUIDialogEditInputMap::~ATUIDialogEditInputMap() {
}

bool ATUIDialogEditInputMap::OnLoaded() {
	AddProxy(&mListView, IDC_LIST);

	mListView.OnItemSelectionChanged() += mItemSelectedDelegate.Bind(this, &ATUIDialogEditInputMap::OnItemSelectionChanged);
	mListView.SetFullRowSelectEnabled(true);
	mListView.InsertColumn(0, L"Source", 10);
	mListView.InsertColumn(1, L"Controller", 10);
	mListView.InsertColumn(2, L"Target", 10);

	OnDataExchange(false);

	SetFocusToControl(IDC_LIST);
	return true;
}

void ATUIDialogEditInputMap::OnDataExchange(bool write) {
	if (write) {
		mInputMap.Clear();

		int n = mListView.GetItemCount();
		int nc = 0;
		for(int i=0; i<n; ++i) {
			ATUIDialogInputMapListItem *item = static_cast<ATUIDialogInputMapListItem *>(mListView.GetVirtualItem(i));

			if (item) {
				const uint32 inputCode = item->GetInputCode();
				const ATInputControllerType ctype = item->GetControllerType();
				const int cunit = item->GetControllerUnit();
				const uint32 targetCode = item->GetTargetCode();

				int cid = -1;
				for(int j=0; j<nc; ++j) {
					const ATInputMap::Controller& controller = mInputMap.GetController(j);
					if (controller.mType == ctype && controller.mIndex == cunit) {
						cid = j;
						break;
					}
				}

				if (cid < 0) {
					cid = nc++;
					mInputMap.AddController(ctype, cunit);
				}

				mInputMap.AddMapping(inputCode, cid, targetCode);
			}
		}
	} else {
		uint32 n = mInputMap.GetMappingCount();
		for(uint32 i=0; i<n; ++i) {
			const ATInputMap::Mapping& mapping = mInputMap.GetMapping(i);
			const ATInputMap::Controller& controller = mInputMap.GetController(mapping.mControllerId);

			vdrefptr<IVDUIListViewVirtualItem> item(new ATUIDialogInputMapListItem(mInputMan, mapping.mInputCode, controller.mType, controller.mIndex, mapping.mCode));
			mListView.InsertVirtualItem(i, item);
		}

		mListView.AutoSizeColumns();
	}
}

bool ATUIDialogEditInputMap::OnCommand(uint32 id, uint32 extcode) {
	if (id == IDC_ADD) {
		if (mEditMappingDialog.ShowDialog((VDGUIHandle)mhdlg)) {
			vdrefptr<IVDUIListViewVirtualItem> item(new ATUIDialogInputMapListItem(mInputMan
				, mEditMappingDialog.GetInputCode()
				, mEditMappingDialog.GetControllerType()
				, mEditMappingDialog.GetControllerIndex()
				, mEditMappingDialog.GetTargetCode()
				));
			mListView.InsertVirtualItem(mListView.GetItemCount(), item);
			mListView.AutoSizeColumns();
		}
		return true;
	} else if (id == IDC_DELETE) {
		int selIdx = mListView.GetSelectedIndex();
		if (selIdx >= 0)
			mListView.DeleteItem(selIdx);
	}

	return VDDialogFrameW32::OnCommand(id, extcode);
}

void ATUIDialogEditInputMap::OnItemSelectionChanged(VDUIProxyListView *source, int index) {
	EnableControl(IDC_DELETE, index >= 0);
	EnableControl(IDC_EDIT, index >= 0);
}

///////////////////////////////////////////////////////////////////////////

class ATUIDialogInputListItem : public vdrefcounted<IVDUIListViewVirtualItem> {
public:
	ATUIDialogInputListItem(ATInputMap *imap) : mpInputMap(imap) {}

	ATInputMap *GetInputMap() const { return mpInputMap; }

	void GetText(int subItem, VDStringW& s) const;

protected:
	vdrefptr<ATInputMap> mpInputMap;
};

void ATUIDialogInputListItem::GetText(int subItem, VDStringW& s) const {
	if (subItem == 0)
		s = mpInputMap->GetName();
}

///////////////////////////////////////////////////////////////////////////

class ATUIDialogInput : public VDDialogFrameW32 {
public:
	ATUIDialogInput(ATInputManager& iman, IATJoystickManager *ijoy);
	~ATUIDialogInput();

protected:
	bool OnLoaded();
	bool OnCommand(uint32 id, uint32 extcode);
	void OnDataExchange(bool write);
	void SortItems();
	void OnItemCheckedChanged(VDUIProxyListView *source, int index); 
	void OnItemSelectionChanged(VDUIProxyListView *source, int index); 
	void OnItemLabelChanged(VDUIProxyListView *source, const VDUIProxyListView::LabelEventData& index); 

	VDUIProxyListView mListView;
	ATInputManager& mInputMan;
	IATJoystickManager *const mpJoyMan;

	VDDelegate mItemCheckedDelegate;
	VDDelegate mItemSelectedDelegate;
	VDDelegate mItemLabelEditedDelegate;
};

ATUIDialogInput::ATUIDialogInput(ATInputManager& iman, IATJoystickManager *ijoy)
	: VDDialogFrameW32(IDD_INPUT_MAPPINGS)
	, mInputMan(iman)
	, mpJoyMan(ijoy)
{
}

ATUIDialogInput::~ATUIDialogInput() {
}

bool ATUIDialogInput::OnLoaded() {
	AddProxy(&mListView, IDC_LIST);

	mListView.OnItemCheckedChanged() += mItemCheckedDelegate.Bind(this, &ATUIDialogInput::OnItemCheckedChanged);
	mListView.OnItemSelectionChanged() += mItemSelectedDelegate.Bind(this, &ATUIDialogInput::OnItemSelectionChanged);
	mListView.OnItemLabelChanged() += mItemLabelEditedDelegate(this, &ATUIDialogInput::OnItemLabelChanged);
	mListView.SetFullRowSelectEnabled(true);
	mListView.SetItemCheckboxesEnabled(true);
	mListView.InsertColumn(0, L"Name", 10);

	OnDataExchange(false);
	SetFocusToControl(IDC_LIST);
	return true;
}

bool ATUIDialogInput::OnCommand(uint32 id, uint32 extcode) {
	if (id == IDC_ADD) {
		vdrefptr<ATInputMap> newMap(new ATInputMap);

		VDStringW s;
		s.sprintf(L"Input map %d", mInputMan.GetInputMapCount() + 1);
		newMap->SetName(s.c_str());

		vdrefptr<IVDUIListViewVirtualItem> item(new ATUIDialogInputListItem(newMap));
		int idx = mListView.InsertVirtualItem(mListView.GetItemCount(), item);

		if (idx >= 0) {
			mInputMan.AddInputMap(newMap);

			SetFocusToControl(IDC_LIST);
			mListView.SetSelectedIndex(idx);
			mListView.EnsureItemVisible(idx);
			mListView.EditItemLabel(idx);
		}

	} else if (id == IDC_EDIT) {
		int index = mListView.GetSelectedIndex();
		if (index >= 0) {
			ATUIDialogInputListItem *item = static_cast<ATUIDialogInputListItem *>(mListView.GetVirtualItem(index));

			if (item) {
				ATInputMap *imap = item->GetInputMap();

				if (imap) {
					ATUIDialogEditInputMap dlg(mInputMan, mpJoyMan, *imap);

					bool wasEnabled = mInputMan.IsInputMapEnabled(imap);

					if (wasEnabled)
						mInputMan.ActivateInputMap(imap, false);

					dlg.ShowDialog((VDGUIHandle)mhdlg);

					mInputMan.ActivateInputMap(imap, wasEnabled);
				}
			}
		}

		return true;
	} else if (id == IDC_DELETE) {
		int index = mListView.GetSelectedIndex();
		if (index >= 0) {
			ATUIDialogInputListItem *item = static_cast<ATUIDialogInputListItem *>(mListView.GetVirtualItem(index));

			if (item) {
				ATInputMap *imap = item->GetInputMap();

				VDStringW msg;
				msg.sprintf(L"Are you sure you want to delete the input map \"%ls\"?", imap->GetName());
				if (Confirm(msg.c_str(), L"Altirra warning")) {
					mListView.DeleteItem(index);
					mInputMan.RemoveInputMap(imap);
				}
			}
		}

		return true;
	} else if (id == IDC_RESET) {
		if (Confirm(L"This will erase all custom input maps and restore the default ones. Continue?", L"Altirra warning")) {
			mInputMan.ResetToDefaults();
			OnDataExchange(false);
		}

		return true;
	}

	return VDDialogFrameW32::OnCommand(id, extcode);
}

void ATUIDialogInput::OnDataExchange(bool write) {
	if (!write) {
		mListView.Clear();

		uint32 n = mInputMan.GetInputMapCount();

		for(uint32 i=0; i<n; ++i) {
			vdrefptr<ATInputMap> imap;
			mInputMan.GetInputMapByIndex(i, ~imap);

			vdrefptr<IVDUIListViewVirtualItem> item(new ATUIDialogInputListItem(imap));
			int idx = mListView.InsertVirtualItem(i, item);

			if (idx >= 0)
				mListView.SetItemChecked(idx, mInputMan.IsInputMapEnabled(imap));
		}

		SortItems();

		mListView.AutoSizeColumns();
	}
}

namespace {
	struct InputMapComparer : public IVDUIListViewVirtualComparer {
		int Compare(IVDUIListViewVirtualItem *x, IVDUIListViewVirtualItem *y) {
			ATUIDialogInputListItem *a = static_cast<ATUIDialogInputListItem *>(x);
			ATUIDialogInputListItem *b = static_cast<ATUIDialogInputListItem *>(y);
			
			return vdwcsicmp(a->GetInputMap()->GetName(), b->GetInputMap()->GetName());
		}
	};
}

void ATUIDialogInput::SortItems() {
	mListView.Sort(InputMapComparer());
}

void ATUIDialogInput::OnItemCheckedChanged(VDUIProxyListView *source, int index) {
	ATUIDialogInputListItem *item = static_cast<ATUIDialogInputListItem *>(mListView.GetVirtualItem(index));

	if (item) {
		ATInputMap *imap = item->GetInputMap();

		mInputMan.ActivateInputMap(imap, mListView.IsItemChecked(index));
	}
}

void ATUIDialogInput::OnItemSelectionChanged(VDUIProxyListView *source, int index) {
	EnableControl(IDC_DELETE, index >= 0);
	EnableControl(IDC_EDIT, index >= 0);
}

void ATUIDialogInput::OnItemLabelChanged(VDUIProxyListView *source, const VDUIProxyListView::LabelEventData& data) {
	ATUIDialogInputListItem *item = static_cast<ATUIDialogInputListItem *>(mListView.GetVirtualItem(data.mIndex));

	if (item) {
		ATInputMap *imap = item->GetInputMap();

		VDStringW s;
		mListView.GetItemText(data.mIndex, s);
		imap->SetName(data.mpNewLabel);
		mListView.RefreshItem(data.mIndex);
	}
}

void ATUIShowDialogInputMappings(VDZHWND parent, ATInputManager& iman, IATJoystickManager *ijoy) {
	ATUIDialogInput dlg(iman, ijoy);

	dlg.ShowDialog((VDGUIHandle)parent);
}
