#include "stdafx.h"
#include "uionscreenkeyboard.h"
#include "uibutton.h"
#include "uimanager.h"
#include "simulator.h"
#include "pokey.h"

extern ATSimulator g_sim;

struct ATUIOnScreenKeyboard::KeyEntry {
	uint8 mScanCode;
	const wchar_t *mpNormalText;
	const wchar_t *mpShiftText;
	const wchar_t *mpControlText;
};

ATUIOnScreenKeyboard::ATUIOnScreenKeyboard()
	: mButtonWidth(32)
	, mButtonHeight(32)
{
	for(size_t i=0; i<vdcountof(mButtons); ++i)
		mButtons[i].mpButton = NULL;
}

ATUIOnScreenKeyboard::~ATUIOnScreenKeyboard() {
}

void ATUIOnScreenKeyboard::AutoSize() {
	SetSize(vdsize32(mButtonWidth * kCols, mButtonHeight * kRows));
}

void ATUIOnScreenKeyboard::OnCreate() {
	static const KeyEntry kEntries[]={
		{ 0x11, L"Help", NULL, NULL },
		{ 0x48, L"Sta", NULL, NULL },
		{ 0x49, L"Sel", NULL, NULL },
		{ 0x4A, L"Opt", NULL, NULL },
		{ 0x4B, L"Reset", NULL, NULL },

		{ 0x1C, L"Esc", NULL, NULL },
		{ 0x1F, L"1", L"!", NULL },
		{ 0x1E, L"2", L"\"", NULL },
		{ 0x1A, L"3", L"#", NULL },
		{ 0x18, L"4", L"$", NULL },
		{ 0x1D, L"5", L"%", NULL },
		{ 0x1B, L"6", L"&", NULL },
		{ 0x33, L"7", L"'", NULL },
		{ 0x35, L"8", L"@", NULL },
		{ 0x30, L"9", L"(", NULL },
		{ 0x32, L"0", L")", NULL },
		{ 0x36, L"<", L"Clr", NULL },
		{ 0x37, L">", L"Ins", NULL },
		{ 0x34, L"BkSp", L"Del", NULL },
		{ 0x40, L"Break", NULL, NULL },

		{ 0x2C, L"Tab", L"Set", L"Clr" },
		{ 0x2F, L"Q", NULL, NULL },
		{ 0x2E, L"W", NULL, NULL },
		{ 0x2A, L"E", NULL, NULL },
		{ 0x28, L"R", NULL, NULL },
		{ 0x2D, L"T", NULL, NULL },
		{ 0x2B, L"Y", NULL, NULL },
		{ 0x0B, L"U", NULL, NULL },
		{ 0x0D, L"I", NULL, NULL },
		{ 0x08, L"O", NULL, NULL },
		{ 0x0A, L"P", NULL, NULL },
		{ 0x0E, L"-", L"_", NULL },
		{ 0x0F, L"=", L"|", NULL },
		{ 0x0C, L"Return", NULL, NULL },

		{ 0x41, L"Ctrl", NULL, NULL },
		{ 0x3F, L"A", NULL, NULL },
		{ 0x3E, L"S", NULL, NULL },
		{ 0x3A, L"D", NULL, NULL },
		{ 0x38, L"F", NULL, NULL },
		{ 0x3d, L"G", NULL, NULL },
		{ 0x39, L"H", NULL, NULL },
		{ 0x01, L"J", NULL, NULL },
		{ 0x05, L"K", NULL, NULL },
		{ 0x00, L"L", NULL, NULL },
		{ 0x02, L";", L":", NULL },
		{ 0x06, L"+", L"\\", NULL },
		{ 0x07, L"*", L"^", NULL },
		{ 0x3C, L"Caps", NULL, NULL },

		{ 0x42, L"Shift", NULL, NULL },
		{ 0x17, L"Z", NULL, NULL },
		{ 0x16, L"X", NULL, NULL },
		{ 0x12, L"C", NULL, NULL },
		{ 0x10, L"V", NULL, NULL },
		{ 0x15, L"B", NULL, NULL },
		{ 0x23, L"N", NULL, NULL },
		{ 0x25, L"M", NULL, NULL },
		{ 0x20, L",", L"[", NULL },
		{ 0x22, L".", L"]", NULL },
		{ 0x26, L"/", L"?", NULL },
		{ 0x42, L"Shift", NULL, NULL },
		{ 0x27, L"Inv", NULL, NULL },

		{ 0x21, L"Space", NULL, NULL },
	};

	VDASSERTCT(vdcountof(kEntries) == vdcountof(mButtons));

	const int kRowBreaks[]={
		0,
		5,
		20,
		34,
		48,
		61,
		62
	};

	VDASSERTCT(vdcountof(kRowBreaks) == kRows + 1);

	const int bw = mButtonWidth;
	const int bh = mButtonHeight;
	int x = 0;
	int y = -1;
	bool foundShift = false;
	bool foundControl = false;

	for(int i=0; i<(int)vdcountof(mButtons); ++i) {
		if (i >= kRowBreaks[y+1]) {
			x = 0;
			++y;
		}

		ATUIButton *button = new ATUIButton;
		AddChild(button);

		ButtonEntry& entry = mButtons[i];
		entry.mpButton = button;
		entry.mX = x;
		entry.mY = y;
		entry.mpKeyEntry = &kEntries[i];

		button->SetText(entry.mpKeyEntry->mpNormalText);

		int xoffset = (bw * y) >> 2;
		vdrect32 r(xoffset + x * bw, y * bh, xoffset + (x+1) * bw, (y+1)*bh);

		if (x == 0)
			r.left = 0;

		if (i == kRowBreaks[y+1]-1)
			r.right = kCols*mButtonWidth;

		button->SetArea(r);

		button->BindAction(kATUIVK_UIAccept, ATUIButton::kActionActivate);

		button->OnPressedEvent() = ATBINDCALLBACK(this, &ATUIOnScreenKeyboard::OnButtonPressed);
		button->OnActivatedEvent() = ATBINDCALLBACK(this, &ATUIOnScreenKeyboard::OnButtonReleased);

		switch(entry.mpKeyEntry->mScanCode) {
			case 0x34:	// backspace
				BindAction(kATUIVK_UIOption, ATUIButton::kActionActivate, 0, button->GetInstanceId());
				break;

			case 0x21:	// space
				BindAction(kATUIVK_UIMenu, ATUIButton::kActionActivate, 0, button->GetInstanceId());
				break;

			case 0x41:	// control
				if (!foundControl) {
					foundControl = true;

					BindAction(kATUIVK_UIRightShift, ATUIButton::kActionActivate, 0, button->GetInstanceId());
				}
				break;

			case 0x42:	// shift
				if (!foundShift) {
					foundShift = true;

					BindAction(kATUIVK_UILeftShift, ATUIButton::kActionActivate, 0, button->GetInstanceId());
				}
				break;
		}

		++x;
	}

	for(size_t row=0; row<vdcountof(kRowBreaks)-1; ++row) {
		const int base0 = row ? kRowBreaks[row-1] : 0;
		const int base1 = kRowBreaks[row];
		const int base2 = kRowBreaks[row+1];
		const int base3 = row+2 < vdcountof(kRowBreaks) ? kRowBreaks[row+2] : base2;

		for(int i=base1; i<base2; ++i) {
			ATUIButton *btn = mButtons[i].mpButton;

			if (i != base1)
				mButtons[i].mNavOrder[0] = i-1;

			if (i+1 != base2)
				mButtons[i].mNavOrder[1] = i+1;

			if (base0 != base1)
				mButtons[i].mNavOrder[2] = std::min<int>(i - base1 + base0, base1-1);

			if (base2 != base3)
				mButtons[i].mNavOrder[3] = std::min<int>(i - base1 + base2, base3-1);
		}
	}

	BindAction(kATUIVK_UIUp, kActionUp);
	BindAction(kATUIVK_UIDown, kActionDown);
	BindAction(kATUIVK_UILeft, kActionLeft);
	BindAction(kATUIVK_UIRight, kActionRight);
}

