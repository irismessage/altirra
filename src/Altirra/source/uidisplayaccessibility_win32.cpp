//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2023 Avery Lee
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
#include <combaseapi.h>
#include <propvarutil.h>
#include <UIAutomation.h>
#include <vd2/system/binary.h>
#include <at/atcore/comsupport_win32.h>
#include <at/atui/accessibility.h>
#include "uidisplayaccessibility.h"
#include "uidisplayaccessibility_win32.h"

//#define VDTRACE_ACC(...) VDDEBUG2(__VA_ARGS__)
#define VDTRACE_ACC(...) ((void)0)

#pragma comment(lib, "uiautomationcore.lib")
#pragma comment(lib, "propsys.lib")

namespace {
	[[maybe_unused]] const char *ToString(TextUnit unit) {
		switch(unit) {
			case TextUnit_Character:	return "Character";
			case TextUnit_Format:		return "Format";
			case TextUnit_Word:			return "Word";
			case TextUnit_Line:			return "Line";
			case TextUnit_Paragraph:	return "Paragraph";
			case TextUnit_Page:			return "Page";
			case TextUnit_Document:		return "Document";
			default:					return "?";
		}
	}

	[[maybe_unused]] const char *ToString(TextPatternRangeEndpoint endpoint) {
		switch(endpoint) {
			case TextPatternRangeEndpoint_Start:	return "Start";
			case TextPatternRangeEndpoint_End:		return "End";
			default:					return "?";
		}
	}
}

////////////////////////////////////////////////////////////////////////////////

class ATUIDisplayTextProviderW32;
class ATUIDisplayAccessibilityProviderW32;

class ATUIDisplayAccessibilityProviderInterfaceW32 : public IATUIDisplayAccessibilityProviderW32 {
private:
	int AddRef() override;
	int Release() override;
};

class ATUIDisplayAccessibilityProviderW32 final : public ATCOMQIW32<ATCOMBaseW32<IRawElementProviderSimple, IInvokeProvider>, IInvokeProvider, IRawElementProviderSimple, IUnknown>, public ATUIDisplayAccessibilityProviderInterfaceW32 {
public:
	ATUIDisplayAccessibilityProviderW32(HWND hwnd, IATUIDisplayAccessibilityCallbacksW32& callbacks);

	using ATCOMQIW32::AddRef;
	using ATCOMQIW32::Release;

	IRawElementProviderSimple *AsRawElementProviderSimple() override {
		return this;
	}

	void Detach() override;

	void SetScreen(ATUIDisplayAccessibilityScreen *screenInfo) override;

	void OnGainedFocus() override;

	void SetSelection(const ATUIDisplayAccessibilityTextPoint& start, const ATUIDisplayAccessibilityTextPoint& end);

	HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions *pRetVal) override;
	HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID patternId, IUnknown **pRetVal) override;
	HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID propertyId, VARIANT *pRetVal) override;
	HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple **pRetVal) override;

	HRESULT STDMETHODCALLTYPE Invoke() override { return S_OK; }

	vdrefptr<ATUIDisplayAccessibilityScreen> mpCurrentScreen;

	HWND mhwnd;
	IATUIDisplayAccessibilityCallbacksW32& mCallbacks;
	vdrefptr<ATUIDisplayTextProviderW32> mpTextProvider;

	ATUIDisplayAccessibilityTextPoint mSelStart;
	ATUIDisplayAccessibilityTextPoint mSelEnd;
};

class ATUIDisplayTextProviderW32 final : public ATCOMQIW32<ATCOMBaseW32<ITextProvider>, ITextProvider, IUnknown> {
public:
	ATUIDisplayTextProviderW32(ATUIDisplayAccessibilityProviderW32& parent);

	HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY **pRetVal) override;
	HRESULT STDMETHODCALLTYPE GetVisibleRanges(SAFEARRAY **pRetVal) override;
	HRESULT STDMETHODCALLTYPE RangeFromChild(IRawElementProviderSimple *childElement, ITextRangeProvider **pRetVal) override;
	HRESULT STDMETHODCALLTYPE RangeFromPoint(UiaPoint point, ITextRangeProvider **pRetVal) override;
	HRESULT STDMETHODCALLTYPE get_DocumentRange(ITextRangeProvider **pRetVal) override;
	HRESULT STDMETHODCALLTYPE get_SupportedTextSelection(SupportedTextSelection *pRetVal) override;

private:
	const vdrefptr<ATUIDisplayAccessibilityProviderW32> mpParent;
};

class __declspec(uuid("{649B28C1-57BB-4212-908E-D4ED9C84C2DE}")) ATUIDisplayTextRangeProviderW32 final : public ATCOMQIW32<ATCOMBaseW32<ITextRangeProvider>, ITextRangeProvider, IUnknown, ATUIDisplayTextRangeProviderW32> {
public:
	ATUIDisplayTextRangeProviderW32(const ATUIDisplayAccessibilityTextPoint& start, const ATUIDisplayAccessibilityTextPoint& end, ATUIDisplayAccessibilityProviderW32& parent);

