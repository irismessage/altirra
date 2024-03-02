//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2017 Avery Lee
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
#include <vd2/system/registry.h>
#include <vd2/system/w32assist.h>
#include <at/atnativeui/dialog.h>
#include <at/atnativeui/genericdialog.h>
#include "resource.h"

namespace {
	struct LayoutSpecs {
		vdsize32 mMinSize;
		vdsize32 mPreferredSize;
	};

	class LayoutObject {
	public:
		vdrect32 GetArea() const { return mArea; }

		void SetMargins(const vdrect32& r) { mMargins = r; }

		LayoutSpecs Measure(const vdsize32& r);
		void Arrange(const vdrect32& r);

		virtual LayoutSpecs MeasureInternal(const vdsize32& r) = 0;
		virtual void ArrangeInternal(const vdrect32& r) = 0;

	protected:
		vdrect32 mArea {};
		vdrect32 mMargins {};
	};

	LayoutSpecs LayoutObject::Measure(const vdsize32& r) {
		LayoutSpecs specs = MeasureInternal(r);
		specs.mMinSize.w += mMargins.left + mMargins.right;
		specs.mMinSize.h += mMargins.top + mMargins.bottom;
		specs.mPreferredSize.w += mMargins.left + mMargins.right;
		specs.mPreferredSize.h += mMargins.top + mMargins.bottom;

		return specs;
	}

	void LayoutObject::Arrange(const vdrect32& r) {
		mArea = r;

		vdrect32 r2 = r;

		r2.left += mMargins.left;
		r2.right = std::max<sint32>(r2.left, r2.right - mMargins.right);
		r2.top += mMargins.top;
		r2.bottom = std::max<sint32>(r2.top, r2.bottom - mMargins.bottom);

		ArrangeInternal(r2);
	}

	class LayoutCustom final : public LayoutObject {
	public:
		void Init(vdfunction<LayoutSpecs(const vdsize32&)> measure, vdfunction<void(const vdrect32&, const LayoutSpecs&)> arrange);

		LayoutSpecs MeasureInternal(const vdsize32& r) override;
		void ArrangeInternal(const vdrect32& r) override;

	private:
		vdfunction<LayoutSpecs(const vdsize32&)> mpMeasure;
		vdfunction<void(const vdrect32&, const LayoutSpecs& specs)> mpArrange;
	};

	void LayoutCustom::Init(vdfunction<LayoutSpecs(const vdsize32&)> measure, vdfunction<void(const vdrect32&, const LayoutSpecs&)> arrange) {
		mpMeasure = measure;
		mpArrange = arrange;
	}

	LayoutSpecs LayoutCustom::MeasureInternal(const vdsize32& r) {
		return mpMeasure(r);
	}

	void LayoutCustom::ArrangeInternal(const vdrect32& r) {
		mpArrange(r, mpMeasure(r.size()));
	}

	class LayoutWindow final : public LayoutObject {
	public:
		LayoutWindow() = default;
		LayoutWindow(HWND hwnd, const vdrect32f& area = { 0, 0, 0, 0 });

		void Init(HWND hwnd, const vdrect32f& area = { 0, 0, 0, 0 });
		void InitFixed(HWND hwnd, sint32 w, sint32 h, const vdrect32f& area = { 0, 0, 0, 0 });

		LayoutSpecs MeasureInternal(const vdsize32& r) override;
		void ArrangeInternal(const vdrect32& r) override;

	private:
		HWND mhwnd = nullptr;
		vdrect32f mRelArea { 0, 0, 0, 0 };
		LayoutSpecs mSpecs {};
	};

	LayoutWindow::LayoutWindow(HWND hwnd, const vdrect32f& area) {
		Init(hwnd, area);
	}

	void LayoutWindow::Init(HWND hwnd, const vdrect32f& area) {
		mhwnd = hwnd;
		mRelArea = area;

		RECT r;
		if (hwnd && GetWindowRect(hwnd, &r)) {
			mSpecs.mMinSize = { r.right - r.left, r.bottom - r.top };
			mSpecs.mPreferredSize = mSpecs.mMinSize;
		}
	}

	void LayoutWindow::InitFixed(HWND hwnd, sint32 w, sint32 h, const vdrect32f& area) {
		mhwnd = hwnd;
		mRelArea = area;
		mSpecs.mMinSize = mSpecs.mPreferredSize = { w, h };
	}

