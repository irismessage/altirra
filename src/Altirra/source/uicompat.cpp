//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2016 Avery Lee
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
#include <at/atnativeui/uiproxies.h>
#include "compatdb.h"
#include "compatengine.h"
#include "uicompat.h"
#include "resource.h"

const wchar_t *ATUICompatGetKnownTagDisplayName(ATCompatKnownTag knownTag) {
	static constexpr const wchar_t *kKnownTagNames[] = {
		L"Requires BASIC",
		L"Requires Atari BASIC revision A",
		L"Requires Atari BASIC revision B",
		L"Requires Atari BASIC revision C",
		L"Requires BASIC disabled",
		L"Requires OS-A",
		L"Requires OS-B",
		L"Requires XL/XE OS",
		L"Requires accurate disk timing",
		L"Requires no additional CIO devices",
		L"Requires no expanded memory",
		L"Requires CTIA",
		L"Incompatible with Ultimate1MB",
		L"Requires 6502 undocumented opcodes",
		L"Incompatible with 65C816 24-bit addressing",
		L"Requires writable disk",
		L"Incompatible with floating data bus",
		L"Cart: Use 5200 8K mapper",
		L"Cart: Use 5200 one-chip 16K mapper",
		L"Cart: Use 5200 two-chip 16K mapper",
		L"Cart: Use 5200 32K mapper",
		L"Requires 60Hz (NTSC ANTIC)",
		L"Requires 50Hz (PAL ANTIC)",
	};

	static_assert(vdcountof(kKnownTagNames) == (size_t)kATCompatKnownTagCount - 1);

	const size_t index = (size_t)knownTag - 1;
	if (index < vdcountof(kKnownTagNames))
		return kKnownTagNames[index];

	return L"<Unknown tag>";
}

class ATUIDialogCompatWarning final : public VDDialogFrameW32 {
public:
	ATUIDialogCompatWarning(const ATCompatDBTitle *title, const ATCompatKnownTag *tags, size_t numTags);

private:
	bool OnLoaded() override;
	void OnDataExchange(bool write) override;

	void OnAutoAdjust();
	void OnPause();
	void OnIgnore();

	const ATCompatKnownTag *mpTags;
	const size_t mNumTags;
	const ATCompatDBTitle *mpTitle;

	VDUIProxyButtonControl mButtonAutoAdjust;
	VDUIProxyButtonControl mButtonPause;
	VDUIProxyButtonControl mButtonIgnore;
};

ATUIDialogCompatWarning::ATUIDialogCompatWarning(const ATCompatDBTitle *title, const ATCompatKnownTag *tags, size_t numTags)
	: VDDialogFrameW32(IDD_COMPATIBILITY)
	, mpTags(tags)
	, mNumTags(numTags)
	, mpTitle(title)
{
	mButtonAutoAdjust.SetOnClicked([this] { OnAutoAdjust(); });
	mButtonPause.SetOnClicked([this] { OnPause(); });
	mButtonIgnore.SetOnClicked([this] { OnIgnore(); });
}

bool ATUIDialogCompatWarning::OnLoaded() {
	AddProxy(&mButtonAutoAdjust, IDC_AUTOADJUST);
	AddProxy(&mButtonPause, IDC_PAUSE);
	AddProxy(&mButtonIgnore, IDC_IGNORE);

	VDStringW text;
	text.sprintf(L"The title \"%ls\" being booted has compatibility issues with current settings:\r\n\r\n",
		VDTextU8ToW(VDStringSpanA(mpTitle->mName.c_str())).c_str());

	for(size_t i = 0; i < mNumTags; ++i) {
		text += L"\u00A0\u00A0\u00A0\u00A0\u25CF\u00A0";
		text += ATUICompatGetKnownTagDisplayName(mpTags[i]);
		text += L"\r\n";
	}

	text += L"\r\nDo you want to automatically adjust emulation settings for better compatibility?";

	SetControlText(IDC_DIAGNOSIS, text.c_str());

	SetFocusToControl(IDC_PAUSE);
	return true;
}

void ATUIDialogCompatWarning::OnDataExchange(bool write) {
	if (write) {
		if (IsButtonChecked(IDC_IGNOREALL))
			ATCompatSetAllMuted(true);
		else if (IsButtonChecked(IDC_IGNORETHISTITLE))
			ATCompatSetTitleMuted(mpTitle, true);
	} else {
		CheckButton(IDC_IGNOREALL, ATCompatIsAllMuted());
		CheckButton(IDC_IGNORETHISTITLE, ATCompatIsTitleMuted(mpTitle));
	}
}

void ATUIDialogCompatWarning::OnAutoAdjust() {
	OnDataExchange(true);
	End(kATUICompatAction_AutoAdjust);
}

void ATUIDialogCompatWarning::OnPause() {
	OnDataExchange(true);
	End(kATUICompatAction_Pause);
}

void ATUIDialogCompatWarning::OnIgnore() {
	OnDataExchange(true);
	End(kATUICompatAction_Ignore);
}

///////////////////////////////////////////////////////////////////////////

ATUICompatAction ATUIShowDialogCompatWarning(VDGUIHandle hParent, const ATCompatDBTitle *title, const ATCompatKnownTag *tags, size_t numTags) {
	ATUIDialogCompatWarning dlg(title, tags, numTags);

	return (ATUICompatAction)dlg.ShowDialog(hParent);
}
