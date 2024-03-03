//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2021 Avery Lee
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
#include <at/atnativeui/dialog.h>
#include <at/atnativeui/theme.h>
#include <at/atnativeui/uiproxies.h>
#include "resource.h"
#include "oshelper.h"
#include "uiaccessors.h"
#include "updatecheck.h"
#include "updatefeed.h"

class ATUIDialogCheckForUpdates final : public VDDialogFrameW32 {
public:
	ATUIDialogCheckForUpdates();

private:
	bool OnLoaded() override;
	void OnDestroy() override;

	void InitMessage(VDStringA& rtf);
	void BeginLink(VDStringA& rtf, const wchar_t *url);
	void EndLink(VDStringA& rtf);
	void AddLink(VDStringA& rtf, const wchar_t *url, const wchar_t *text);

	void SetInitialMessage();
	void CheckForUpdates();
	void OpenFeed();
	void OnUpdateCheckCompleted(const ATUpdateFeedInfo *feedInfo);

	VDUIProxyRichEditControl mInfoBox;
	VDUIProxyComboBoxControl mChannelSelector;

	static constexpr inline char kLinkPre1[] = "{\\field {\\*\\fldinst HYPERLINK \"";
	static constexpr inline char kLinkPre2[] = "\"}{\\fldrslt\\ul\\cf2 ";
	static constexpr inline char kLinkPost[] = "}}";
};

ATUIDialogCheckForUpdates::ATUIDialogCheckForUpdates()
	: VDDialogFrameW32(IDD_CHECKFORUPDATES)
{
	mInfoBox.SetOnLinkSelected(
		[this](const wchar_t *s) {
			VDStringSpanW str(s);

			if (str == L"?check")
				CheckForUpdates();
			else if (str == L"?feed")
				OpenFeed();
			else
				ATLaunchURL(s);
			return true;
		}
	);
}

bool ATUIDialogCheckForUpdates::OnLoaded() {
	AddProxy(&mInfoBox, IDC_INFO);
	AddProxy(&mChannelSelector, IDC_UPDATE_CHANNEL);

	mResizer.Add(IDOK, mResizer.kBR);
	mResizer.Add(IDC_INFO, mResizer.kMC);
	mResizer.Add(IDC_UPDATE_CHANNEL, mResizer.kBL);
	mResizer.Add(IDC_STATIC_UPDATE_CHANNEL, mResizer.kBL);

	mChannelSelector.AddItem(L"Release");
	mChannelSelector.AddItem(L"Test");
	mChannelSelector.SetSelection(ATUpdateIsTestChannelDefault() ? 1 : 0);

	if (!ATUIIsDarkThemeActive())
		mInfoBox.SetReadOnlyBackground();

	mInfoBox.DisableSelectOnFocus();

	SetInitialMessage();

	return VDDialogFrameW32::OnLoaded();
}

void ATUIDialogCheckForUpdates::OnDestroy() {
	ATUpdateShutdown();

	VDDialogFrameW32::OnDestroy();
}

void ATUIDialogCheckForUpdates::InitMessage(VDStringA& rtf) {
	rtf = 
		"{\\rtf1"
		"{\\fonttbl"
			"{\\f0\\fswiss MS Shell Dlg;}"
			"{\\f1\\fnil\\fcharset2 Symbol;}"
		"}";

	const auto& tc = ATUIGetThemeColors();
	rtf.append_sprintf(R"---({\colortbl;\red%u\green%u\blue%u;\red%u\green%u\blue%u;\red%u\green%u\blue%u;})---"
		, (tc.mContentFg >> 16) & 0xFF
		, (tc.mContentFg >>  8) & 0xFF
		, (tc.mContentFg      ) & 0xFF
		, (tc.mHyperlinkText >> 16) & 0xFF
		, (tc.mHyperlinkText >>  8) & 0xFF
		, (tc.mHyperlinkText      ) & 0xFF
		, (tc.mHeadingText >> 16) & 0xFF
		, (tc.mHeadingText >>  8) & 0xFF
		, (tc.mHeadingText      ) & 0xFF
	);

	rtf += "\\fs18\\cf1 ";
}