	HRESULT STDMETHODCALLTYPE Clone(ITextRangeProvider **pRetVal) override;
	HRESULT STDMETHODCALLTYPE Compare(ITextRangeProvider *range, BOOL *pRetVal) override;
	HRESULT STDMETHODCALLTYPE CompareEndpoints(TextPatternRangeEndpoint endpoint, ITextRangeProvider *targetRange, TextPatternRangeEndpoint targetEndpoint, int *pRetVal) override;
	HRESULT STDMETHODCALLTYPE ExpandToEnclosingUnit(TextUnit unit) override;
	HRESULT STDMETHODCALLTYPE FindAttribute( TEXTATTRIBUTEID attributeId, VARIANT val, BOOL backward, ITextRangeProvider **pRetVal) override;
	HRESULT STDMETHODCALLTYPE FindText(BSTR text, BOOL backward, BOOL ignoreCase, ITextRangeProvider **pRetVal) override;
	HRESULT STDMETHODCALLTYPE GetAttributeValue(TEXTATTRIBUTEID attributeId, VARIANT *pRetVal) override;
	HRESULT STDMETHODCALLTYPE GetBoundingRectangles(SAFEARRAY * *pRetVal) override;
	HRESULT STDMETHODCALLTYPE GetEnclosingElement(IRawElementProviderSimple **pRetVal) override;
	HRESULT STDMETHODCALLTYPE GetText(int maxLength, BSTR *pRetVal) override;
	HRESULT STDMETHODCALLTYPE Move(TextUnit unit, int count, int *pRetVal) override;
	HRESULT STDMETHODCALLTYPE MoveEndpointByUnit(TextPatternRangeEndpoint endpoint, TextUnit unit, int count, int *pRetVal) override;
	HRESULT STDMETHODCALLTYPE MoveEndpointByRange(TextPatternRangeEndpoint endpoint, ITextRangeProvider *targetRange, TextPatternRangeEndpoint targetEndpoint) override;
	HRESULT STDMETHODCALLTYPE Select() override;
	HRESULT STDMETHODCALLTYPE AddToSelection() override;
	HRESULT STDMETHODCALLTYPE RemoveFromSelection() override;
	HRESULT STDMETHODCALLTYPE ScrollIntoView(BOOL alignToTop) override;
	HRESULT STDMETHODCALLTYPE GetChildren(SAFEARRAY * *pRetVal) override;

private:
	ATUIDisplayAccessibilityTextPoint mStart;
	ATUIDisplayAccessibilityTextPoint mEnd;
	const vdrefptr<ATUIDisplayAccessibilityProviderW32> mpParent;
	int mIndent = 0;
};

////////////////////////////////////////////////////////////////////////////////

void ATUICreateDisplayAccessibilityProviderW32(HWND hwnd, IATUIDisplayAccessibilityCallbacksW32& callbacks, IATUIDisplayAccessibilityProviderW32 **provider) {
	*provider = new ATUIDisplayAccessibilityProviderW32(hwnd, callbacks);
	(*provider)->AddRef();

	ATUIAccSetUsed();
}

int ATUIDisplayAccessibilityProviderInterfaceW32::AddRef() { return static_cast<ATUIDisplayAccessibilityProviderW32 *>(this)->AddRef(); }
int ATUIDisplayAccessibilityProviderInterfaceW32::Release() { return static_cast<ATUIDisplayAccessibilityProviderW32 *>(this)->Release(); }

ATUIDisplayAccessibilityProviderW32::ATUIDisplayAccessibilityProviderW32(HWND hwnd, IATUIDisplayAccessibilityCallbacksW32& callbacks)
	: mpCurrentScreen(new ATUIDisplayAccessibilityScreen)
	, mhwnd(hwnd)
	, mCallbacks(callbacks)
	, mpTextProvider(new ATUIDisplayTextProviderW32(*this))
{
}

void ATUIDisplayAccessibilityProviderW32::Detach() {
	mhwnd = nullptr;
	mpTextProvider = nullptr;
}

void ATUIDisplayAccessibilityProviderW32::SetScreen(ATUIDisplayAccessibilityScreen *screenInfo) {
	if (!screenInfo)
		screenInfo = new ATUIDisplayAccessibilityScreen;

	mpCurrentScreen = screenInfo;

	UiaRaiseAutomationEvent(this, UIA_Text_TextChangedEventId);
}

void ATUIDisplayAccessibilityProviderW32::OnGainedFocus() {
	// This is critical for Narrator to properly allow re-focusing the element.
	// Otherwise, Narrator can fail to move its focus when the keyboard focus
	// changes. For some reason the default window provider doesn't do this.
	UiaRaiseAutomationEvent(this, UIA_AutomationFocusChangedEventId);
}

void ATUIDisplayAccessibilityProviderW32::SetSelection(const ATUIDisplayAccessibilityTextPoint& start, const ATUIDisplayAccessibilityTextPoint& end) {
	if (mSelStart != start || mSelEnd != end) {
		mSelStart = start;
		mSelEnd = end;

		UiaRaiseAutomationEvent(this, UIA_Text_TextSelectionChangedEventId);
	}
}