	LayoutSpecs LayoutWindow::MeasureInternal(const vdsize32& r) {
		return mSpecs;
	}

	void LayoutWindow::ArrangeInternal(const vdrect32& r) {
		if (!mhwnd)
			return;

		sint32 errX = r.width() - mSpecs.mPreferredSize.w;
		sint32 errY = r.height() - mSpecs.mPreferredSize.h;

		const sint32 x = r.left + VDRoundToInt32(mRelArea.left * (float)errX);
		const sint32 y = r.top + VDRoundToInt32(mRelArea.top * (float)errY);
		const sint32 width = mSpecs.mPreferredSize.w + VDRoundToInt32(mRelArea.width() * (float)errX);
		const sint32 height = mSpecs.mPreferredSize.h + VDRoundToInt32(mRelArea.height() * (float)errY);

		SetWindowPos(mhwnd, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
	}

	class LayoutRelative final : public LayoutObject {
	public:
		void AddChild(LayoutObject *child, const vdrect32f& area);

		LayoutSpecs MeasureInternal(const vdsize32& r) override;
		void ArrangeInternal(const vdrect32& r) override;

	private:
		struct Child {
			LayoutObject *mpObject;
			vdrect32f mRelArea;
		};

		vdfastvector<Child> mChildren;
	};

	void LayoutRelative::AddChild(LayoutObject *child, const vdrect32f& area) {
		mChildren.push_back({child, area});
	}

	LayoutSpecs LayoutRelative::MeasureInternal(const vdsize32& r) {
		return LayoutSpecs {};
	}

	void LayoutRelative::ArrangeInternal(const vdrect32& r) {
		for(const Child& child : mChildren) {
			vdrect32 rc;
			rc.left = VDRoundToInt32(r.left + (float)(r.right - r.left) * child.mRelArea.left);
			rc.right = VDRoundToInt32(r.left + (float)(r.right - r.left) * child.mRelArea.right);
			rc.top = VDRoundToInt32(r.top + (float)(r.bottom - r.top) * child.mRelArea.top);
			rc.bottom = VDRoundToInt32(r.top + (float)(r.bottom - r.top) * child.mRelArea.bottom);

			child.mpObject->Arrange(rc);
		}
	}

	class LayoutStack final : public LayoutObject {
	public:
		void Init(bool vertical, sint32 spacing);
		void AddChild(LayoutObject *child, float weight = 1);

		LayoutSpecs MeasureInternal(const vdsize32& r) override;
		void ArrangeInternal(const vdrect32& r) override;

	private:
		struct Child {
			LayoutObject *mpObject;
			float mWeight;
			sint32 mMinSize;
			sint32 mSize;
			LayoutSpecs mSpecs;
		};

		vdfastvector<Child> mChildren;
		sint32 mSpacing;
		bool mbVertical;
		float mTotalWeight = 0;
		sint32 mTotalSpacing = 0;
		LayoutSpecs mSpecs;
	};

	void LayoutStack::Init(bool vertical, sint32 spacing) {
		mbVertical = vertical;
		mSpacing = spacing;

		mChildren.clear();
		mTotalWeight = 0;
		mTotalSpacing = 0;
	}

	void LayoutStack::AddChild(LayoutObject *child, float weight) {
		if (!mChildren.empty())
			mTotalSpacing += mSpacing;

		mChildren.push_back({ child, weight });

		mTotalWeight += weight;
	}

	LayoutSpecs LayoutStack::MeasureInternal(const vdsize32& r) {
		const sint32 n = (sint32)mChildren.size();

		mSpecs = {};

		vdsize32 r2 = r;

		if (mbVertical) {
			mSpecs.mMinSize.h += mTotalSpacing;
			mSpecs.mPreferredSize.h += mTotalSpacing;

			r2.h = std::max<sint32>(0, r2.h - mTotalSpacing);
		} else {
			mSpecs.mMinSize.w += mTotalSpacing;
			mSpecs.mPreferredSize.w += mTotalSpacing;

			r2.w = std::max<sint32>(0, r2.w - mTotalSpacing);
		}

		for(Child& child : mChildren) {
			if (child.mpObject)
				child.mSpecs = child.mpObject->Measure(r2);
			else
				child.mSpecs = {};
	
			if (mbVertical) {
				mSpecs.mMinSize.w = std::max<sint32>(mSpecs.mMinSize.w, child.mSpecs.mMinSize.w);
				mSpecs.mMinSize.h += child.mSpecs.mMinSize.h;
				mSpecs.mPreferredSize.w = std::max<sint32>(mSpecs.mPreferredSize.w, child.mSpecs.mPreferredSize.w);
				mSpecs.mPreferredSize.h += child.mSpecs.mPreferredSize.h;
			} else {
				mSpecs.mMinSize.w += child.mSpecs.mMinSize.w;
				mSpecs.mMinSize.h = std::max<sint32>(mSpecs.mMinSize.h, child.mSpecs.mMinSize.h);
				mSpecs.mPreferredSize.w += child.mSpecs.mPreferredSize.w;
				mSpecs.mPreferredSize.h = std::max<sint32>(mSpecs.mPreferredSize.h, child.mSpecs.mPreferredSize.h);
			}
		}

		return mSpecs;
	}
	
	void LayoutStack::ArrangeInternal(const vdrect32& r) {
		const float weightScale = mTotalWeight > 0 ? 1.0f : 0.0f;
		const float weightAdd = mTotalWeight > 0 ? 0.0f : 1.0f;
		const float weightTotal = mTotalWeight > 0 ? mTotalWeight : (float)mChildren.size();

		VDASSERT(weightTotal > 0);

		if (mbVertical) {
			for(Child& child : mChildren) {
				child.mMinSize = child.mSpecs.mMinSize.h;
				child.mSize = child.mSpecs.mPreferredSize.h;
			}
		} else {
			for(Child& child : mChildren) {
				child.mMinSize = child.mSpecs.mMinSize.w;
				child.mSize = child.mSpecs.mPreferredSize.w;
			}
		}

		sint32 padding = mbVertical
			? std::max<sint32>(r.height(), mSpecs.mMinSize.h) - mSpecs.mPreferredSize.h
			: std::max<sint32>(r.width(), mSpecs.mMinSize.w) - mSpecs.mPreferredSize.w;

		if (padding > 0) {
			float weightLeft = weightTotal;

			for(Child& child : mChildren) {
				const LayoutSpecs& childSpecs = child.mSpecs;
				const float weight = child.mWeight * weightScale + weightAdd;

				sint32 adjust = VDRoundToInt32((float)padding * (weight / weightLeft));

				child.mSize += adjust;
				padding -= adjust;
				
				weightLeft -= weight;

				if (weightLeft <= 0)
					break;
			}
		} else if (padding < 0) {
			while(padding) {
				float weightLeft = weightTotal;
				sint32 changeCheck = 0;

				for(Child& child : mChildren) {
					const LayoutSpecs& childSpecs = child.mSpecs;
					const float weight = child.mWeight * weightScale + weightAdd;

					sint32 adjust = std::max<sint32>(child.mMinSize - child.mSize, VDRoundToInt32((float)padding * (weight / weightLeft)));

					child.mSize += adjust;
					padding -= adjust;

					changeCheck |= adjust;

					weightLeft -= weight;

					if (weightLeft <= 0)
						break;
				}

				if (!changeCheck)
					break;
			}
		}

		if (mbVertical) {
			sint32 y = r.top;

			for(Child& child : mChildren) {
				if (child.mpObject)
					child.mpObject->Arrange(vdrect32(r.left, y, r.right, y + child.mSize));

				y += child.mSize + mSpacing;
			}
		} else {
			sint32 x = r.left;

			for(Child& child : mChildren) {
				if (child.mpObject)
					child.mpObject->Arrange(vdrect32(x, r.top, x + child.mSize, r.bottom));

				x += child.mSize + mSpacing;
			}
		}
	}

	class LayoutDock final : public LayoutObject {
	public:
		enum DockLocation {
			kDockLocation_Left,
			kDockLocation_Right,
			kDockLocation_Top,
			kDockLocation_Bottom,
			kDockLocation_Fill
		};

		void AddChild(LayoutObject *child, DockLocation location);

		LayoutSpecs MeasureInternal(const vdsize32& r) override;
		void ArrangeInternal(const vdrect32& r) override;

	private:
		struct Child {
			LayoutObject *mpObject;
			DockLocation mLocation;
			LayoutSpecs mSpecs;
		};

		vdfastvector<Child> mChildren;
		LayoutSpecs mSpecs;
	};

	void LayoutDock::AddChild(LayoutObject *child, DockLocation location) {
		mChildren.push_back({ child, location });
	}

	LayoutSpecs LayoutDock::MeasureInternal(const vdsize32& r) {
		mSpecs = {};

		vdsize32 r2 = r;

		for(Child& child : mChildren) {
			child.mSpecs = child.mpObject->Measure(r2);

			switch(child.mLocation) {
				case kDockLocation_Left:
				case kDockLocation_Right:
					r2.w = std::max<sint32>(0, r2.w - child.mSpecs.mPreferredSize.w);
					break;

				case kDockLocation_Top:
				case kDockLocation_Bottom:
					r2.h = std::max<sint32>(0, r2.h - child.mSpecs.mPreferredSize.h);
					break;

				case kDockLocation_Fill:
					break;
			}
		}

		for(auto it = mChildren.rbegin(), itEnd = mChildren.rend(); it != itEnd; ++it) {
			const Child& child = *it;

			switch(child.mLocation) {
				case kDockLocation_Left:
				case kDockLocation_Right:
					mSpecs.mMinSize.w += child.mSpecs.mPreferredSize.w;
					mSpecs.mMinSize.h = std::max<sint32>(mSpecs.mMinSize.h, child.mSpecs.mPreferredSize.h);
					break;

				case kDockLocation_Top:
				case kDockLocation_Bottom:
					mSpecs.mMinSize.h += child.mSpecs.mPreferredSize.h;
					mSpecs.mMinSize.w = std::max<sint32>(mSpecs.mMinSize.w, child.mSpecs.mPreferredSize.w);
					break;

				case kDockLocation_Fill:
					mSpecs.mMinSize.w = std::max<sint32>(mSpecs.mMinSize.w, child.mSpecs.mPreferredSize.w);
					mSpecs.mMinSize.h = std::max<sint32>(mSpecs.mMinSize.h, child.mSpecs.mPreferredSize.h);
					break;
			}
		}

		mSpecs.mPreferredSize = mSpecs.mMinSize;

		return mSpecs;
	}
	
	void LayoutDock::ArrangeInternal(const vdrect32& r) {
		mSpecs = {};

		vdrect32 r2 = r;

		for(Child& child : mChildren) {
			vdrect32 r3 = r2;

			switch(child.mLocation) {
				case kDockLocation_Left:
					r3.right = std::min<sint32>(r3.right, r3.left + child.mSpecs.mPreferredSize.w);
					r2.left = r3.right;
					break;

				case kDockLocation_Right:
					r3.left = std::max<sint32>(r3.left, r3.right - child.mSpecs.mPreferredSize.w);
					r2.right = r3.left;
					break;

				case kDockLocation_Top:
					r3.bottom = std::min<sint32>(r3.bottom, r3.top + child.mSpecs.mPreferredSize.h);
					r2.top = r3.bottom;
					break;

				case kDockLocation_Bottom:
					r3.top = std::max<sint32>(r3.top, r3.bottom - child.mSpecs.mPreferredSize.h);
					r2.bottom = r3.top;
					break;

				case kDockLocation_Fill:
					break;
			}

			child.mpObject->Arrange(r3);
		}
	}
}

class ATGenericDialogW32 : public VDDialogFrameW32 {
public:
	static void SetDefaultCaption(const wchar_t *s);