void ATUIDialogCheckForUpdates::BeginLink(VDStringA& rtf, const wchar_t *url) {
	rtf += kLinkPre1;
	rtf += VDTextWToU8(VDStringSpanW(url));
	rtf += kLinkPre2;
}

void ATUIDialogCheckForUpdates::EndLink(VDStringA& rtf) {
	rtf += kLinkPost;
}

void ATUIDialogCheckForUpdates::AddLink(VDStringA& rtf, const wchar_t *url, const wchar_t *text) {
	BeginLink(rtf, url);
	mInfoBox.AppendEscapedRTF(rtf, text);
	EndLink(rtf);
}

void ATUIDialogCheckForUpdates::SetInitialMessage() {
	VDStringA rtf;

	InitMessage(rtf);

	rtf += "\\sb50\\sa50 ";
	AddLink(rtf, L"?check", L"Click to check online for a newer version.");
	rtf += "\\par ";
	mInfoBox.AppendEscapedRTF(rtf, L"You can change the update channel to check via the drop-down at the bottom.");
	rtf += "\\par\\par ";
	mInfoBox.AppendEscapedRTF(rtf,
		L"The update check will fetch a static update feed. "
		L"No personal, identifying, or telemetry information "
		L"is sent during this check. If desired, you can also manually check the ");
	AddLink(rtf, L"?feed", L"RSS feed");
	
	mInfoBox.AppendEscapedRTF(rtf, L" in an RSS reader.");

	rtf += "}";

	mInfoBox.SetTextRTF(rtf.c_str());
	mInfoBox.SetCaretPos(0, 0);
}

void ATUIDialogCheckForUpdates::CheckForUpdates() {
	mInfoBox.SetCaption(L"Checking for updates...");

	ATUpdateInit(mChannelSelector.GetSelection() > 0, *ATUIGetDispatcher(), [this](const ATUpdateFeedInfo *feedInfo) { OnUpdateCheckCompleted(feedInfo); });
}

void ATUIDialogCheckForUpdates::OpenFeed() {
	ATCopyTextToClipboard(mhdlg, ATUpdateGetFeedUrl(mChannelSelector.GetSelection() > 0).c_str());

	ShowInfo2(L"The update check URL has been copied to the clipboard. Although this is only intended for the Check for Updates feature, you can paste it into any RSS 2.0 compatible feed reader.", L"Copied to clipboard");
}

