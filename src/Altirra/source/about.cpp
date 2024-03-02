//	VirtualDub - Video processing and capture application
//	Copyright (C) 1998-2001 Avery Lee
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

#include <at/atnativeui/dialog.h>
#include <at/atnativeui/theme.h>
#include <at/atnativeui/uiproxies.h>
#include <richedit.h>

#include "resource.h"
#include "oshelper.h"

#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include <vd2/system/w32assist.h>

namespace {
	void append_cooked(VDStringA& stream, const char *string, const char *stringEnd) {
		while(string != stringEnd) {
			const char *s = string;

			if (*s == '{' || *s == '\\' || *s == '}')
				stream.push_back('\\');

			++s;
			while(s != stringEnd && *s != '{' && *s != '\\' && *s != '}')
				++s;

			stream.append(string, s);
			string = s;
		}
	}

	void TextToRichTextControl(int resId, ATUINativeWindowProxy& dialog, VDUIProxyRichEditControl& richedit) {
		vdfastvector<uint8> buf;
		if (!ATLoadMiscResource(resId, buf))
			return;

		buf.push_back(0);

		const char *const title = (const char *)buf.data();
		const char *s = title;

		while(*s!='\r') ++s;

		dialog.SetCaption(VDTextU8ToW(VDStringSpanA(title, s)).c_str());

		s+=2;

		VDStringA rtf;

		rtf = 
			"{\\rtf"
			"{\\fonttbl"
				"{\\f0\\fswiss MS Shell Dlg;}"
				"{\\f1\\fnil\\fcharset2 Symbol;}"
			"}";

		const auto& tc = ATUIGetThemeColors();
		rtf.append_sprintf("{\\colortbl;\\red%u\\green%u\\blue%u;\\red%u\\green%u\\blue%u;}"
			, (tc.mContentFg >> 16) & 0xFF
			, (tc.mContentFg >>  8) & 0xFF
			, (tc.mContentFg      ) & 0xFF
			, (tc.mHeadingText >> 16) & 0xFF
			, (tc.mHeadingText >>  8) & 0xFF
			, (tc.mHeadingText      ) & 0xFF
		);

		rtf += "\\fs18\\cf1 ";
		static const char listStart[]="{\\*\\pn\\pnlvlblt\\pnindent0{\\pntxtb\\'B7}}\\fi-240\\li540 ";

		bool list_active = false;
		bool firstLine = true;

		while(*s) {
			// parse line
			int spaces = 0;

			while(*s == ' ') {
				++s;
				++spaces;
			}

			const char *end = s, *t;
			while(*end && *end != '\r' && *end != '\n')
				++end;

			// check for header, etc.
			if (*s == '[') {
				t = ++s;
				while(t != end && *t != ']')
					++t;

				rtf += "\\li300{\\cf2\\b ";
				append_cooked(rtf, s, t);
				rtf +=  "}\\par ";

				firstLine = false;
			} else {
				if (*s == '*') {
					if (!list_active) {
						list_active = true;
						rtf += listStart;
					} else
						rtf += "\\par ";

					append_cooked(rtf, s + 2, end);

					firstLine = false;
				} else if (!firstLine || s != end) {
					firstLine = false;

					if (list_active) {
						rtf.push_back(' ');
						if (s == end) {
							list_active = false;
							rtf += "\\par\\pard";
						}
					}

					if (!list_active) {
						if (spaces)
							rtf += "\\li300 ";
						else
							rtf += "\\li0{\\fs23 ";
					}

					append_cooked(rtf, s, end);

					if (!list_active) {
						if (spaces == 0)
							rtf += "}";

						rtf += "\\par ";
					}
				}
			}

			// skip line termination
			s = end;
			if (*s == '\r' || *s == '\n') {
				++s;
				if ((s[0] ^ s[-1]) == ('\r' ^ '\n'))
					++s;
			}
		}

		rtf.push_back('}');

		richedit.SetTextRTF(rtf.c_str());
		richedit.SetCaretPos(0, 0);
		richedit.Focus();
	}
}

class ATUIDialogChangeLog final : public VDResizableDialogFrameW32 {
public:
	ATUIDialogChangeLog();

private:
	bool OnLoaded();
	void OnDataExchange(bool write);