	ATGenericDialogW32(const ATUIGenericDialogOptions& options);

	ATUIGenericResult GetResult() const { return mResult; }
	bool GetIgnoreEnabled() const { return mbIgnoreEnabled; }

private:
	VDZINT_PTR DlgProc(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam) override;
	bool OnLoaded() override;
	void OnDestroy() override;
	bool PreNCDestroy() override;
	bool OnErase(VDZHDC hdc) override;
	bool OnOK() override;
	bool OnCancel() override;

	bool ShouldSetDialogIcon() const override { return false; }

	static VDStringW sDefaultCaption;

	ATUIGenericDialogOptions mOptions;
	int mSplitY = 0;

	ATUIGenericResult mResult {};
	bool mbIgnoreEnabled = false;

	HFONT mhFontTitle = nullptr;

	HWND mhwndTitle = nullptr;
	HWND mhwndDisableButton = nullptr;
};

VDStringW ATGenericDialogW32::sDefaultCaption;

void ATGenericDialogW32::SetDefaultCaption(const wchar_t *s) {
	sDefaultCaption = s;
}

ATGenericDialogW32::ATGenericDialogW32(const ATUIGenericDialogOptions& options)
	: VDDialogFrameW32(IDD_GENERIC_DIALOG)
	, mOptions(options)
{
}

VDZINT_PTR ATGenericDialogW32::DlgProc(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam) {
	switch(msg) {
		case WM_CTLCOLORSTATIC:
			if ((HWND)lParam == mhwndTitle) {
				const uint32 c1 = GetSysColor(COLOR_HIGHLIGHT);
				const uint32 c2 = GetSysColor(COLOR_WINDOWTEXT);
				SetTextColor((HDC)wParam, (c1 | c2) - (((c1 ^ c2) & 0xfefefe) >> 1));
			}

			if ((HWND)lParam != mhwndDisableButton)
				return (INT_PTR)GetSysColorBrush(COLOR_WINDOW);
			break;
	}

	return VDDialogFrameW32::DlgProc(msg, wParam, lParam);
}

bool ATGenericDialogW32::OnLoaded() {
	SetCaption(mOptions.mpCaption ? mOptions.mpCaption : sDefaultCaption.c_str());
	SetControlText(IDC_GENERIC_TEXT, mOptions.mpMessage);

	const HWND hwndMessage = GetControl(IDC_GENERIC_TEXT);
	mhwndTitle = GetControl(IDC_GENERIC_TITLE);
	mhwndDisableButton = GetControl(IDC_DISABLE);

	const HFONT hFontMessage = (HFONT)SendMessage(hwndMessage, WM_GETFONT, 0, 0);
	HFONT hFontTitle = hFontMessage;

	if (mOptions.mpTitle) {
		SetControlText(IDC_GENERIC_TITLE, mOptions.mpTitle);

		LOGFONT logFont;
		if (GetObject(hFontMessage, sizeof logFont, &logFont)) {
			logFont.lfHeight = (logFont.lfHeight * 4) / 3;
			logFont.lfWidth = (logFont.lfWidth * 4) / 3;

			mhFontTitle = CreateFontIndirect(&logFont);
			if (mhFontTitle) {
				SendMessage(mhwndTitle, WM_SETFONT, (WPARAM)mhFontTitle, TRUE);

				hFontTitle = mhFontTitle;
			}
		}
	} else {
		ShowControl(IDC_GENERIC_TITLE, false);
	}

	// lookup and set icon
	bool iconVisible = true;
	LPCWSTR baseIcon = nullptr;
	SHSTOCKICONID shellIcon {};

	switch(mOptions.mIconType) {
		case kATUIGenericIconType_None:
			ShowControl(IDC_GENERIC_ICON, false);
			iconVisible = false;
			break;

		case kATUIGenericIconType_Info:
			baseIcon = IDI_INFORMATION;
			shellIcon = SIID_INFO;
			break;

		case kATUIGenericIconType_Warning:
			baseIcon = IDI_WARNING;
			shellIcon = SIID_WARNING;
			break;

		case kATUIGenericIconType_Error:
			baseIcon = IDI_ERROR;
			shellIcon = SIID_ERROR;
			break;
	}

	int titleOffsetY = 0;
	if (iconVisible) {
		HICON hIcon = LoadIcon(nullptr, baseIcon);

		if (VDIsAtLeastVistaW32()) {
			static const auto pSHGetStockIconInfo = (decltype(SHGetStockIconInfo) *)GetProcAddress(GetModuleHandle(L"shell32"), "SHGetStockIconInfo");

			if (pSHGetStockIconInfo) {
				SHSTOCKICONINFO ssii = {sizeof(SHSTOCKICONINFO)};
				HRESULT hr = pSHGetStockIconInfo(shellIcon, SHGSI_ICON | SHGSI_LARGEICON, &ssii);

				if (SUCCEEDED(hr) && ssii.hIcon)
					hIcon = ssii.hIcon;
			}
		}

		int iconWidth = 0;
		int iconHeight = 0;

		if (hIcon) {
			ICONINFO iconInfo {};
			if (GetIconInfo(hIcon, &iconInfo)) {
				BITMAP bm {};

				if (GetObject(iconInfo.hbmMask, sizeof bm, &bm)) {
					iconWidth = bm.bmWidth;
					iconHeight = bm.bmHeight;

					if (!iconInfo.hbmColor)
						iconHeight >>= 1;
				}
			}
		}

		// square off the icon
		iconWidth = iconHeight = std::max<sint32>(iconWidth, iconHeight);

		SendDlgItemMessageW(mhdlg, IDC_GENERIC_ICON, STM_SETICON, (WPARAM)hIcon, 0);

		if (HDC hdc = GetDC(mhdlg)) {
			SelectObject(hdc, hFontTitle);

			TEXTMETRIC tm;
			if (GetTextMetrics(hdc, &tm)) {
				titleOffsetY = std::max<sint32>(0, (iconHeight - (tm.tmAscent + tm.tmDescent)) / 2);
			}

			ReleaseDC(mhdlg, hdc);
		}
	}

	RECT rInsets { 0, 0, 7, 7 };
	MapDialogRect(mhdlg, &rInsets);

	const int padX = rInsets.right - rInsets.left;
	const int padY = rInsets.bottom - rInsets.top;

	LayoutWindow layoutIcon;
	LayoutWindow layoutTitle;
	LayoutCustom layoutTitleCustom;
	LayoutWindow layoutText;
	LayoutCustom layoutTextCustom;
	LayoutWindow layoutDisable;
	LayoutWindow layoutOK;
	LayoutWindow layoutCancel;
	LayoutStack layoutMainStack;
	LayoutDock layoutMessageDock;
	LayoutStack layoutOptionsStack;

	layoutText.Init(hwndMessage, vdrect32f(0, 0, 1, 1));
	layoutTitle.Init(mhwndTitle, vdrect32f(0, 0, 1, 1));

	if (mOptions.mpIgnoreTag)
		layoutDisable.Init(mhwndDisableButton, vdrect32f(0, 0.5f, 0, 0.5f));
	else
		ShowControl(IDC_DISABLE, false);

	layoutOK.Init(GetControl(IDOK), vdrect32f(0.5f, 0.5f, 0.5f, 0.5f));
	layoutCancel.Init(GetControl(IDCANCEL), vdrect32f(0.5f, 0.5f, 0.5f, 0.5f));

	const auto measureText = [](HWND hwnd, HFONT hFont, const wchar_t *s) {
		return [hwnd,hFont,str=VDStringW(s)](const vdsize32& sz) -> LayoutSpecs {
			RECT rIdeal { 0, 0, sz.w, sz.h };

			if (HDC hdc = GetDC(hwnd)) {
				SelectObject(hdc, hFont);

				DrawText(hdc, str.c_str(), (int)str.size(), &rIdeal, DT_NOPREFIX | DT_CALCRECT | DT_WORDBREAK);

				ReleaseDC(hwnd, hdc);
			}

			LayoutSpecs specs {};
			specs.mPreferredSize.w = rIdeal.right - rIdeal.left;
			specs.mPreferredSize.h = rIdeal.bottom - rIdeal.top;
			specs.mMinSize.h = rIdeal.bottom - rIdeal.top;

			return specs;
		};
	};

	const auto arrangeText = [](LayoutObject& layoutTitle) {
		return [&layoutTitle](const vdrect32& r, const LayoutSpecs& newSpecs) {
			sint32 y = (r.height() - newSpecs.mPreferredSize.h) / 2;

			vdrect32 r2 { r.left, r.top + y, r.right, r.top + y + newSpecs.mPreferredSize.h };

			layoutTitle.ArrangeInternal(r2);
		};
	};

	layoutMessageDock.SetMargins(vdrect32(padX, padY, padX, padY));

	if (iconVisible) {
		layoutIcon.Init(GetControl(IDC_GENERIC_ICON), vdrect32f(0.5f, 0.0f, 0.5f, 0.0f));
		layoutIcon.SetMargins(vdrect32(0, 0, padX, 0));
		layoutMessageDock.AddChild(&layoutIcon, LayoutDock::kDockLocation_Left);
	}

	if (mOptions.mpTitle) {
		layoutTitleCustom.SetMargins(vdrect32(0, titleOffsetY, 0, padY));
		layoutTitleCustom.Init(measureText(mhwndTitle, hFontTitle, mOptions.mpTitle), arrangeText(layoutTitle));
		layoutMessageDock.AddChild(&layoutTitleCustom, LayoutDock::kDockLocation_Top);
	}

	if (mOptions.mpMessage) {
		layoutTextCustom.SetMargins(vdrect32(0, mOptions.mpTitle ? 0 : titleOffsetY, 0, titleOffsetY));
		layoutTextCustom.Init(measureText(hwndMessage, hFontMessage, mOptions.mpMessage), arrangeText(layoutText));
		layoutMessageDock.AddChild(&layoutTextCustom, LayoutDock::kDockLocation_Fill);
	}

	layoutOptionsStack.Init(false, padX);
	layoutOptionsStack.SetMargins(vdrect32(padX, padY, padX, padY));

	if (mOptions.mpIgnoreTag)
		layoutOptionsStack.AddChild(&layoutDisable, 1);
	else
		layoutOptionsStack.AddChild(nullptr, 1);

	layoutOptionsStack.AddChild(&layoutOK, 0);
	layoutOptionsStack.AddChild(&layoutCancel, 0);

	layoutMainStack.Init(true, 0);
	layoutMainStack.AddChild(&layoutMessageDock, 1);
	layoutMainStack.AddChild(&layoutOptionsStack, 0);

	const DWORD dwStyle = GetWindowLong(mhdlg, GWL_STYLE);
	const DWORD dwExStyle = GetWindowLong(mhdlg, GWL_EXSTYLE);

	RECT rMargins {};
	AdjustWindowRectEx(&rMargins, dwStyle, FALSE, dwExStyle);

	const int marginsX = rMargins.right - rMargins.left;
	const int marginsY = rMargins.bottom - rMargins.top;

	RECT rWorkArea {};
	SystemParametersInfo(SPI_GETWORKAREA, 0, &rWorkArea, FALSE);

	LayoutWindow layoutMainWindow;
	layoutMainWindow.Init(mhdlg, vdrect32f(0, 0, 1, 1));

	const sint32 workAreaWidth = rWorkArea.right - rWorkArea.left;

	RECT rVWidth = { 0, 0, 278, 1 };
	MapDialogRect(mhdlg, &rVWidth);

	const sint32 widths[]={
		workAreaWidth,
		(workAreaWidth * 7) / 8,
		(workAreaWidth * 3) / 4,
		(workAreaWidth * 5) / 8,
		rVWidth.right - rVWidth.left
	};

	sint32 bestHeight = INT32_MAX;
	sint32 bestWidth = workAreaWidth;

	for(sint32 width : widths) {
		const LayoutSpecs& layoutSpecs = layoutMainStack.MeasureInternal({ width, rWorkArea.bottom - rWorkArea.top});

		if (bestHeight > layoutSpecs.mPreferredSize.h || (bestHeight == layoutSpecs.mPreferredSize.h && bestWidth > layoutSpecs.mPreferredSize.w)) {
			bestWidth = layoutSpecs.mPreferredSize.w;
			bestHeight = layoutSpecs.mPreferredSize.h;
		}
	}

	if (bestWidth < std::end(widths)[-1])
		bestWidth = std::end(widths)[-1];

	const LayoutSpecs& layoutSpecs = layoutMainStack.MeasureInternal({ bestWidth, bestHeight });
	layoutMainStack.ArrangeInternal(vdrect32(0, 0, bestWidth, bestHeight));

	RECT r = { 0, 0, bestWidth, bestHeight };
	AdjustWindowRectEx(&r, dwStyle, FALSE, dwExStyle);

	vdrect32 rWin(r.left, r.top, r.right, r.bottom);

	vdrect32 rCenterTarget { rWorkArea.left, rWorkArea.top, rWorkArea.right, rWorkArea.bottom };

	if (!mOptions.mCenterTarget.empty())
		rCenterTarget = mOptions.mCenterTarget;

	rWin.translate(rCenterTarget.left + ((rCenterTarget.right - rCenterTarget.left) - rWin.width()) / 2, rCenterTarget.top + ((rCenterTarget.bottom - rCenterTarget.top) - rWin.height()) / 2);
	layoutMainWindow.ArrangeInternal(rWin);

	SendMessage(mhdlg, DM_REPOSITION, 0, 0);

	mSplitY = layoutOptionsStack.GetArea().top;

	return true;
}

void ATGenericDialogW32::OnDestroy() {
	if (mOptions.mpIgnoreTag)
		mbIgnoreEnabled = IsButtonChecked(IDC_DISABLE);
}

bool ATGenericDialogW32::PreNCDestroy() {
	if (mhFontTitle) {
		DeleteObject(mhFontTitle);
		mhFontTitle = nullptr;
	}

	return VDDialogFrameW32::PreNCDestroy();
}

bool ATGenericDialogW32::OnErase(VDZHDC hdc) {
	RECT r;
	if (GetClientRect(mhdlg, &r)) {
		SetBkMode(hdc, OPAQUE);
		SetBkColor(hdc, GetSysColor(COLOR_WINDOW));

		const RECT r1 { 0, 0, r.right, mSplitY };
		ExtTextOutW(hdc, 0, 0, ETO_OPAQUE, &r1, L"", 0, nullptr);

		const RECT r2 { 0, mSplitY, r.right, r.bottom };
		SetBkColor(hdc, GetSysColor(COLOR_3DFACE));
		ExtTextOutW(hdc, 0, 0, ETO_OPAQUE, &r2, L"", 0, nullptr);
	}
	return true;
}

bool ATGenericDialogW32::OnOK() {
	mResult = kATUIGenericResult_OK;
	return false;
}

bool ATGenericDialogW32::OnCancel() {
	mResult = kATUIGenericResult_Cancel;
	return false;
}

///////////////////////////////////////////////////////////////////////////

void ATUISetDefaultGenericDialogCaption(const wchar_t *s) {
	ATGenericDialogW32::SetDefaultCaption(s);
}

void ATUIGenericDialogUndoAllIgnores() {
	VDRegistryAppKey key;
	key.removeKeyRecursive("DialogDefaults");
}

ATUIGenericResult ATUIShowGenericDialog(const ATUIGenericDialogOptions& opts) {
	static const struct ResultMapping {
		ATUIGenericResult mValue;
		const char *mpName;
	} kResultMappings[]={
		{ kATUIGenericResult_Cancel, "cancel" },
		{ kATUIGenericResult_OK, "ok" },
	};

	if (opts.mpIgnoreTag) {
		VDRegistryAppKey key("DialogDefaults", false);
		VDStringA name;

		if (key.getString(opts.mpIgnoreTag, name)) {
			ATUIGenericResult value {};
			bool valid = false;

			for(const auto& mapping : kResultMappings) {
				if (name == mapping.mpName) {
					value = mapping.mValue;
					valid = true;
					break;
				}
			}

			// At this point, we've retrieved a previously saved setting. However,
			// we need to validate it against the valid options in case it was saved
			// from a version that had different options.
			return value;
		}
	}

	ATGenericDialogW32 dlg(opts);

	dlg.ShowDialog(opts.mhParent);

	const ATUIGenericResult result = dlg.GetResult();

	if (opts.mpIgnoreTag && dlg.GetIgnoreEnabled() && (opts.mValidIgnoreMask & (UINT32_C(1) << (int)result))) {
		for(const auto& mapping : kResultMappings) {
			if (mapping.mValue == result) {
				VDRegistryAppKey key("DialogDefaults", true);

				key.setString(opts.mpIgnoreTag, mapping.mpName);
				break;
			}
		}
	}

	return result;
}

bool ATUIConfirm(VDGUIHandle hParent, const char *ignoreTag, const wchar_t *message, const wchar_t *title) {
	ATUIGenericDialogOptions opts {};

	HWND hwnd = (HWND)hParent;

	RECT rTarget;
	if (GetWindowRect(hwnd ? hwnd : GetDesktopWindow(), &rTarget))
		opts.mCenterTarget = vdrect32(rTarget.left, rTarget.top, rTarget.right, rTarget.bottom);

	if (HWND parent = GetAncestor(hwnd, GA_ROOT))
		hwnd = parent;

	opts.mhParent = (VDGUIHandle)hwnd;
	opts.mpTitle = title;
	opts.mpMessage = message;
	opts.mpIgnoreTag = ignoreTag;
	opts.mIconType = kATUIGenericIconType_Warning;
	opts.mResultMask = kATUIGenericResultMask_OKCancel;
	opts.mValidIgnoreMask = kATUIGenericResultMask_OK;

	return ATUIShowGenericDialog(opts) == kATUIGenericResult_OK;
}