void ATUIDialogCheckForUpdates::OnUpdateCheckCompleted(const ATUpdateFeedInfo *feedInfo) {
	VDStringA rtf;

	InitMessage(rtf);

	if (!feedInfo) {
		mInfoBox.AppendEscapedRTF(rtf, L"Unable to retrieve update information.");
	} else{
		rtf += "{\\sa200\\fs23\\cf3\\b ";

		if (feedInfo->mCurrentVersion >= feedInfo->mLatestVersion) {
			mInfoBox.AppendEscapedRTF(rtf, L"You have the latest version");
		} else {
			mInfoBox.AppendEscapedRTF(rtf, L"A newer version is available");
		}

		rtf += "\\par}";

		// add current version info
		rtf += "{\\fs23 ";
		mInfoBox.AppendEscapedRTF(rtf, feedInfo->mLatestReleaseItem.mTitle.c_str());
		rtf += "}";

		if (!feedInfo->mLatestReleaseItem.mLink.empty()) {
			mInfoBox.AppendEscapedRTF(rtf, L" - ");

			AddLink(rtf, feedInfo->mLatestReleaseItem.mLink.c_str(), L"get release");
		}

		rtf += "\\sb50\\sa50\\par ";

		auto& doc = feedInfo->mLatestReleaseItem.mDoc;

		struct Context {
			ATUpdateFeedNodeRef mNextNode;
			const char *mpClosingRTF;
			bool mbEatNextSpace = false;
			bool mbEatLastSpace = false;
			bool mbInlineContext = false;
		};

		vdvector<Context> elementStack;
		auto it = doc.GetRoot();

		auto elUl = doc.GetNameToken(ATXMLSubsetHashedStr("ul"));
		auto elLi = doc.GetNameToken(ATXMLSubsetHashedStr("li"));
		auto elP = doc.GetNameToken(ATXMLSubsetHashedStr("p"));
		auto elB = doc.GetNameToken(ATXMLSubsetHashedStr("b"));
		auto elI = doc.GetNameToken(ATXMLSubsetHashedStr("i"));
		auto elA = doc.GetNameToken(ATXMLSubsetHashedStr("a"));
		auto attrHREF = doc.GetNameToken(ATXMLSubsetHashedStr("href"));
		
		Context context;
		bool spacePending = false;

		VDStringW buf;
		for(;;) {
			if (!it) {
				if (elementStack.empty())
					break;

				if (spacePending) {
					if (!context.mbEatLastSpace)
						rtf += " ";

					spacePending = false;
				}

				context = elementStack.back();
				rtf += context.mpClosingRTF;
				it = context.mNextNode;
				elementStack.pop_back();
				continue;
			}

			if (it.IsCDATA()) {
				if (context.mbInlineContext) {
					const VDStringW& s = VDTextU8ToW(doc.GetText(it));
					buf.clear();
					buf.reserve(s.size());

					for(wchar_t c : s) {
						if (c == L'\t' || c == L'\n' || c == L'\r')
							c = L' ';

						if (c == L' ') {
							spacePending = true;
						} else {
							if (spacePending) {
								spacePending = false;

								if (!context.mbEatNextSpace)
									buf.push_back(L' ');
							}

							buf.push_back(c);
							context.mbEatNextSpace = false;
						}
					}

					mInfoBox.AppendEscapedRTF(rtf, buf.c_str());
				}

				++it;
			} else {
				if (spacePending) {
					if (!context.mbEatNextSpace)
						rtf += L' ';

					spacePending = false;
				}
				context.mbEatNextSpace = false;

				auto child = *it;
				elementStack.push_back(context);

				auto& savedContext = elementStack.back();
				savedContext.mNextNode = it;
				++savedContext.mNextNode;
				savedContext.mpClosingRTF = "";
				savedContext.mbEatLastSpace = false;

				if (it.IsElement(elP)) {
					savedContext.mpClosingRTF = "\\par";
					context.mbInlineContext = true;
					context.mbEatNextSpace = true;
					context.mbEatLastSpace = true;
				} else if (it.IsElement(elUl)) {
					context.mbInlineContext = false;
					context.mbEatNextSpace = true;
					context.mbEatLastSpace = true;
				} else if (it.IsElement(elLi)) {
					rtf += "{\\sb0\\sa0{\\*\\pn\\pnlvlblt\\pnindent0{\\pntxtb\\'B7}}\\fi-240\\li540 ";
					savedContext.mpClosingRTF = "\\par}";
					context.mbInlineContext = true;
					context.mbEatNextSpace = true;
					context.mbEatLastSpace = true;
				} else if (it.IsElement(elB)) {
					rtf += "{\\b ";
					savedContext.mpClosingRTF = "}";
					context.mbEatNextSpace = false;
					context.mbEatLastSpace = false;
				} else if (it.IsElement(elI)) {
					rtf += "{\\i ";
					savedContext.mpClosingRTF = "}";
					context.mbEatNextSpace = false;
					context.mbEatLastSpace = false;
				} else if (it.IsElement(elA)) {
					const VDStringSpanA href = doc.GetAttributeValue(it, attrHREF);

					rtf += kLinkPre1;
					rtf += href;
					rtf += kLinkPre2;

					savedContext.mpClosingRTF = kLinkPost;
					context.mbEatNextSpace = false;
					context.mbEatLastSpace = false;
				}

				it = child;
			}
		}
	}

	rtf += "}";

	mInfoBox.SetTextRTF(rtf.c_str());
	mInfoBox.SetCaretPos(0, 0);
}

////////////////////////////////////////////////////////////////////////////////

void ATUIShowDialogCheckForUpdates(VDGUIHandle hParent) {
	ATUIDialogCheckForUpdates dlg;
	dlg.ShowDialog(hParent);
}
