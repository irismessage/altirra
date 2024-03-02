//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2020 Avery Lee
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
#include <at/atnativeui/dialog.h>
#include <at/atnativeui/messageloop.h>
#include <at/atcore/configvar.h>
#include "resource.h"

////////////////////////////////////////////////////////////////////////////////

class ATUIDialogEditVariable final : public VDDialogFrameW32 {
public:
	ATUIDialogEditVariable(ATConfigVar& var);

	void SetOnClose(vdfunction<void()> fn);

private:
	bool OnLoaded();
	bool OnClose();
	void OnDestroy();
	bool PreNCDestroy();
	void OnDataExchange(bool write);

	ATConfigVar& mConfigVar;
	vdfunction<void()> mpOnClose;

	VDUIProxyEditControl mVarNameView;
	VDUIProxyEditControl mVarValueView;
	VDUIProxyButtonControl mVarBoolView;
	VDUIProxyButtonControl mResetView;
};

ATUIDialogEditVariable::ATUIDialogEditVariable(ATConfigVar& var)
	: VDDialogFrameW32(IDD_EDIT_VAR)
	, mConfigVar(var)
{
	mResetView.SetOnClicked(
		[this] {
			mConfigVar.Unset();
			End(true);
		}
	);
}

void ATUIDialogEditVariable::SetOnClose(vdfunction<void()> fn) {
	mpOnClose = std::move(fn);
}

bool ATUIDialogEditVariable::OnLoaded() {
	if (!mbIsModal)
		ATUIRegisterModelessDialog(mhdlg);

	AddProxy(&mVarNameView, IDC_NAME);
	AddProxy(&mVarValueView, IDC_VALUE);
	AddProxy(&mVarBoolView, IDC_BOOLOPT);
	AddProxy(&mResetView, IDC_RESET);

	mVarNameView.SetCaption(VDTextAToW(mConfigVar.mpVarName).c_str());
	OnDataExchange(false);

	if (mConfigVar.GetVarType() == ATConfigVarType::Bool) {
		mVarBoolView.Focus();
	} else {
		mVarValueView.Focus();
		mVarValueView.SelectAll();
	}
	
	return true;
}

bool ATUIDialogEditVariable::OnClose() {
	if (mpOnClose)
		mpOnClose();

	return VDDialogFrameW32::OnClose();
}

void ATUIDialogEditVariable::OnDestroy() {
	if (!mbIsModal)
		ATUIUnregisterModelessDialog(mhdlg);

	VDDialogFrameW32::OnDestroy();
}

bool ATUIDialogEditVariable::PreNCDestroy() {
	return true;
}

void ATUIDialogEditVariable::OnDataExchange(bool write) {
	if (mConfigVar.GetVarType() == ATConfigVarType::Bool) {
		if (write) {
			static_cast<ATConfigVarBool&>(mConfigVar) = mVarBoolView.GetChecked();
		} else {
			mVarValueView.SetVisible(false);
			mVarBoolView.SetVisible(true);
			mVarBoolView.SetChecked(static_cast<ATConfigVarBool&>(mConfigVar));
		}
	} else {
		if (write) {
			if (!mConfigVar.FromString(VDTextWToA(mVarValueView.GetCaption()).c_str()))
				FailValidation(mVarValueView.GetWindowId());
		} else {
			mVarBoolView.SetVisible(false);
			mVarValueView.SetVisible(true);
			mVarValueView.SetCaption(VDTextAToW(mConfigVar.ToString()).c_str());
		}
	}
}

////////////////////////////////////////////////////////////////////////////////

class ATUIDialogAdvancedConfiguration final : public VDDialogFrameW32 {
public:
	ATUIDialogAdvancedConfiguration();

	static void OpenModeless(VDGUIHandle parent);

private:
	struct VarItem;

	bool OnLoaded();
	void OnDestroy();
	bool PreNCDestroy();
	void OnItemDoubleClicked();

	VDUIProxyListView mVarsView;

	static ATUIDialogAdvancedConfiguration *spModelessInstance;
};

ATUIDialogAdvancedConfiguration *ATUIDialogAdvancedConfiguration::spModelessInstance;

struct ATUIDialogAdvancedConfiguration::VarItem final : public vdrefcounted<IVDUIListViewVirtualItem> {
	VarItem(ATConfigVar& var);
	VarItem(const char *name);

	void GetText(int subItem, VDStringW& s) const override;

	void ReadValue();

	ATConfigVar *mpVar;
	VDStringW mName;
	VDStringW mValue;
};

ATUIDialogAdvancedConfiguration::VarItem::VarItem(ATConfigVar& var)
	: mpVar(&var)
{
	mName = VDTextAToW(var.mpVarName);
	ReadValue();
}

ATUIDialogAdvancedConfiguration::VarItem::VarItem(const char *name)
	: mpVar(nullptr)
	, mName(VDTextAToW(name))
{
}