void ATUIOnScreenKeyboard::OnDestroy() {
	for(size_t i=0; i<vdcountof(mButtons); ++i)
		mButtons[i].mpButton = NULL;

	ATUIContainer::OnDestroy();
}

void ATUIOnScreenKeyboard::OnActionStart(uint32 id) {
	if (id >= kActionCustom)
		return OnActionRepeat(id);

	return ATUIContainer::OnActionStart(id);
}

void ATUIOnScreenKeyboard::OnActionRepeat(uint32 id) {
	switch(id) {
		case kActionLeft:
		case kActionRight:
		case kActionUp:
		case kActionDown:
			{
				ATUIWidget *focus = mpManager->GetFocusWindow();

				for(size_t i=0; i<vdcountof(mButtons); ++i) {
					if (mButtons[i].mpButton == focus) {
						int j = mButtons[i].mNavOrder[id - kActionLeft];

						if (j >= 0)
							mButtons[j].mpButton->Focus();
						break;
					}
				}
			}
			break;

		default:
			return ATUIContainer::OnActionRepeat(id);
	}
}

void ATUIOnScreenKeyboard::OnButtonPressed(ATUIButton *src) {
	for(size_t i=0; i<vdcountof(mButtons); ++i) {
		if (mButtons[i].mpButton == src) {
			const KeyEntry& ke = *mButtons[i].mpKeyEntry;

			ATPokeyEmulator& pokey = g_sim.GetPokey();
			ATGTIAEmulator& gtia = g_sim.GetGTIA();

			switch(ke.mScanCode) {
				default:
					pokey.PushRawKey(ke.mScanCode + (pokey.GetControlKeyState() ? 0x80 : 0x00) + (pokey.GetShiftKeyState() ? 0x40 : 0x00));
					break;

				case 0x40:
					pokey.PushBreak();
					break;

				case 0x41:
					pokey.SetControlKeyState(true);
					UpdateLabels();
					break;

				case 0x42:
					pokey.SetShiftKeyState(true);
					UpdateLabels();
					break;

				case 0x48:
					gtia.SetConsoleSwitch(0x01, true);
					break;

				case 0x49:
					gtia.SetConsoleSwitch(0x02, true);
					break;

				case 0x4A:
					gtia.SetConsoleSwitch(0x04, true);
					break;

				case 0x4B:
					g_sim.WarmReset();
					break;
			}
			break;
		}
	}
}

