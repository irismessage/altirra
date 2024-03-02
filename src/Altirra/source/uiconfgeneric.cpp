//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2022 Avery Lee
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

#include "stdafx.h"
#include <vd2/Dita/services.h>
#include <at/atcore/propertyset.h>
#include <at/atnativeui/dialog.h>
#include "resource.h"
#include "uiaccessors.h"
#include "uiconfgeneric.h"

class ATUIConfDialogGenericPanel final : public VDDialogFrameW32, public IATUIConfigView {
public:
	ATUIConfDialogGenericPanel(IATUIConfigController& controller);
	~ATUIConfDialogGenericPanel();

	IATUIConfigBoolView& AddCheckbox() override;
	IATUIConfigPathView& AddPath() override;

	void Read(const ATPropertySet& pset) override;
	void Write(ATPropertySet& pset) const override;

private:
	class BaseView;
	class BoolView;
	class StringView;
	class CheckboxView;
	class PathView;

	bool OnLoaded() override;

	void OnValueChanged();
	void UpdateEnables();

	void AddView(BaseView *view);

	static constexpr uint32 kBaseId = 30000;

	IATUIConfigController& mController;
	vdfastvector<BaseView *> mViews;

	sint32 mYPosDLUs = 0;
};

class ATUIConfDialogGenericPanel::BaseView : public VDDialogFrameW32, public IATUIConfigPropView {
public:
	using VDDialogFrameW32::VDDialogFrameW32;

	void SetViewIndex(ATUIConfDialogGenericPanel& parent, uint32 viewIndex) {
		mpParent = &parent;
		mViewIndex = viewIndex;
	}

	IATUIConfigPropView& SetTag(const char *tag) override {
		mTag = tag;
		return *this;
	}

	IATUIConfigPropView& SetLabel(const wchar_t *label) override {
		mLabel = label;
		UpdateLabel();
		return *this;
	}

	IATUIConfigPropView& SetHelp(const wchar_t *text) override {
		return *this;
	}

	IATUIConfigPropView& SetHelp(const wchar_t *caption, const wchar_t *text) override {
		return *this;
	}

	IATUIConfigPropView& SetEnableExpr(vdfunction<bool()> fn) override {
		mpEnableExpr = std::move(fn);
		return *this;
	}

	void UpdateEnable() {
		if (mpEnableExpr)
			SetEnabled(mpEnableExpr());
	}

	virtual void Read(const ATPropertySet& pset) = 0;
	virtual void Write(ATPropertySet& pset) const = 0;

protected:
	virtual void UpdateLabel() = 0;
	virtual void UpdateView() = 0;

	ATUIConfDialogGenericPanel *mpParent = nullptr;
	uint32 mViewIndex = 0;
	VDStringA mTag;
	VDStringW mLabel;
	vdfunction<bool()> mpEnableExpr;
};

class ATUIConfDialogGenericPanel::BoolView : public BaseView, public IATUIConfigBoolView {
public:
	using BaseView::BaseView;
	using BaseView::SetTag;

	IATUIConfigPropView *operator->() { return this; }
	IATUIConfigBoolView& SetDefault(bool val) override { mbDefault = val; return *this; }

	bool GetValue() const override {
		return mbValue;
	}

	void SetValue(bool v) override {
		if (mbValue != v) {
			mbValue = v;
			UpdateView();
		}
	}

	void Read(const ATPropertySet& pset) override {
		if (!mTag.empty())
			SetValue(pset.GetBool(mTag.c_str(), mbDefault));
	}

	void Write(ATPropertySet& pset) const override {
		if (!mTag.empty() && mbValue != mbDefault)
			pset.SetBool(mTag.c_str(), mbValue);
	}

protected:
	bool mbDefault = false;
	bool mbValue = false;
};

class ATUIConfDialogGenericPanel::StringView : public BaseView, public IATUIConfigStringView {
public:
	using BaseView::BaseView;
	using BaseView::SetTag;

	IATUIConfigPropView *operator->() { return this; }
	const wchar_t *GetValue() const override { return mValue.c_str(); }
	void SetValue(const wchar_t *s) override {
		const VDStringSpanW sp(s);

		if (mValue != sp) {
			mValue = sp;

			UpdateView();
		}
	}

	void Read(const ATPropertySet& pset) override {
		if (!mTag.empty())
			SetValue(pset.GetString(mTag.c_str(), L""));
	}

	void Write(ATPropertySet& pset) const override {
		if (!mTag.empty() && !mValue.empty())
			pset.SetString(mTag.c_str(), mValue.c_str());
	}

protected:
	VDStringW mValue;
};