void ATUIDialogAdvancedConfiguration::VarItem::GetText(int subItem, VDStringW& s) const {
	switch(subItem) {
		case 0:
			s = mName;
			break;

		case 1:
			if (mpVar)
				s = mValue;
			else
				s = L"<unknown cvar>";
			break;
	}
}

void ATUIDialogAdvancedConfiguration::VarItem::ReadValue() {
	if (mpVar)
		mValue = VDTextAToW(mpVar->ToString());
}

////////////////////////////////////////////////////////////////////////////////

ATUIDialogAdvancedConfiguration::ATUIDialogAdvancedConfiguration()
	: VDDialogFrameW32(IDD_ADVANCED_CONFIGURATION)
{
	mVarsView.SetOnItemDoubleClicked([this](int idx) { OnItemDoubleClicked(); });
	mVarsView.SetOnItemCustomStyle([](IVDUIListViewVirtualItem& item, sint32& c, bool& bold) -> bool {
		auto& varItem = static_cast<const VarItem&>(item);
		bold = varItem.mpVar ? varItem.mpVar->mbOverridden : true;
		return true;
	});
}

void ATUIDialogAdvancedConfiguration::OpenModeless(VDGUIHandle parent) {
	if (spModelessInstance)
		spModelessInstance->Focus();
	else {
		auto *p = new ATUIDialogAdvancedConfiguration;

		if (!p->Create(parent))
			delete p;
	}
}

bool ATUIDialogAdvancedConfiguration::OnLoaded() {
	if (!mbIsModal) {
		ATUIRegisterModelessDialog(mhdlg);
		spModelessInstance = this;
	}

	AddProxy(&mVarsView, IDC_VARS);

	mResizer.Add(mVarsView.GetHandle(), mResizer.kMC);

	mVarsView.InsertColumn(0, L"Name", GetClientSize().w * 2 / 3);
	mVarsView.InsertColumn(1, L"Value", -1);

	ATConfigVar **vars = nullptr;
	size_t numVars = 0;
	ATGetConfigVars(vars, numVars);

	vdvector<vdrefptr<VarItem>> sortedVars(numVars);

	for(size_t i=0; i<numVars; ++i)
		sortedVars[i] = new VarItem(*vars[i]);

	const VDStringA *uvars = nullptr;
	size_t numUVars = 0;
	ATGetUndefinedConfigVars(uvars, numUVars);

	for(size_t i=0; i<numUVars; ++i)
		sortedVars.emplace_back() = new VarItem(uvars[i].c_str());

	std::sort(sortedVars.begin(), sortedVars.end(),
		[](VarItem *x, VarItem *y) -> bool {
			return x->mName < y->mName;
		}
	);

	for(VarItem *vi : sortedVars)
		mVarsView.InsertVirtualItem(-1, vi);

	mVarsView.SetFullRowSelectEnabled(true);
	mVarsView.Focus();
	return true;
}

void ATUIDialogAdvancedConfiguration::OnDestroy() {
	if (!mbIsModal) {
		spModelessInstance = nullptr;
		ATUIUnregisterModelessDialog(mhwnd);
	}

	VDDialogFrameW32::OnDestroy();
}

bool ATUIDialogAdvancedConfiguration::PreNCDestroy() {
	return !mbIsModal;
}

void ATUIDialogAdvancedConfiguration::OnItemDoubleClicked() {
	VarItem *item = static_cast<VarItem *>(mVarsView.GetSelectedItem());

	if (!item)
		return;

	if (item->mpVar) {
		ATUIDialogEditVariable *dlg = new ATUIDialogEditVariable(*item->mpVar);

		if (dlg->Create(this)) {
			SetEnabled(false);

			dlg->SetOnClose(
				[this] {
					SetEnabled(true);
					Activate();

					VarItem *item = static_cast<VarItem *>(mVarsView.GetSelectedItem());
					if (item) {
						item->ReadValue();
						mVarsView.RefreshItem(mVarsView.GetSelectedIndex());
					}
				}
			);
		} else
			delete dlg;
	} else {
		if (Confirm2("UndefCVar", L"This variable override isn't supported in this program version. Unset it?", L"Unknown variable override")) {
			ATUnsetUndefinedConfigVar(VDTextWToA(item->mName).c_str());
			mVarsView.DeleteItem(mVarsView.GetSelectedIndex());
		}
	}
}

////////////////////////////////////////////////////////////////////////////////

void ATUIShowDialogAdvancedConfiguration(VDGUIHandle h) {
	ATUIDialogAdvancedConfiguration dlg;

	dlg.ShowDialog(h);
}

void ATUIShowDialogAdvancedConfigurationModeless(VDGUIHandle h) {
	ATUIDialogAdvancedConfiguration::OpenModeless(h);
}