void ATUIOnScreenKeyboard::OnButtonReleased(ATUIButton *src) {
	for(size_t i=0; i<vdcountof(mButtons); ++i) {
		if (mButtons[i].mpButton == src) {
			const KeyEntry& ke = *mButtons[i].mpKeyEntry;

			ATPokeyEmulator& pokey = g_sim.GetPokey();
			ATGTIAEmulator& gtia = g_sim.GetGTIA();

			switch(ke.mScanCode) {
				case 0x40:
					break;
				case 0x41:
					pokey.SetControlKeyState(false);
					UpdateLabels();
					break;
				case 0x42:
					pokey.SetShiftKeyState(false);
					UpdateLabels();
					break;
				case 0x43:
					break;
				case 0x48:
					gtia.SetConsoleSwitch(0x01, false);
					break;

				case 0x49:
					gtia.SetConsoleSwitch(0x02, false);
					break;

				case 0x4A:
					gtia.SetConsoleSwitch(0x04, false);
					break;
				default:
					pokey.ReleaseRawKey();
			}

			break;
		}
	}
}

void ATUIOnScreenKeyboard::UpdateLabels() {
	ATPokeyEmulator& pokey = g_sim.GetPokey();
	const bool shift = pokey.GetShiftKeyState();
	const bool control = pokey.GetControlKeyState();

	for(size_t i=0; i<vdcountof(mButtons); ++i) {
		const ButtonEntry& be = mButtons[i];

		if (be.mpButton) {
			const KeyEntry& ke = *be.mpKeyEntry;
			const wchar_t *s = ke.mpNormalText;

			if (control) {
				if (ke.mpControlText)
					s = ke.mpControlText;
			} else if (shift) {
				if (ke.mpShiftText)
					s = ke.mpShiftText;
			}

			be.mpButton->SetText(s);
		}
	}
}