class ATUIConfDialogGenericPanel::CheckboxView final : public BoolView {
public:
	CheckboxView() : BoolView(IDD_CFGPROP_CHECKBOX) {
		mCheckView.SetOnClicked(
			[this] {
				const bool v = mCheckView.GetChecked();
				if (mbValue != v) {
					mbValue = v;
					mpParent->OnValueChanged();
				}
			}
		);
	}

private:
	VDUIProxyButtonControl mCheckView;

	bool OnLoaded() override {
		AddProxy(&mCheckView, IDC_CHECK);
		mResizer.Add(mCheckView.GetWindowHandle(), mResizer.kTC);
		return false;
	}

	void OnEnable(bool enable) override {
		mCheckView.SetEnabled(enable);
	}

	void UpdateView() override {
		mCheckView.SetChecked(mbValue);
	}

	void UpdateLabel() override {
		mCheckView.SetCaption(mLabel.c_str());
	}
};

class ATUIConfDialogGenericPanel::PathView final : public StringView, public IATUIConfigPathView {
public:
	PathView() : StringView(IDD_CFGPROP_PATH) {
		mPathView.SetOnTextChanged(
			[this](VDUIProxyEditControl *c) {
				VDStringW s = c->GetText();
				if (mValue != s) {
					mValue = s;

					mpParent->OnValueChanged();
				}
			}
		);

		mBrowseView.SetOnClicked(
			[this] {
				OnBrowse();
			}
		);
	}
	
	IATUIConfigStringView& AsStringView() override { return *this; }

	IATUIConfigPathView& SetBrowseCaption(const wchar_t *caption) override {
		mBrowseCaption = caption;
		return *this;
	}
	
	IATUIConfigPathView& SetBrowseKey(uint32 key) override {
		mBrowseKey = key;
		return *this;
	}

	IATUIConfigPathView& SetSave() override {
		mbSave = true;
		return *this;
	}

	IATUIConfigPathView& SetType(const wchar_t *filter, const wchar_t *ext) override {
		const wchar_t *filterEnd = filter;
		while(*filterEnd) {
			filterEnd += wcslen(filterEnd) + 1;
		}

		mBrowseFilter.assign(filter, filterEnd);
		mBrowseExt = ext ? ext : L"";
		return *this;
	}

	IATUIConfigPathView& SetTypeImage() {
		SetBrowseKey('img ');
		return SetType(L"Supported image files\0*.png;*.jpg;*.jpeg\0All files\0*.*\0", nullptr);
	}

private:
	VDUIProxyControl mLabelView;
	VDUIProxyEditControl mPathView;
	VDUIProxyButtonControl mBrowseView;
	bool mbSave = false;
	VDStringW mBrowseFilter;
	VDStringW mBrowseExt;
	VDStringW mBrowseCaption;
	uint32 mBrowseKey = 'path';

	bool OnLoaded() override {
		AddProxy(&mLabelView, IDC_LABEL);
		AddProxy(&mPathView, IDC_PATH);
		AddProxy(&mBrowseView, IDC_BROWSE);
		mResizer.Add(mPathView.GetWindowHandle(), mResizer.kTC);
		mResizer.Add(mBrowseView.GetWindowHandle(), mResizer.kTR);
		return false;
	}

	void OnEnable(bool enable) override {
		mPathView.SetEnabled(enable);
		mBrowseView.SetEnabled(enable);
	}

	void UpdateView() override {
		mPathView.SetText(mValue.c_str());
	}

	void UpdateLabel() override {
		mLabelView.SetCaption(mLabel.c_str());
	}

	void OnBrowse() {
		const wchar_t *filter = mBrowseFilter.empty() ? L"All files\0*.*\0" : mBrowseFilter.c_str();
		const wchar_t *ext = mBrowseExt.empty() ? nullptr : mBrowseExt.c_str();

		if (mbSave)
			VDGetSaveFileName(mBrowseKey, (VDGUIHandle)mhdlg, L"Select file", filter, ext);
		else
			VDGetLoadFileName(mBrowseKey, (VDGUIHandle)mhdlg, L"Select file", filter, ext);
	}
};

////////////////////////////////////////////////////////////////////////////

ATUIConfDialogGenericPanel::ATUIConfDialogGenericPanel(IATUIConfigController& controller)
	: VDDialogFrameW32(IDD_CFGPROP_GENERIC)
	, mController(controller)
{
}

ATUIConfDialogGenericPanel::~ATUIConfDialogGenericPanel() {
	while(!mViews.empty()) {
		delete mViews.back();
		mViews.pop_back();
	}
}

