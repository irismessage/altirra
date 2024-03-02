//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2010 Avery Lee
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
#include <vd2/system/math.h>
#include "Dialog.h"
#include "resource.h"
#include "cassette.h"

class ATTapeControlDialog : public VDDialogFrameW32 {
public:
	ATTapeControlDialog(ATCassetteEmulator& tape);

protected:
	bool OnLoaded();
	void OnHScroll(uint32 id, int code);
	void UpdateLabelText();
	void AppendTime(VDStringW& s, float t);

	ATCassetteEmulator& mTape;
	VDStringW mLabel;
	float mPos;
	float mLength;
	float mSecondsPerTick;
};

ATTapeControlDialog::ATTapeControlDialog(ATCassetteEmulator& tape)
	: VDDialogFrameW32(IDD_TAPE_CONTROL)
	, mTape(tape)
{
}

bool ATTapeControlDialog::OnLoaded() {
	mPos = mTape.GetPosition();
	mLength = ceilf(mTape.GetLength());

	float r = mLength < 1e-5f ? 0.0f : mPos / mLength;
	int ticks = 100000;

	if (mLength < 10000.0f)
		ticks = VDCeilToInt(mLength * 10.0f);

	mSecondsPerTick = mLength / (float)ticks;

	TBSetRange(IDC_POSITION, 0, ticks);
	TBSetValue(IDC_POSITION, VDRoundToInt(r * (float)ticks));
	
	UpdateLabelText();
	SetFocusToControl(IDC_POSITION);
	return true;
}

void ATTapeControlDialog::OnHScroll(uint32 id, int code) {
	float pos = (float)TBGetValue(IDC_POSITION) * mSecondsPerTick;

	if (pos != mPos) {
		mPos = pos;

		mTape.SeekToTime(mPos);
		UpdateLabelText();
	}
}

void ATTapeControlDialog::UpdateLabelText() {
	mLabel.clear();
	AppendTime(mLabel, mTape.GetPosition());
	mLabel += L'/';
	AppendTime(mLabel, mTape.GetLength());

	SetControlText(IDC_STATIC_POSITION, mLabel.c_str());
}

void ATTapeControlDialog::AppendTime(VDStringW& s, float t) {
	sint32 ticks = VDRoundToInt32(t*10.0f);
	int minutesWidth = 1;

	if (ticks >= 36000) {
		sint32 hours = ticks / 36000;
		ticks %= 36000;

		s.append_sprintf(L"%d:", hours);
		minutesWidth = 2;
	}

	sint32 minutes = ticks / 600;
	ticks %= 600;

	sint32 seconds = ticks / 10;
	ticks %= 10;

	s.append_sprintf(L"%0*d:%02d.%d", minutesWidth, minutes, seconds, ticks);
}

void ATUIShowTapeControlDialog(VDGUIHandle hParent, ATCassetteEmulator& cassette) {
	ATTapeControlDialog dlg(cassette);

	dlg.ShowDialog(hParent);
}
