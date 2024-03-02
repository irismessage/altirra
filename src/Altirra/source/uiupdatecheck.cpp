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

	void OnUpdateCheckCompleted(const ATUpdateFeedInfo *feedInfo);

	VDUIProxyRichEditControl mInfoBox;
};

ATUIDialogCheckForUpdates::ATUIDialogCheckForUpdates()
	: VDDialogFrameW32(IDD_CHECKFORUPDATES)
{
	mInfoBox.SetOnLinkSelected(
		[](const wchar_t *s) {
			ATLaunchURL(s);
			return true;
		}
	);
}

bool ATUIDialogCheckForUpdates::OnLoaded() {
	AddProxy(&mInfoBox, IDC_INFO);

	mResizer.Add(IDOK, mResizer.kBR);
	mResizer.Add(IDC_INFO, mResizer.kMC);

	if (!ATUIIsDarkThemeActive())
		mInfoBox.SetReadOnlyBackground();

	mInfoBox.DisableSelectOnFocus();

	mInfoBox.SetCaption(L"Checking for updates...");

	ATUpdateInit(*ATUIGetDispatcher(), [this](const ATUpdateFeedInfo *feedInfo) { OnUpdateCheckCompleted(feedInfo); });

	return VDDialogFrameW32::OnLoaded();
}

void ATUIDialogCheckForUpdates::OnDestroy() {
	ATUpdateShutdown();

	VDDialogFrameW32::OnDestroy();
}

void ATUIDialogCheckForUpdates::OnUpdateCheckCompleted(const ATUpdateFeedInfo *feedInfo) {
	VDStringA rtf;

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

		static constexpr char kLinkPre1[] = "{\\field {\\*\\fldinst HYPERLINK \"";
		static constexpr char kLinkPre2[] = "\"}{\\fldrslt\\ul\\cf2 ";
		static constexpr char kLinkPost[] = "}}";

		if (!feedInfo->mLatestReleaseItem.mLink.empty()) {
			mInfoBox.AppendEscapedRTF(rtf, L" - ");
			rtf += kLinkPre1;
			rtf += VDTextWToU8(feedInfo->mLatestReleaseItem.mLink);
			rtf += kLinkPre2;
			mInfoBox.AppendEscapedRTF(rtf, L"get release");
			rtf += kLinkPost;
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