IATUIConfigBoolView& ATUIConfDialogGenericPanel::AddCheckbox() {
	CheckboxView *view = new CheckboxView;
	AddView(view);

	return *view;
}

IATUIConfigPathView& ATUIConfDialogGenericPanel::AddPath() {
	PathView *view = new PathView;
	AddView(view);

	return *view;
}

void ATUIConfDialogGenericPanel::Read(const ATPropertySet& pset) {
	for(BaseView *view : mViews)
		view->Read(pset);
}

void ATUIConfDialogGenericPanel::Write(ATPropertySet& pset) const {
	for(BaseView *view : mViews)
		view->Write(pset);
}

bool ATUIConfDialogGenericPanel::OnLoaded() {
	mController.BuildDialog(*this);

	UpdateEnables();

	vdsize32 sz = GetSize();
	sz.h = DLUsToPixelSize(vdsize32(0, mYPosDLUs + 14)).h;
	SetSize(sz);

	return false;
}

void ATUIConfDialogGenericPanel::OnValueChanged() {
	UpdateEnables();
}

void ATUIConfDialogGenericPanel::UpdateEnables() {
	for(BaseView *view : mViews)
		view->UpdateEnable();
}

void ATUIConfDialogGenericPanel::AddView(BaseView *view) {
	vdautoptr view2(view);

	view->SetViewIndex(*this, (uint32)mViews.size());

	mViews.push_back(nullptr);
	mViews.back() = view2.release();

	if (view->Create(this)) {
		const uint16 id = (uint16)(kBaseId + (mViews.size() - 1));
		view->SetWindowId(id);

		sint32 htDLUs = view->GetTemplateSizeDLUs().h + 1;

		mResizer.AddWithOffsets(view->GetWindowHandle(), 7, 7 + mYPosDLUs, -7, 7 + mYPosDLUs + htDLUs, mResizer.kTC, true, true);

		mYPosDLUs += htDLUs;
	}
}

////////////////////////////////////////////////////////////////////////////

class ATUIConfDialogGeneric final : public VDDialogFrameW32, private IATUIConfigController {
public:
	ATUIConfDialogGeneric(ATPropertySet& pset, const wchar_t *caption, vdfunction<void(IATUIConfigView&)> fn);

private:
	bool OnLoaded() override;
	void OnDataExchange(bool write) override;

	void BuildDialog(IATUIConfigView& view) override;

	ATPropertySet& mPropSet;
	vdfunction<void(IATUIConfigView&)> mpConfigurator;
	VDStringW mCaption;

	ATUIConfDialogGenericPanel mPropPanel;
};

ATUIConfDialogGeneric::ATUIConfDialogGeneric(ATPropertySet& pset, const wchar_t *caption, vdfunction<void(IATUIConfigView&)> fn)
	: VDDialogFrameW32(IDD_DEVICE_GENERIC)
	, mPropSet(pset)
	, mpConfigurator(std::move(fn))
	, mCaption(caption)
	, mPropPanel(*this)
{
}

bool ATUIConfDialogGeneric::OnLoaded() {
	mResizer.Add(IDOK, mResizer.kBR);
	mResizer.Add(IDCANCEL, mResizer.kBR);

	mPropPanel.Create(this);

	vdrect32 r = GetControlPos(IDC_STATIC_LAYOUTRECT);

	mPropPanel.SetPosition(r.top_left());
	vdsize32 baseSize = GetSize();
	vdsize32 panelSize = mPropPanel.GetSize();
	SetSize(vdsize32(baseSize.w + panelSize.w - r.width(), baseSize.h + panelSize.h - r.height()));
	SetCurrentSizeAsMinSize();

	mResizer.Add(mPropPanel.GetHandleW32(), mResizer.kMC);

	SetCaption(mCaption.c_str());

	OnDataExchange(false);
	mPropPanel.Focus();
	return true;
}

void ATUIConfDialogGeneric::OnDataExchange(bool write) {
	if (write)
		mPropPanel.Write(mPropSet);
	else
		mPropPanel.Read(mPropSet);
}

void ATUIConfDialogGeneric::BuildDialog(IATUIConfigView& view) {
	mpConfigurator(view);
}

////////////////////////////////////////////////////////////////////////////

bool ATUIShowDialogGenericConfig(VDGUIHandle h, IATUIConfigController& controller) {
	ATUIConfDialogGenericPanel dlg(controller);

	return dlg.ShowDialog(h);
}

bool ATUIShowDialogGenericConfig(VDGUIHandle h, ATPropertySet& pset, const wchar_t *name, vdfunction<void(IATUIConfigView&)> fn) {
	ATUIConfDialogGeneric dlg(pset, name, fn);

	return dlg.ShowDialog(h);
}