HRESULT STDMETHODCALLTYPE ATUIDisplayAccessibilityProviderW32::get_ProviderOptions(ProviderOptions *pRetVal) {
	if (!pRetVal)
		return E_POINTER;

	// Must allow this to be called even when detached.
	*pRetVal = ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDisplayAccessibilityProviderW32::GetPatternProvider(PATTERNID patternId, IUnknown **pRetVal) {
	if (!pRetVal)
		return E_POINTER;

	if (!mhwnd)
		return UIA_E_ELEMENTNOTAVAILABLE;

	*pRetVal = nullptr;

	if (patternId == UIA_TextPatternId) {
		*pRetVal = mpTextProvider;
		mpTextProvider->AddRef();
	}

	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDisplayAccessibilityProviderW32::GetPropertyValue(PROPERTYID propertyId, VARIANT *pRetVal) {
	if (!pRetVal)
		return E_POINTER;

	if (!mhwnd)
		return UIA_E_ELEMENTNOTAVAILABLE;

	pRetVal->vt = VT_EMPTY;

	// This is critical -- Narrator will not consistently focus a window if this
	// is not returned or we try to set it based on GetFocus(). For some reason
	// the default window provider does not work for this either.
	if (propertyId == UIA_HasKeyboardFocusPropertyId) {
		pRetVal->vt = VT_BOOL;
		pRetVal->boolVal = VARIANT_TRUE;
		return S_OK;
	}

	if (propertyId == UIA_IsKeyboardFocusablePropertyId) {
		pRetVal->vt = VT_BOOL;
		pRetVal->boolVal = VARIANT_TRUE;
		return S_OK;
	}

	if (propertyId == UIA_ControlTypePropertyId) {
		pRetVal->vt = VT_I4;
		pRetVal->lVal = UIA_DocumentControlTypeId;
		return S_OK;
	}
	
	if (propertyId == UIA_NamePropertyId) {
		pRetVal->bstrVal = SysAllocString(L"Screen");
		if (pRetVal->bstrVal)
			pRetVal->vt = VT_BSTR;

		return S_OK;
	}
	
	if (propertyId == UIA_LiveSettingPropertyId) {
		pRetVal->vt = VT_I4;
		pRetVal->lVal = LiveSetting::Polite;
		return S_OK;
	}

	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDisplayAccessibilityProviderW32::get_HostRawElementProvider(IRawElementProviderSimple **pRetVal) {
	return UiaHostProviderFromHwnd(mhwnd, pRetVal);
}

////////////////////////////////////////////////////////////////////////////////

ATUIDisplayTextProviderW32::ATUIDisplayTextProviderW32(ATUIDisplayAccessibilityProviderW32& parent)
	: mpParent(&parent)
{
}

HRESULT STDMETHODCALLTYPE ATUIDisplayTextProviderW32::GetSelection(SAFEARRAY **pRetVal) {
	if (!pRetVal)
		return E_POINTER;

	vdrefptr<ATUIDisplayTextRangeProviderW32> range(new(std::nothrow) ATUIDisplayTextRangeProviderW32(mpParent->mSelStart, mpParent->mSelEnd, *mpParent));
	if (!range)
		return E_OUTOFMEMORY;

	*pRetVal = SafeArrayCreateVector(VT_UNKNOWN, 0, 1);
	if (!*pRetVal)
		return E_OUTOFMEMORY;

	LONG index = 0;
	IUnknown *value = range;
	HRESULT hr = SafeArrayPutElement(*pRetVal, &index, value);
	if (FAILED(hr)) {
		SafeArrayDestroy(*pRetVal);
		return hr;
	}

	VDTRACE_ACC("%p GetSelection() = [(%d,%d):(%d,%d)]\n",
		this,
		mpParent->mSelStart.y,
		mpParent->mSelStart.x,
		mpParent->mSelEnd.y,
		mpParent->mSelEnd.x);

	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDisplayTextProviderW32::GetVisibleRanges(SAFEARRAY **pRetVal) {
	if (!pRetVal)
		return E_POINTER;

	*pRetVal = nullptr;

	SAFEARRAY *array = SafeArrayCreateVector(VT_UNKNOWN, 0, 1);

	if (array) {
		vdrefptr<ITextRangeProvider> range;
		get_DocumentRange(~range);

		LONG index = 0;
		HRESULT hr = SafeArrayPutElement(array, &index, range);
		if (FAILED(hr)) {
			SafeArrayDestroy(array);
			return hr;
		}
	}

	*pRetVal = array;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDisplayTextProviderW32::RangeFromChild(IRawElementProviderSimple *childElement, ITextRangeProvider **pRetVal) {
	if (!pRetVal)
		return E_POINTER;

	*pRetVal = nullptr;

	return E_INVALIDARG;
}

HRESULT STDMETHODCALLTYPE ATUIDisplayTextProviderW32::RangeFromPoint(UiaPoint point, ITextRangeProvider **pRetVal) {
	if (!pRetVal)
		return E_POINTER;

	*pRetVal = nullptr;

	if (!mpParent->mhwnd)
		return UIA_E_ELEMENTNOTAVAILABLE;

	const vdpoint32 pixelPos(
		VDRoundToInt32(point.x - 0.5),
		VDRoundToInt32(point.y - 0.5)
	);

	vdpoint32 beamPos = mpParent->mCallbacks.GetNearestBeamPositionForScreenPoint(pixelPos.x, pixelPos.y);

	// find nearest line at or below the current point
	const auto& screen = *mpParent->mpCurrentScreen;
	auto it = std::upper_bound(screen.mLines.begin(), screen.mLines.end(), beamPos.y,
		[](sint32 y, const ATUIDisplayAccessibilityLineInfo& lineInfo) {
			return y < (sint32)(lineInfo.mStartBeamY + lineInfo.mHeight);
		}
	);

	// check if below the bottom
	ATUIDisplayAccessibilityTextPoint pt;

	if (it == screen.mLines.end()) {
		pt = screen.GetDocumentEnd();
	} else {
		const auto& line = *it;

		pt.y = (sint32)(it - screen.mLines.begin());
		pt.x = std::clamp<sint32>((beamPos.x - (sint32)line.mStartBeamX) >> line.mBeamToCellShift, 0, line.mTextLength);
	}

	ATUIDisplayTextRangeProviderW32 *range = new(std::nothrow) ATUIDisplayTextRangeProviderW32(pt, pt, *mpParent);
	if (!range)
		return E_OUTOFMEMORY;

	VDTRACE_ACC("%p RangeFromPoint(%g,%g) -> [(%d,%d)-(%d,%d)]\n",
		this,
		point.x, point.y, pt.y, pt.x, pt.y, pt.x);

	range->AddRef();
	*pRetVal = range;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDisplayTextProviderW32::get_DocumentRange(ITextRangeProvider **pRetVal) {
	if (!pRetVal)
		return E_POINTER;

	ATUIDisplayTextRangeProviderW32 *range = new(std::nothrow) ATUIDisplayTextRangeProviderW32(
		ATUIDisplayAccessibilityTextPoint(),
		mpParent->mpCurrentScreen->GetDocumentEnd(),
		*mpParent
	);
	
	if (!range)
		return E_OUTOFMEMORY;

	range->AddRef();

	*pRetVal = range;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDisplayTextProviderW32::get_SupportedTextSelection(SupportedTextSelection *pRetVal) {
	if (!pRetVal)
		return E_POINTER;

	*pRetVal = SupportedTextSelection_None;
	return S_OK;
}

////////////////////////////////////////////////////////////////////////////////

ATUIDisplayTextRangeProviderW32::ATUIDisplayTextRangeProviderW32(const ATUIDisplayAccessibilityTextPoint& start, const ATUIDisplayAccessibilityTextPoint& end, ATUIDisplayAccessibilityProviderW32& parent)
	: mStart(start)
	, mEnd(end)
	, mpParent(&parent)
{
}

HRESULT STDMETHODCALLTYPE ATUIDisplayTextRangeProviderW32::Clone(ITextRangeProvider **pRetVal) {
	if (!pRetVal)
		return E_POINTER;

	ATUIDisplayTextRangeProviderW32 *other = new(std::nothrow) ATUIDisplayTextRangeProviderW32(*this);
	if (!other)
		return E_OUTOFMEMORY;

	other->AddRef();

	*pRetVal = other;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDisplayTextRangeProviderW32::Compare(ITextRangeProvider *range, BOOL *pRetVal) {
	if (!pRetVal)
		return E_POINTER;

	*pRetVal = FALSE;

	if (!range)
		return E_POINTER;

	vdrefptr<ATUIDisplayTextRangeProviderW32> other;
	if (!SUCCEEDED(range->QueryInterface(__uuidof(ATUIDisplayTextRangeProviderW32), (void **)~other)))
		return S_OK;

	if (mStart == other->mStart && mEnd == other->mEnd)
		*pRetVal = TRUE;

	VDTRACE_ACC("%*s%p [(%d,%d):(%d,%d)]->Compare(%p [(%d,%d):(%d,%d)]) = %s\n",
		mIndent,
		"",
		this,
		mStart.y,
		mStart.x,
		mEnd.y,
		mEnd.x,
		&*other,
		other->mStart.y,
		other->mStart.x,
		other->mEnd.y,
		other->mEnd.x,
		*pRetVal ? "equal" : "not equal");

	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDisplayTextRangeProviderW32::CompareEndpoints(TextPatternRangeEndpoint endpoint, ITextRangeProvider *targetRange, TextPatternRangeEndpoint targetEndpoint, int *pRetVal) {
	if (!pRetVal)
		return E_POINTER;

	*pRetVal = 0;

	if (!targetRange)
		return E_POINTER;

	vdrefptr<ATUIDisplayTextRangeProviderW32> other;
	if (!SUCCEEDED(targetRange->QueryInterface(__uuidof(ATUIDisplayTextRangeProviderW32), (void **)~other)))
		return S_OK;

	const ATUIDisplayAccessibilityTextPoint& pt1 = endpoint == TextPatternRangeEndpoint_Start ? mStart : mEnd;
	const ATUIDisplayAccessibilityTextPoint& pt2 = targetEndpoint == TextPatternRangeEndpoint_Start ? other->mStart : other->mEnd;

	auto c = pt1 <=> pt2;

	*pRetVal = (c == 0) ? 0 : (c < 0) ? -1 : 1;

	VDTRACE_ACC("%*s%p [(%d,%d):(%d,%d)]->Compare(%s, %p [(%d,%d):(%d,%d)], %s) = %d\n",
		mIndent,
		"",
		this,
		mStart.y,
		mStart.x,
		mEnd.y,
		mEnd.x,
		ToString(endpoint),
		&*other,
		other->mStart.y,
		other->mStart.x,
		other->mEnd.y,
		other->mEnd.x,
		ToString(targetEndpoint),
		*pRetVal);

	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDisplayTextRangeProviderW32::ExpandToEnclosingUnit(TextUnit unit) {
	if (!mpParent->mhwnd)
		return UIA_E_ELEMENTNOTAVAILABLE;

	const auto& screen = *mpParent->mpCurrentScreen;
	auto end = screen.GetDocumentEnd();

	[[maybe_unused]] const auto start0 = mStart;
	[[maybe_unused]] const auto end0 = mEnd;

	if (mEnd > end)
		mEnd = end;

	if (mStart > mEnd)
		mStart = mEnd;

	switch(unit) {
		case TextUnit_Character:
			mEnd = mStart;
			if (mEnd.x < (sint32)screen.mLines[mEnd.y].mTextLength)
				++mEnd.x;
			else if (mEnd.y < end.y) {
				mEnd.x = 0;
				++mEnd.y;
			}

			break;

		case TextUnit_Word:
			if (!screen.IsAtWordBoundary(mStart))
				mStart = screen.MoveToPrevWordBoundary(mStart);

			mEnd = screen.MoveToNextWordBoundary(mStart);
			break;

		case TextUnit_Line:
		case TextUnit_Paragraph:
			mStart.x = 0;
			mEnd = mStart;

			if (mEnd.y < end.y)
				++mEnd.y;
			else
				mEnd.x = screen.mLines[mEnd.y].mTextLength;
			break;

		case TextUnit_Format:
			if (screen.mFormatSpans.empty()) {
				mStart = {};
				mEnd = end;
			} else {
				uint32 startTxOffset = screen.TextPointToOffset(mStart);
				uint32 endTxOffset = screen.TextPointToOffset(mEnd);

				if (startTxOffset == endTxOffset)
					++endTxOffset;

				uint32 startFmtIdx = screen.TextOffsetToFormatIndex(startTxOffset);
				uint32 endFmtIdx = screen.TextOffsetToFormatIndex(endTxOffset);

				const auto& startFormat = screen.mFormatSpans[startFmtIdx];
				const auto& endFormat = screen.mFormatSpans[endFmtIdx];

				mStart = screen.TextOffsetToPoint(startFormat.mOffset);

				if (endFormat.mOffset != endTxOffset)
					mEnd = screen.TextOffsetToPoint(screen.mFormatSpans[endFmtIdx + 1].mOffset);
			}
			break;

		case TextUnit_Page:
		case TextUnit_Document:
			mStart = {};
			mEnd = end;
			break;

		default:
			return E_INVALIDARG;
	}

	VDTRACE_ACC("%*s%p [(%d,%d):(%d,%d)]->ExpandToEnclosingUnit(%s) = [(%d,%d):(%d,%d)]\n",
		mIndent,
		"",
		this,
		start0.y,
		start0.x,
		end0.y,
		end0.x,
		ToString(unit),
		mStart.y,
		mStart.x,
		mEnd.y,
		mEnd.x);

	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDisplayTextRangeProviderW32::FindAttribute(TEXTATTRIBUTEID attributeId, VARIANT val, BOOL backward, ITextRangeProvider **pRetVal) {
	if (!pRetVal)
		return E_POINTER;

	if (!mpParent->mhwnd)
		return UIA_E_ELEMENTNOTAVAILABLE;

	*pRetVal = nullptr;

	vdfunction<bool(const ATUIDisplayAccFormatSpan&)> pred;
	if (attributeId == UIA_ForegroundColorAttributeId) {
		if (val.vt != VT_I4)
			return E_INVALIDARG;

		const uint32 fg = VDSwizzleU32(val.lVal) >> 8;

		pred = [fg](const ATUIDisplayAccFormatSpan& fmt) {
			return fmt.mFgColor == fg;
		};
	} else if (attributeId == UIA_BackgroundColorAttributeId) {
		if (val.vt != VT_I4)
			return E_INVALIDARG;

		const uint32 bg = VDSwizzleU32(val.lVal) >> 8;

		pred = [bg](const ATUIDisplayAccFormatSpan& fmt) {
			return fmt.mBgColor == bg;
		};
	}

	if (!pred)
		return UIA_E_NOTSUPPORTED;

	const auto& screen = *mpParent->mpCurrentScreen;
	auto docEnd = screen.GetDocumentEnd();

	auto start = mStart;
	auto end = mEnd;

	if (end > docEnd)
		end = docEnd;

	if (start >= end)
		return S_OK;

	uint32 startTxOffset = screen.TextPointToOffset(start);
	uint32 endTxOffset = screen.TextPointToOffset(end);

	uint32 startFmtIdx = screen.TextOffsetToFormatIndex(startTxOffset);
	uint32 endFmtIdx = screen.TextOffsetToFormatIndex(endTxOffset - 1) + 1;

	const vdspan formatSpans(screen.mFormatSpans);
	if (backward) {
		for(; startFmtIdx != endFmtIdx; --endFmtIdx) {
			if (pred(formatSpans[endFmtIdx - 1]))
				break;
		}

		for(uint32 fmtIdx = endFmtIdx; fmtIdx != startFmtIdx; ++fmtIdx) {
			if (!pred(formatSpans[fmtIdx])) {
				startFmtIdx = fmtIdx;
				break;
			}
		}
	} else {
		for(; startFmtIdx != endFmtIdx; ++startFmtIdx) {
			if (pred(formatSpans[startFmtIdx]))
				break;
		}

		for(uint32 fmtIdx = startFmtIdx; fmtIdx != endFmtIdx; ++fmtIdx) {
			if (!pred(formatSpans[fmtIdx])) {
				endFmtIdx = fmtIdx;
				break;
			}
		}
	}

	auto newStart = screen.TextOffsetToPoint(formatSpans[startFmtIdx].mOffset);
	auto newEnd = screen.TextOffsetToPoint(formatSpans[endFmtIdx].mOffset);

	if (newStart < start)
		newStart = start;

	if (newEnd > end)
		newEnd = end;

	ATUIDisplayTextRangeProviderW32 *range = new(std::nothrow) ATUIDisplayTextRangeProviderW32(
		newStart,
		newEnd,
		*mpParent
	);

	if (!range)
		return E_OUTOFMEMORY;

	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDisplayTextRangeProviderW32::FindText(BSTR text, BOOL backward, BOOL ignoreCase, ITextRangeProvider **pRetVal) {
	if (!pRetVal)
		return E_POINTER;

	*pRetVal = nullptr;

	if (!mpParent->mhwnd)
		return UIA_E_ELEMENTNOTAVAILABLE;

	const uint32 len = (uint32)SysStringLen(text);
	if (len == 0)
		return S_OK;

	const auto& screen = *mpParent->mpCurrentScreen;
	const ATUIDisplayAccessibilityTextPoint end = screen.GetDocumentEnd();
	ATUIDisplayAccessibilityTextPoint p1 = mStart;
	ATUIDisplayAccessibilityTextPoint p2 = mEnd;

	if (p2 > end)
		p2 = end;

	if (p1 >= p2)
		return S_OK;

	size_t pos1 = screen.mLines[p1.y].mTextOffset + p1.x;
	size_t pos2 = screen.mLines[p2.y].mTextOffset + p2.x;

	if (len > pos2 - pos1)
		return S_OK;

	size_t maxOffset = (pos2 - pos1) - len;
	size_t candidatePos = backward ? maxOffset : 0;

	for(size_t i = 0; i <= maxOffset; ++i) {
		if (!memcmp(text, screen.mText.c_str() + candidatePos, len * sizeof(text[0]))) {
			ATUIDisplayAccessibilityTextPoint found1;
			for(size_t j = 0; j < candidatePos; ++j) {
				if (screen.mText[j] == '\n') {
					found1.x = 0;
					++found1.y;
				} else
					++found1.x;
			}

			ATUIDisplayAccessibilityTextPoint found2 = found1;
			for(size_t j = 0; j < len; ++j) {
				if (screen.mText[candidatePos + j] == '\n') {
					found2.x = 0;
					++found2.y;
				} else
					++found2.x;
			}

			ATUIDisplayTextRangeProviderW32 *range = new(std::nothrow) ATUIDisplayTextRangeProviderW32(
				found1,
				found2,
				*mpParent
			);

			if (!range)
				return E_OUTOFMEMORY;

			range->AddRef();
			*pRetVal = range;
			return S_OK;
		}

		candidatePos += (backward ? -1 : 1);
	}

	// not found
	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDisplayTextRangeProviderW32::GetAttributeValue(TEXTATTRIBUTEID attributeId, VARIANT *pRetVal) {
	if (!pRetVal)
		return E_POINTER;

	if (!mpParent->mhwnd)
		return UIA_E_ELEMENTNOTAVAILABLE;

	const auto& screen = *mpParent->mpCurrentScreen;
	uint32 firstTxOffset = screen.TextPointToOffset(mStart);
	uint32 lastTxOffset = screen.TextPointToOffset(mEnd);

	if (lastTxOffset > firstTxOffset)
		--lastTxOffset;

	uint32 firstFmtIdx = screen.TextOffsetToFormatIndex(firstTxOffset);
	uint32 lastFmtIdx = screen.TextOffsetToFormatIndex(lastTxOffset);

	VariantInit(pRetVal);
	bool mixed = false;

	if (attributeId == UIA_ForegroundColorAttributeId) {
		const uint32 fg = screen.mFormatSpans[firstFmtIdx].mFgColor;

		for(uint32 i = firstFmtIdx + 1; i <= lastFmtIdx; ++i) {
			if (screen.mFormatSpans[i].mFgColor != fg) {
				mixed = true;
				break;
			}
		}

		if (!mixed) {
			pRetVal->vt = VT_I4;
			pRetVal->lVal = VDSwizzleU32(fg) >> 8;
		}
	} else if (attributeId == UIA_BackgroundColorAttributeId) {
		const uint32 bg = screen.mFormatSpans[firstFmtIdx].mBgColor;

		for(uint32 i = firstFmtIdx + 1; i <= lastFmtIdx; ++i) {
			if (screen.mFormatSpans[i].mBgColor != bg) {
				mixed = true;
				break;
			}
		}

		if (!mixed) {
			pRetVal->vt = VT_I4;
			pRetVal->lVal = VDSwizzleU32(bg) >> 8;
		}
	} else {
		pRetVal->vt = VT_UNKNOWN;
		pRetVal->punkVal = nullptr;

		UiaGetReservedNotSupportedValue(&pRetVal->punkVal);
		return S_OK;
	}

	if (mixed) {
		pRetVal->vt = VT_UNKNOWN;
		pRetVal->punkVal = nullptr;

		UiaGetReservedMixedAttributeValue(&pRetVal->punkVal);
	}

	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDisplayTextRangeProviderW32::GetBoundingRectangles(SAFEARRAY **pRetVal) {
	if (!pRetVal)
		return E_POINTER;

	*pRetVal = nullptr;

	if (!mpParent->mhwnd)
		return UIA_E_ELEMENTNOTAVAILABLE;

	vdfastvector<vdrect32> rects;
	for(int y = mStart.y; y <= mEnd.y; ++y) {
		if (y >= (int)mpParent->mpCurrentScreen->mLines.size())
			break;

		const ATUIDisplayAccessibilityLineInfo& line = mpParent->mpCurrentScreen->mLines[y];

		int x1 = (y == mStart.y) ? std::min<int>(mStart.x, line.mTextLength) : 0;
		int x2 = (y == mEnd.y) ? std::min<int>(mEnd.x, line.mTextLength) : line.mTextLength;

		if (x1 < x2) {
			const wchar_t *s0 = mpParent->mpCurrentScreen->mText.c_str() + line.mTextOffset;
			const wchar_t *s1 = s0 + x1;
			const wchar_t *s2 = s0 + x2;

			const auto visiblePred = [](wchar_t c) { return c != L'{' && c != L'}'; };
			const uint32 ch1 = (uint32)std::count_if(s0, s1, visiblePred);
			const uint32 ch2 = ch1 + (uint32)std::count_if(s1, s2, visiblePred);

			vdrect32 r = mpParent->mCallbacks.GetTextSpanBoundingBox(line.mStartBeamY, ch1, ch2);

			if (r.left == r.right)
				++r.right;

			rects.push_back(r);

			VDTRACE_ACC("%*s%p [(%d,%d):(%d,%d)]->GetBoundingRectangles() = %d:(%d,%d)-(%d,%d)\n",
				mIndent,
				"",
				this,
				mStart.y,
				mStart.x,
				mEnd.y,
				mEnd.x,
				y,
				r.left, r.top, r.right, r.bottom);
		}
	}

	size_t n = rects.size();
	SAFEARRAY *array = SafeArrayCreateVector(VT_R8, 0, 4*n);

	if (!array)
		return E_OUTOFMEMORY;

	void *arrayData = nullptr;
	HRESULT hr = SafeArrayAccessData(array, &arrayData);
	if (FAILED(hr)) {
		SafeArrayDestroy(array);
		return hr;
	}

	double *arrayDst = (double *)arrayData;
	for(size_t i = 0; i < n; ++i) {
		const vdrect32& r = rects[i];

		arrayDst[0] = (double)r.left;
		arrayDst[1] = (double)r.top;
		arrayDst[2] = (double)r.width();
		arrayDst[3] = (double)r.height();
		arrayDst += 4;
	}

	SafeArrayUnaccessData(array);

	*pRetVal = array;

	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDisplayTextRangeProviderW32::GetEnclosingElement(IRawElementProviderSimple **pRetVal) {
	*pRetVal = mpParent;
	mpParent->AddRef();
	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDisplayTextRangeProviderW32::GetText(int maxLength, BSTR *pRetVal) {
	if (!pRetVal)
		return E_POINTER;

	if (!mpParent->mhwnd)
		return UIA_E_ELEMENTNOTAVAILABLE;

	size_t maxSize = maxLength < 0 ? ~(size_t)0 : (size_t)maxLength;
	const wchar_t *s = L"";
	size_t slen = 0;

	const auto& screen = *mpParent->mpCurrentScreen;
	ATUIDisplayAccessibilityTextPoint p1 = mStart;
	ATUIDisplayAccessibilityTextPoint p2 = mEnd;
	ATUIDisplayAccessibilityTextPoint pe = screen.GetDocumentEnd();

	if (p2 > pe)
		p2 = pe;

	if (p1 < p2) {
		const size_t offset1 = screen.mLines[p1.y].mTextOffset + p1.x;
		const size_t offset2 = screen.mLines[p2.y].mTextOffset + p2.x;
		s =  screen.mText.c_str() + offset1;
		slen = offset2 - offset1;
	}

	VDTRACE_ACC("%*s%p [(%d,%d):(%d,%d)]->GetText(%d) = \"%.*ls\"\n",
		mIndent,
		"",
		this,
		mStart.y,
		mStart.x,
		mEnd.y,
		mEnd.x,
		maxLength,
		slen, s);

	*pRetVal = SysAllocStringLen(s, (UINT)std::min<size_t>(slen, maxSize));
	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDisplayTextRangeProviderW32::Move(TextUnit unit, int count, int *pRetVal) {
	if (!mpParent->mhwnd)
		return UIA_E_ELEMENTNOTAVAILABLE;

	[[maybe_unused]] auto start0 = mStart;
	[[maybe_unused]] auto end0 = mEnd;

	// if the range is degenerate, just move it by the given number of units
	if (mStart == mEnd) {
		if (count > 0)
			return MoveEndpointByUnit(TextPatternRangeEndpoint_Start, unit, count, pRetVal);
		else
			return MoveEndpointByUnit(TextPatternRangeEndpoint_End, unit, count, pRetVal);
	}

	// check if we can move -- if we can't, we must not change the range at all
	const auto& screen = *mpParent->mpCurrentScreen;
	bool canMove = false;

	switch(unit) {
		case TextUnit_Character:
		case TextUnit_Word:
			if (count < 0) {
				if (mStart.x > 0 || mStart.y > 0)
					canMove = true;
			} else {
				if (mEnd < screen.GetDocumentEnd())
					canMove = true;
			}				
			break;

		case TextUnit_Format:
			if (count < 0) {
				if (screen.TextOffsetToFormatIndex(screen.TextPointToOffset(mStart)) > 0)
					canMove = true;
			} else {
				const uint32 endTxOffset = screen.TextPointToOffset(mEnd);
				const uint32 endFmtIdx = screen.TextOffsetToFormatIndex(endTxOffset);

				if (endFmtIdx < screen.mFormatSpans.size() - 1 || endTxOffset != screen.mFormatSpans[endFmtIdx].mOffset)
					canMove = true;
			}
			break;

		case TextUnit_Line:
		case TextUnit_Paragraph:
			if (count < 0) {
				if (mStart.y > 0)
					canMove = true;
			} else {
				// the range may end at the start of the last line, but that's still OK
				if (mStart.y < (sint32)screen.mLines.size() - 1)
					canMove = true;
			}
			break;

		case TextUnit_Page:
		case TextUnit_Document:
			// can never move
			break;
	}

	if (canMove) {
		// expand to enclosing unit
		++mIndent;
		HRESULT hr = ExpandToEnclosingUnit(unit);
		--mIndent;
		if (FAILED(hr))
			return hr;

		// move the head endpoint the requested number of units
		++mIndent;
		hr = MoveEndpointByUnit(count < 0 ? TextPatternRangeEndpoint_Start : TextPatternRangeEndpoint_End, unit, count, pRetVal);
		--mIndent;
		if (FAILED(hr))
			return hr;

		// snap tail to head
		if (count < 0)
			mEnd = mStart;
		else
			mStart = mEnd;

		// move the tail endpoint back a unit
		int dummy = 0;
		++mIndent;
		hr = MoveEndpointByUnit(count < 0 ? TextPatternRangeEndpoint_End : TextPatternRangeEndpoint_Start, unit, count < 0 ? 1 : -1, &dummy);
		--mIndent;
		if (FAILED(hr))
			return hr;
	}

	VDTRACE_ACC("%*s%p [(%d,%d):(%d,%d)]->Move(%s, %d) = %d:[(%d,%d):(%d,%d)]\n",
		mIndent,
		"",
		this,
		start0.y,
		start0.x,
		end0.y,
		end0.x,
		ToString(unit),
		count,
		*pRetVal,
		mStart.y,
		mStart.x,
		mEnd.y,
		mEnd.x);

	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDisplayTextRangeProviderW32::MoveEndpointByUnit(TextPatternRangeEndpoint endpoint, TextUnit unit, int count, int *pRetVal) {
	if (pRetVal)
		*pRetVal = 0;

	if (!count)
		return S_OK;

	if (!mpParent->mhwnd)
		return UIA_E_ELEMENTNOTAVAILABLE;

	[[maybe_unused]] const auto start0 = mStart;
	[[maybe_unused]] const auto end0 = mEnd;
	[[maybe_unused]] const auto count0 = count;

	const auto& screen = *mpParent->mpCurrentScreen;
	int actualSteps = 0;
	sint32 maxLine = (sint32)screen.mLines.size() - 1;

	ATUIDisplayAccessibilityTextPoint pt = (endpoint == TextPatternRangeEndpoint_Start) ? mStart : mEnd;
	ATUIDisplayAccessibilityTextPoint end = screen.GetDocumentEnd();

	if (pt > end)
		pt = end;

	switch(unit) {
		case TextUnit_Character:
			if (count > 0) {
				do {
					const auto& line = screen.mLines[pt.y];
					sint32 lineRemaining = (sint32)line.mTextLength - pt.x;

					if (count <= lineRemaining) {
						pt.x += count;
						actualSteps += count;
						break;
					}

					if (pt.y == maxLine) {
						actualSteps += lineRemaining;
						pt.x += lineRemaining;
						break;
					}

					actualSteps += lineRemaining + 1;
					count -= lineRemaining + 1;
					pt.x = 0;
					++pt.y;
				} while(count > 0);
			} else {
				do {
					sint32 lineRemaining = pt.x;

					if (lineRemaining + count >= 0) {
						// When moving backward, the actual step value is also
						// negative.
						actualSteps += count;
						pt.x += count;
						break;
					}

					if (pt.y == 0) {
						actualSteps -= lineRemaining;
						pt.x -= lineRemaining;
						break;
					}

					actualSteps -= lineRemaining + 1;
					count += lineRemaining + 1;
					--pt.y;
					pt.x = screen.mLines[pt.y].mTextLength;
				} while(count < 0);
			}
			break;

		case TextUnit_Format:
			{
				uint32 txOffset = screen.TextPointToOffset(pt);
				uint32 formatIdx = screen.TextOffsetToFormatIndex(txOffset);
				const auto& format = screen.mFormatSpans[formatIdx];

				if (count > 0) {
					uint32 fwdDist = (uint32)count;
					const uint32 n = (uint32)screen.mFormatSpans.size();

					if (n - formatIdx < fwdDist) {
						formatIdx = n;
						actualSteps = (int)(n - formatIdx);
					} else {
						formatIdx += fwdDist;
						actualSteps = count;
					}
				} else {
					uint32 revDist = (uint32)-count;

					if (txOffset != format.mOffset)
						--revDist;

					if (formatIdx < revDist) {
						actualSteps = (int)formatIdx;
						formatIdx = 0;
					} else {
						actualSteps = (int)revDist;
						formatIdx -= revDist;
					}
				}

				txOffset = screen.mFormatSpans[formatIdx].mOffset;
				pt = screen.TextOffsetToPoint(txOffset);
			}
			break;

		case TextUnit_Word:
			if (count > 0) {
				if (pt >= end)
					break;

				while(count--) {
					pt = screen.MoveToNextWordBoundary(pt);
					++actualSteps;

					if (pt == end)
						break;
				}
			} else {
				if (pt == ATUIDisplayAccessibilityTextPoint{})
					break;

				while(count++) {
					pt = screen.MoveToPrevWordBoundary(pt);
					++actualSteps;

					if (pt == ATUIDisplayAccessibilityTextPoint{})
						break;
				}
			}
			break;

		case TextUnit_Line:
		case TextUnit_Paragraph:
			if (count > 0) {
				if (count > end.y - pt.y) {
					actualSteps = end.y - pt.y + 1;
					pt = end;
				} else {
					actualSteps = count;
					pt.x = 0;
					pt.y += count;
				}
			} else {
				if (pt.x > 0) {
					pt.x = 0;
					++count;
					--actualSteps;
				}

				actualSteps += std::max<sint32>(count, -pt.y);
				pt.y = std::max<sint32>(0, pt.y + count);
			}
			break;

		case TextUnit_Page:
		case TextUnit_Document:
			if (count < 0) {
				if (pt > ATUIDisplayAccessibilityTextPoint{}) {
					pt = ATUIDisplayAccessibilityTextPoint{};
					actualSteps = 1;
				}
			} else {
				if (pt < end) {
					pt = end;
					actualSteps = 1;
				}
			}
			break;
	}

	if (endpoint == TextPatternRangeEndpoint_Start) {
		mStart = pt;

		if (mEnd < mStart)
			mEnd = pt;
	} else {
		mEnd = pt;

		if (mEnd < mStart)
			mStart = pt;
	}

	if (pRetVal)
		*pRetVal = actualSteps;

	VDTRACE_ACC("%*s%p [(%d,%d):(%d,%d)]->MoveEndpointByUnit(%s, %s, %d) = [(%d,%d):(%d,%d)],%d\n",
		mIndent,
		"",
		this,
		start0.y,
		start0.x,
		end0.y,
		end0.x,
		ToString(endpoint),
		ToString(unit),
		count0,
		mStart.y,
		mStart.x,
		mEnd.y,
		mEnd.x,
		actualSteps);

	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDisplayTextRangeProviderW32::MoveEndpointByRange(TextPatternRangeEndpoint endpoint, ITextRangeProvider *targetRange, TextPatternRangeEndpoint targetEndpoint) {
	if (!targetRange)
		return E_POINTER;

	vdrefptr<ATUIDisplayTextRangeProviderW32> other;
	if (!SUCCEEDED(targetRange->QueryInterface(__uuidof(ATUIDisplayTextRangeProviderW32), (void **)~other)))
		return E_INVALIDARG;

	const ATUIDisplayAccessibilityTextPoint srcpt = (targetEndpoint == TextPatternRangeEndpoint_End) ? other->mEnd : other->mStart;
	if (endpoint == TextPatternRangeEndpoint_Start) {
		mStart = srcpt;

		if (mEnd < mStart)
			mEnd = mStart;
	} else {
		mEnd = srcpt;

		if (mEnd < mStart)
			mStart = mEnd;
	}

	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDisplayTextRangeProviderW32::Select() {
	if (!mpParent->mhwnd)
		return UIA_E_ELEMENTNOTAVAILABLE;

	VDTRACE_ACC("%*s%p [(%d,%d):(%d,%d)]->Select()\n",
		mIndent,
		"",
		this,
		mStart.y,
		mStart.x,
		mEnd.y,
		mEnd.x);

	mpParent->SetSelection(mStart, mEnd);
	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDisplayTextRangeProviderW32::AddToSelection() {
	VDFAIL("Not implemented");
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE ATUIDisplayTextRangeProviderW32::RemoveFromSelection() {
	VDFAIL("Not implemented");
	return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE ATUIDisplayTextRangeProviderW32::ScrollIntoView(BOOL alignToTop) {
	return S_OK;
}

HRESULT STDMETHODCALLTYPE ATUIDisplayTextRangeProviderW32::GetChildren(SAFEARRAY **pRetVal) {
	if (!pRetVal)
		return E_POINTER;

	SAFEARRAYBOUND emptyBound{0, 0};
	*pRetVal = SafeArrayCreate(VT_UNKNOWN, 0, &emptyBound);

	return S_OK;
}