	VDUIProxyRichEditControl mTextView;
};

void ATUIDialogChangeLog::OnDataExchange(bool write) {
	if (!write) {
		TextToRichTextControl(IDR_CHANGES, *this, mTextView);
	}
}

ATUIDialogChangeLog::ATUIDialogChangeLog()
	: VDResizableDialogFrameW32(IDD_CHANGE_LOG)
{
}

bool ATUIDialogChangeLog::OnLoaded() {
	AddProxy(&mTextView, IDC_TEXT);

	mResizer.Add(mTextView.GetHandle(), mResizer.kMC | mResizer.kAvoidFlicker | mResizer.kSuppressFontChange);
	mResizer.Add(IDOK, mResizer.kBR);

	return VDResizableDialogFrameW32::OnLoaded();
}

void ATShowChangeLog(VDGUIHandle hParent) {
	ATUIDialogChangeLog dlg;
	dlg.ShowDialog(hParent);
}

///////////////////////////////////////////////////////////////////////////

class ATUIDialogCmdLineHelp final : public VDResizableDialogFrameW32 {
public:
	ATUIDialogCmdLineHelp();

private:
	bool OnLoaded() override;
	void OnSize() override;
	void OnContextMenu(uint32 id, int x, int y) override;

	void UpdateMargins();

	VDUIProxyRichEditControl mTextView;
};

ATUIDialogCmdLineHelp::ATUIDialogCmdLineHelp()
	: VDResizableDialogFrameW32(IDD_CMDLINEHELP)
{
}

bool ATUIDialogCmdLineHelp::OnLoaded() {
	SetCurrentSizeAsMinSize();

	AddProxy(&mTextView, IDC_TEXT);

	mResizer.Add(IDC_TEXT, mResizer.kMC | mResizer.kAvoidFlicker | mResizer.kSuppressFontChange);
	mResizer.Add(IDOK, mResizer.kAnchorX1_C | mResizer.kAnchorX2_C | mResizer.kAnchorY1_B | mResizer.kAnchorY2_B);

	vdfastvector<uint8> data;
	ATLoadMiscResource(IDR_CMDLINEHELP, data);

	const VDStringW& str = VDTextU8ToW(VDStringSpanA((const char *)data.begin(), (const char *)data.end()));
	VDStringA rtfStr;

	rtfStr =	"{\\rtf"
				"{\\fonttbl"
					"{\\f0\\fmodern Lucida Console;}"
				"}"
				"{\\colortbl;\\red160\\green160\\blue160;\\red248\\green248\\blue248;}"
				"\\fs18 ";

	VDStringW lineBuf;
	for(const wchar_t c : str) {
		if (c == '\r')
			continue;

		if (c == '\n') {
			// hack to find headings
			if (wcsstr(lineBuf.c_str(), L"--"))
				rtfStr += "\\cf2 ";
			else
				rtfStr += "\\cf1 ";

			mTextView.AppendEscapedRTF(rtfStr, lineBuf.c_str());
			lineBuf.clear();
			rtfStr += "\\line ";
		} else
			lineBuf.push_back(c);
	}

	rtfStr.push_back('}');
	
	mTextView.SetBackgroundColor(0);
	mTextView.SetTextRTF(rtfStr.c_str());

	UpdateMargins();

	return VDResizableDialogFrameW32::OnLoaded();
}

void ATUIDialogCmdLineHelp::OnSize() {
	VDResizableDialogFrameW32::OnSize();

	UpdateMargins();
}

void ATUIDialogCmdLineHelp::OnContextMenu(uint32 id, int x, int y) {
	static constexpr const wchar_t *kMenuItems[]={
		L"Copy\tCtrl+C",
		nullptr
	};

	if (ActivatePopupMenu(x, y, kMenuItems) >= 0) {
		if (!mTextView.IsSelectionPresent())
			mTextView.SelectAll();

		mTextView.Copy();
	}
}

void ATUIDialogCmdLineHelp::UpdateMargins() {
	mTextView.UpdateMargins(mCurrentDpi/6, mCurrentDpi/6);
}

void ATUIShowDialogCmdLineHelp(VDGUIHandle hParent) {
	ATUIDialogCmdLineHelp dlg;
	dlg.ShowDialog(hParent);
}
