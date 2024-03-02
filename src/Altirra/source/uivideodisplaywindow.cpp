#include "stdafx.h"
#include <vd2/system/math.h>
#include "console.h"
#include "inputmanager.h"
#include "pokey.h"
#include "simulator.h"
#include "uicaptionupdater.h"
#include "uienhancedtext.h"
#include "uikeyboard.h"
#include "uimanager.h"
#include "uirender.h"
#include "uivideodisplaywindow.h"
#include "uionscreenkeyboard.h"

extern ATSimulator g_sim;

extern ATUIWindowCaptionUpdater g_winCaptionUpdater;

extern bool g_fullscreen;
extern bool g_mouseClipped;
extern bool g_mouseCaptured;
extern bool g_mouseAutoCapture;
extern ATUIKeyboardOptions g_kbdOpts;

void ProcessKey(char c, bool repeat, bool allowQueue, bool useCooldown);

ATUIVideoDisplayWindow::ATUIVideoDisplayWindow()
	: mLastVkeyPressed(0)
	, mLastVkeySent(0)
	, mDisplayRect(0, 0, 0, 0)
	, mbDragActive(false)
	, mDragAnchorX(0)
	, mDragAnchorY(0)
	, mbCoordIndicatorActive(false)
	, mbCoordIndicatorEnabled(false)
	, mHoverTipArea(0, 0, 0, 0)
	, mbHoverTipActive(false)
	, mpEnhTextEngine(NULL)
	, mpOSK(NULL)
	, mAllowContextMenuEvent()
	, mDisplayContextMenuEvent()
{
	mbFastClip = true;
	SetAlphaFillColor(0);
}

ATUIVideoDisplayWindow::~ATUIVideoDisplayWindow() {
}

void ATUIVideoDisplayWindow::ToggleCaptureMouse() {
	if (g_mouseCaptured)
		ReleaseMouse();
	else
		CaptureMouse();
}

void ATUIVideoDisplayWindow::ReleaseMouse() {
	ReleaseCursor();
	OnCaptureLost();
}

void ATUIVideoDisplayWindow::CaptureMouse() {
	ATInputManager *im = g_sim.GetInputManager();

	if (im->IsMouseMapped() && g_sim.IsRunning()) {
		g_mouseCaptured = true;

		if (im->IsMouseAbsoluteMode()) {
			CaptureCursor(false, true);

			g_mouseClipped = true;
		} else {
			SetCursorImage(kATUICursorImage_Hidden);
			CaptureCursor(true, false);

		}

		g_winCaptionUpdater.SetMouseCaptured(true);
	}
}

void ATUIVideoDisplayWindow::Copy() {
	if (mDragPreviewSpans.empty())
		return;

	ATAnticEmulator& antic = g_sim.GetAntic();
	const ATAnticEmulator::DLHistoryEntry *dlhist = antic.GetDLHistory();

	char data[48];

	VDStringA s;

	for(TextSpans::const_iterator it(mDragPreviewSpans.begin()), itEnd(mDragPreviewSpans.end());
		it != itEnd;
		++it)
	{
		const TextSpan& ts = *it;

		int actual = ReadText(data, ts.mY, ts.mCharX, ts.mCharWidth);

		if (!actual)
			continue;

		int base = 0;
		while(base < actual && data[base] == ' ')
			++base;

		while(actual > base && data[actual - 1] == ' ')
			--actual;

		s.append(data + base, data + actual);

		if (&*it != &mDragPreviewSpans.back())
			s += "\r\n";
	}

	mpManager->GetClipboard()->CopyText(s.c_str());
}

void ATUIVideoDisplayWindow::SetDisplayRect(const vdrect32& r) {
	mDisplayRect = r;
	UpdateDragPreviewRects();
}

void ATUIVideoDisplayWindow::ClearXorRects() {
	if (!mXorRects.empty()) {
		mXorRects.clear();
		Invalidate();
	}
}

void ATUIVideoDisplayWindow::AddXorRect(int x1, int y1, int x2, int y2) {
	XorRect& xr = mXorRects.push_back();
	xr.mX1 = x1;
	xr.mY1 = y1;
	xr.mX2 = x2;
	xr.mY2 = y2;

	Invalidate();
}

void ATUIVideoDisplayWindow::OnMouseDown(sint32 x, sint32 y, uint32 vk, bool dblclk) {
	ATInputManager *im = g_sim.GetInputManager();

	Focus();

	// If the mouse is mapped, it gets first crack at inputs.
	if (im->IsMouseMapped() && g_sim.IsRunning()) {

		// Check if auto-capture is on and we haven't captured the mouse yet. If so, we
		// should capture the mouse but otherwise eat the click
		if (g_mouseAutoCapture && !g_mouseCaptured) {
			if (vk == kATUIVK_LButton) {
				CaptureMouse();
				return;
			}
		} else {
			const bool absMode = im->IsMouseAbsoluteMode();

			// Check if the mouse is captured or we are in absolute mode. If we are in
			// relative mode and haven't captured the mouse we should not route this
			// shunt to the input manager.
			if (g_mouseCaptured || absMode) {
				if (absMode)
					UpdateMousePosition(x, y);

				switch(vk) {
					case kATUIVK_LButton:
						im->OnButtonDown(0, kATInputCode_MouseLMB);
						break;

					case kATUIVK_MButton:
						im->OnButtonDown(0, kATInputCode_MouseMMB);
						break;

					case kATUIVK_RButton:
						im->OnButtonDown(0, kATInputCode_MouseRMB);
						break;

					case kATUIVK_XButton1:
						im->OnButtonDown(0, kATInputCode_MouseX1B);
						break;

					case kATUIVK_XButton2:
						im->OnButtonDown(0, kATInputCode_MouseX2B);
						break;
				}

				return;
			}
		}
	}
	
	// We aren't routing this mouse event to the input manager, so do selection if it's the
	// LMB.

	if (vk == kATUIVK_LButton) {
		mbDragActive = MapPixelToBeamPosition(x, y, mDragAnchorX, mDragAnchorY, true)
			&& GetModeLineYPos(mDragAnchorY, true) >= 0;

		if (mbDragActive) {
			SetCursorImage(kATUICursorImage_IBeam);
			CaptureCursor();
		}
	}
}

void ATUIVideoDisplayWindow::OnMouseUp(sint32 x, sint32 y, uint32 vk) {
	if (vk == kATUIVK_LButton && mbDragActive) {
		mbDragActive = false;

		ReleaseCursor();
		UpdateDragPreview(x, y);
		return;
	}

	ATInputManager *im = g_sim.GetInputManager();

	if (g_mouseCaptured || im->IsMouseAbsoluteMode()) {
		if (im->IsMouseMapped()) {
			switch(vk) {
				case kATUIVK_LButton:
					im->OnButtonUp(0, kATInputCode_MouseLMB);
					break;
				case kATUIVK_RButton:
					im->OnButtonUp(0, kATInputCode_MouseRMB);
					break;
				case kATUIVK_MButton:
					im->OnButtonUp(0, kATInputCode_MouseMMB);
					break;
				case kATUIVK_XButton1:
					im->OnButtonUp(0, kATInputCode_MouseX1B);
					break;
				case kATUIVK_XButton2:
					im->OnButtonUp(0, kATInputCode_MouseX2B);
					break;
			}

			// Eat the message to prevent a context menu.
			return;
		}
	}

	if (vk == kATUIVK_RButton) {
		if (mAllowContextMenuEvent)
			mAllowContextMenuEvent();
	}
}

void ATUIVideoDisplayWindow::OnMouseRelativeMove(sint32 dx, sint32 dy) {
	ATInputManager *im = g_sim.GetInputManager();

	im->OnMouseMove(0, dx, dy);
	SetCursorImage(kATUICursorImage_Hidden);
}

void ATUIVideoDisplayWindow::OnMouseMove(sint32 x, sint32 y) {
	// If we have already entered a selection drag, it has highest priority.
	if (mbDragActive) {
		SetCursorImage(kATUICursorImage_IBeam);
		UpdateDragPreview(x, y);
		return;
	}

	// Check if we're stopped and should do debug queries.
	if (mpManager->IsKeyDown(kATUIVK_Shift) && !g_sim.IsRunning() && ATIsDebugConsoleActive() && HasFocus()) {
		SetCoordinateIndicator(x, y);
		SetCursorImage(kATUICursorImage_Cross);
		return;
	}

	SetCursorImage(ComputeCursorImage(vdpoint32(x, y)));

	if ((g_mouseCaptured || !g_mouseAutoCapture) && g_sim.GetInputManager()->IsMouseAbsoluteMode()) {
		UpdateMousePosition(x, y);
	} else if (mbHoverTipActive) {
		if (!mHoverTipArea.contains(vdpoint32(x, y))) {
			g_sim.GetUIRenderer()->SetHoverTip(0, 0, NULL);
			mbHoverTipActive = false;
		}
	}
}

void ATUIVideoDisplayWindow::OnMouseLeave() {
	ClearCoordinateIndicator();

	if (mbHoverTipActive)
		g_sim.GetUIRenderer()->SetHoverTip(0, 0, NULL);
}

void ATUIVideoDisplayWindow::OnMouseHover(sint32 x, sint32 y) {
	if (g_mouseCaptured)
		return;

	if (mpManager->IsKeyDown(kATUIVK_Shift)) {
		// don't process if we're in query mode
		if (!g_sim.IsRunning() && ATIsDebugConsoleActive())
			return;

		// tooltip request -- let's try to grab text
		int xc;
		int yc;

		bool valid = false;

		if (MapPixelToBeamPosition(x, y, xc, yc, false)) {
			// attempt to copy out text
			int ymode = GetModeLineYPos(yc, true);

			if (ymode >= 0) {
				char data[49];

				int actual = ReadText(data, ymode, 0, 48);

				// trim and null-terminate the string
				char *s = data;

				while(*s == ' ')
					++s;

				char *t = s + strlen(s);

				while(t != s && t[-1] == ' ')
					--t;

				*t = 0;

				// convert to uppercase
				for(char *s2 = s; *s2; ++s2)
					*s2 = toupper((unsigned char)*s2);

				// look for an error
				t = strstr(s, "ERROR");

				VDStringW msg;
				if (t) {
					// skip ERROR string
					t += 5;

					// skip blanks
					while(*t == ' ')
						++t;

					// look for an optional dash or pound
					if (*t == '#' || *t == '-') {
						++t;

						// skip more blanks
						while(*t == ' ')
							++t;
					}

					// look for a digit
					if ((unsigned char)(*t - '0') < 10) {
						int errCode = atoi(t);

						if (errCode >= 2 && errCode <= 255) {
							msg.sprintf(L"<b>Error %u</b>\n", errCode);

							switch(errCode) {
								case 2: msg += L"<b>Atari BASIC:</b> Out of memory"; break;
								case 3: msg += L"<b>Atari BASIC:</b> Value error"; break;
								case 4: msg += L"<b>Atari BASIC:</b> Too many variables"; break;
								case 5: msg += L"<b>Atari BASIC:</b> String length error"; break;
								case 6: msg += L"<b>Atari BASIC:</b> Out of data"; break;
								case 7: msg += L"<b>Atari BASIC:</b> Number &gt;32767"; break;
								case 8: msg += L"<b>Atari BASIC:</b> Input statement error"; break;
								case 9: msg += L"<b>Atari BASIC:</b> DIM error"; break;
								case 10: msg += L"<b>Atari BASIC:</b> Argument stack overflow"; break;
								case 11: msg += L"<b>Atari BASIC:</b> Floating point overflow/underflow"; break;
								case 12: msg += L"<b>Atari BASIC:</b> Line not found"; break;
								case 13: msg += L"<b>Atari BASIC:</b> No matching FOR statement"; break;
								case 14: msg += L"<b>Atari BASIC:</b> Line too long"; break;
								case 15: msg += L"<b>Atari BASIC:</b> GOSUB or FOR line deleted"; break;
								case 16: msg += L"<b>Atari BASIC:</b> RETURN error"; break;
								case 17: msg += L"<b>Atari BASIC:</b> Garbage error"; break;
								case 18: msg += L"<b>Atari BASIC:</b> Invalid string character"; break;
								case 19: msg += L"<b>Atari BASIC:</b> LOAD program too long"; break;
								case 20: msg += L"<b>Atari BASIC:</b> Device number error"; break;
								case 21: msg += L"<b>Atari BASIC:</b> LOAD file error"; break;

								case 128: msg += L"<b>CIO:</b> User break abort"; break;
								case 129: msg += L"<b>CIO:</b> IOCB in use"; break;
								case 130: msg += L"<b>CIO:</b> Unknown device"; break;
								case 131: msg += L"<b>CIO:</b> IOCB write only"; break;
								case 132: msg += L"<b>CIO:</b> Invalid command"; break;
								case 133: msg += L"<b>CIO:</b> IOCB not open"; break;
								case 134: msg += L"<b>CIO:</b> Invalid IOCB"; break;
								case 135: msg += L"<b>CIO:</b> IOCB read only"; break;
								case 136: msg += L"<b>CIO:</b> End of file"; break;
								case 137: msg += L"<b>CIO:</b> Truncated record"; break;
								case 138: msg += L"<b>CIO/SIO:</b> Timeout"; break;
								case 139: msg += L"<b>CIO/SIO:</b> Device NAK"; break;
								case 140: msg += L"<b>CIO/SIO:</b> Bad frame"; break;
								case 142: msg += L"<b>CIO/SIO:</b> Serial input overrun"; break;
								case 143: msg += L"<b>CIO/SIO:</b> Checksum error"; break;
								case 144: msg += L"<b>CIO/SIO:</b> Device error or write protected disk"; break;
								case 145: msg += L"<b>CIO:</b> Bad screen mode"; break;
								case 146: msg += L"<b>CIO:</b> Not supported"; break;
								case 147: msg += L"<b>CIO:</b> Out of memory"; break;

								case 160: msg += L"<b>DOS:</b> Invalid drive number"; break;
								case 161: msg += L"<b>DOS:</b> Too many open files"; break;
								case 162: msg += L"<b>DOS:</b> Disk full"; break;
								case 163: msg += L"<b>DOS:</b> Fatal disk I/O error"; break;
								case 164: msg += L"<b>DOS:</b> File number mismatch"; break;
								case 165: msg += L"<b>DOS:</b> File name error"; break;
								case 166: msg += L"<b>DOS:</b> POINT data length error"; break;
								case 167: msg += L"<b>DOS:</b> File locked"; break;
								case 168: msg += L"<b>DOS:</b> Command invalid"; break;
								case 169: msg += L"<b>DOS:</b> Directory full"; break;
								case 170: msg += L"<b>DOS:</b> File not found"; break;
								case 171: msg += L"<b>DOS:</b> Invalid POINT"; break;
							}
						}
					}
				}

				if (msg.empty() && *s)
					msg = L"There is no help for this message.\nHover over an SIO, CIO, BASIC, or DOS error message for help.";

				if (!msg.empty()) {
					int xp1, xp2, yp1, yp2;
					MapBeamPositionToPixel(0, ymode, xp1, yp1);

					ATAnticEmulator& antic = g_sim.GetAntic();
					const ATAnticEmulator::DLHistoryEntry *dlhist = antic.GetDLHistory();
					while(++ymode < 248 && !dlhist[ymode].mbValid)
						;

					MapBeamPositionToPixel(228, ymode, xp2, yp2);

					mHoverTipArea.set(xp1, yp1, xp2, yp2);

					g_sim.GetUIRenderer()->SetHoverTip(x, y, msg.c_str());
					mbHoverTipActive = true;
					valid = true;
				}
			}
		}

		if (!valid) {
			g_sim.GetUIRenderer()->SetHoverTip(x, y, NULL);
			mbHoverTipActive = false;
		}
	} else {
		SetCursorImage(kATUICursorImage_Hidden);
	}
}

bool ATUIVideoDisplayWindow::OnContextMenu(const vdpoint32 *pt) {
	// For now we do a bit of a hack and let the top-level native display code handle this,
	// as it is too hard currently to display the menu here.
	if (mDisplayContextMenuEvent) {
		if (pt)
			mDisplayContextMenuEvent(*pt);
		else
			mDisplayContextMenuEvent(TranslateClientPtToScreenPt(vdpoint32(mClientArea.width() >> 1, mClientArea.height() >> 1)));
	}

	return true;
}

bool ATUIVideoDisplayWindow::OnKeyDown(const ATUIKeyEvent& event) {
	// Right-Alt kills capture.
	if (event.mExtendedVirtKey == kATUIVK_RAlt && g_mouseCaptured) {
		ReleaseMouse();
		return true;
	}

	if (event.mVirtKey == kATUIVK_Shift) {
		if (HasCursor() && !g_mouseCaptured) {
			if (!g_sim.IsRunning() && ATIsDebugConsoleActive()) {
				mbCoordIndicatorEnabled = true;

				SetCursorImage(kATUICursorImage_Cross);

				vdpoint32 cpt;
				if (TranslateScreenPtToClientPt(mpManager->GetCursorPosition(), cpt)) {
					SetCoordinateIndicator(cpt.x, cpt.y);
				}
			}

			// We don't switch to the query cursor here as that would cause annoying cursor
			// blinking while typing with the Shift key.
		}
	}

	// fall through so the simulator still receives the shift key, in case a key is typed
	if (ProcessKeyDown(event, !mpEnhTextEngine || mpEnhTextEngine->IsRawInputEnabled()) || (mpEnhTextEngine && mpEnhTextEngine->OnKeyDown(event.mVirtKey))) {
		ClearDragPreview();
		return true;
	}

	return ATUIWidget::OnKeyDown(event);
}

bool ATUIVideoDisplayWindow::OnKeyUp(const ATUIKeyEvent& event) {
	if (event.mVirtKey == kATUIVK_Shift) {
		mbCoordIndicatorEnabled = false;
		ClearCoordinateIndicator();

		g_sim.GetUIRenderer()->SetHoverTip(0, 0, NULL);
		mbHoverTipActive = false;

		vdpoint32 cpt;
		if (TranslateScreenPtToClientPt(mpManager->GetCursorPosition(), cpt))
			SetCursorImage(ComputeCursorImage(cpt));
	}

	if (ProcessKeyUp(event, !mpEnhTextEngine || mpEnhTextEngine->IsRawInputEnabled()) || (mpEnhTextEngine && mpEnhTextEngine->OnKeyUp(event.mVirtKey)))
		return true;

	return ATUIWidget::OnKeyUp(event);
}

bool ATUIVideoDisplayWindow::OnChar(const ATUICharEvent& event) {
	ClearDragPreview();

	if (mpEnhTextEngine && !mpEnhTextEngine->IsRawInputEnabled()) {
		if (mpEnhTextEngine)
			mpEnhTextEngine->OnChar(event.mCh);

		return true;
	}

	int code = event.mCh;
	if (code <= 0 || code > 127)
		return false;

	if (g_kbdOpts.mbRawKeys) {
		uint8 ch;

		if (!event.mbIsRepeat && ATUIGetScanCodeForCharacter(code, ch)) {
			mLastVkeySent = mLastVkeyPressed;

			g_sim.GetPokey().PushRawKey(ch);
		}
	} else {
		ProcessKey((char)code, event.mbIsRepeat, false, true);
	}

	return true;
}

void ATUIVideoDisplayWindow::OnActionStart(uint32 id) {
	switch(id) {
		case kActionOpenOSK:
			if (!mpOSK) {
				mpOSK = new ATUIOnScreenKeyboard;
				mpOSK->AddRef();
				AddChild(mpOSK);
				mpOSK->AutoSize();
				mpOSK->Focus();
				OnSize();
			}
			break;

		case kActionCloseOSK:
			if (mpOSK) {
				mpOSK->Destroy();
				vdsaferelease <<= mpOSK;
			}
			break;

		default:
			return ATUIContainer::OnActionStart(id);
	}
}

void ATUIVideoDisplayWindow::OnCreate() {
	ATUIContainer::OnCreate();

	BindAction(kATUIVK_UIOption, kActionOpenOSK);
	BindAction(kATUIVK_UIReject, kActionCloseOSK);
}

void ATUIVideoDisplayWindow::OnDestroy() {
	UnbindAllActions();

	vdsaferelease <<= mpOSK;

	ATUIContainer::OnDestroy();
}

void ATUIVideoDisplayWindow::OnSize() {
	const vdsize32& csz = mClientArea.size();

	if (mpOSK) {
		const vdsize32& osksz = mpOSK->GetArea().size();
		mpOSK->SetPosition(vdpoint32((csz.w - osksz.w) >> 1, csz.h - osksz.h));
	}

	ATUIContainer::OnSize();
}

void ATUIVideoDisplayWindow::OnKillFocus() {
	mbCoordIndicatorEnabled = false;
	ClearCoordinateIndicator();

	if (mbDragActive) {
		mbDragActive = false;

		ReleaseCursor();
	}
}

void ATUIVideoDisplayWindow::OnCaptureLost() {
	mbDragActive = false;
	g_mouseCaptured = false;
	g_mouseClipped = false;
	g_winCaptionUpdater.SetMouseCaptured(g_mouseClipped || g_mouseCaptured);
}

void ATUIVideoDisplayWindow::Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) {
	ATUIContainer::Paint(rdr, w, h);

	if (rdr.GetCaps().mbSupportsAlphaBlending) {
		for(XorRects::const_iterator it(mXorRects.begin()), itEnd(mXorRects.end());
			it != itEnd; ++it)
		{
			XorRect xr = *it;

			rdr.AlphaFillRect(xr.mX1, xr.mY1, xr.mX2 - xr.mX1, xr.mY2 - xr.mY1, 0x8000A0FF);
		}
	} else {
		rdr.SetColorRGB(0x00A0FF);

		for(XorRects::const_iterator it(mXorRects.begin()), itEnd(mXorRects.end());
			it != itEnd; ++it)
		{
			XorRect xr = *it;

			rdr.FillRect(xr.mX1, xr.mY1, xr.mX2 - xr.mX1, 1);
			rdr.FillRect(xr.mX1, xr.mY2 - 1, xr.mX2 - xr.mX1, 1);
			rdr.FillRect(xr.mX1, xr.mY1 + 1, 1, xr.mY2 - xr.mY1 - 2);
			rdr.FillRect(xr.mX2 - 1, xr.mY1 + 1, 1, xr.mY2 - xr.mY1 - 2);
		}
	}
}

bool ATUIVideoDisplayWindow::ProcessKeyDown(const ATUIKeyEvent& event, bool enableKeyInput) {
	ATInputManager *im = g_sim.GetInputManager();

	const int key = event.mVirtKey;
	const bool alt = mpManager->IsKeyDown(kATUIVK_Alt);

	if (!alt) {
		const int inputCode = event.mExtendedVirtKey;

		if (im->IsInputMapped(0, inputCode)) {
			im->OnButtonDown(0, inputCode);
			return true;
		}
	}

	const bool isRepeat = event.mbIsRepeat;
	const bool shift = mpManager->IsKeyDown(kATUIVK_Shift);
	const bool ctrl = mpManager->IsKeyDown(kATUIVK_Control);
	const bool acs = alt || ctrl || shift;
	const uint8 ctrlmod = (ctrl ? 0x80 : 0x00);
	const uint8 shiftmod = (shift ? 0x40 : 0x00);

	const uint8 modifier = ctrlmod + shiftmod;
	if (key == kATUIVK_CapsLock) {
		if (!enableKeyInput)
			return false;

		ProcessVirtKey(key, 0x3C + modifier, false);
		return true;
	}

	const bool ext = event.mbIsExtendedKey;
	if (ATUIActivateVirtKeyMapping(key, alt, ctrl, shift, ext, false, kATUIAccelContext_Display)) {
		mLastVkeyPressed = key;
		return true;
	} else {
		uint8 scanCode;
		if (ATUIGetScanCodeForVirtualKey(key, alt, ctrl, shift, scanCode)) {
			if (!enableKeyInput)
				return false;

			ProcessVirtKey(key, scanCode, isRepeat);
			return true;
		}

		switch(key) {
			case kATUIVK_Shift:
				g_sim.GetPokey().SetShiftKeyState(true);
				break;

			case kATUIVK_F2:
				if (!acs) {
					g_sim.GetGTIA().SetConsoleSwitch(0x01, true);
					return true;
				}
				break;

			case kATUIVK_F3:
				if (!acs) {
					g_sim.GetGTIA().SetConsoleSwitch(0x02, true);
					return true;
				}
				break;

			case kATUIVK_F4:
				if (!acs) {
					g_sim.GetGTIA().SetConsoleSwitch(0x04, true);
					return true;
				}

				// Note: Particularly important that we don't eat Alt+F4 here.
				break;

			case kATUIVK_F7:
				if (!ctrl && !alt) {
					g_sim.GetPokey().PushBreak();
					return true;
				}
				break;

			case kATUIVK_Cancel:
			case kATUIVK_Pause:
				if (!ctrl && !alt) {
					g_sim.GetPokey().PushBreak();
					return true;
				}
				break;
		}
	}

	mLastVkeyPressed = key;
	return false;
}

bool ATUIVideoDisplayWindow::ProcessKeyUp(const ATUIKeyEvent& event, bool enableKeyInput) {
	ATInputManager *im = g_sim.GetInputManager();

	const int key = event.mVirtKey;
	const bool alt = mpManager->IsKeyDown(kATUIVK_Alt);

	if (!alt) {
		int inputCode = event.mExtendedVirtKey;

		if (im->IsInputMapped(0, inputCode)) {
			im->OnButtonUp(0, inputCode);
			return true;
		}
	}

	if (key == kATUIVK_CapsLock) {
		if (!enableKeyInput)
			return false;
	}

	if (mLastVkeySent == key) {
		g_sim.GetPokey().ReleaseRawKey();
		return true;
	}

	const bool shift = mpManager->IsKeyDown(kATUIVK_Shift);
	const bool ctrl = mpManager->IsKeyDown(kATUIVK_Control);
	const bool acs = alt || ctrl || shift;
	const bool ext = event.mbIsExtendedKey;

	if (!ATUIActivateVirtKeyMapping(key, alt, ctrl, shift, ext, true, kATUIAccelContext_Display)) {
		switch(key) {
			case kATUIVK_CapsLock:
				return true;

			case kATUIVK_Shift:
				g_sim.GetPokey().SetShiftKeyState(false);
				break;

			case kATUIVK_F2:
				if (!acs) {
					g_sim.GetGTIA().SetConsoleSwitch(0x01, false);
					return true;
				}
				break;

			case kATUIVK_F3:
				if (!acs) {
					g_sim.GetGTIA().SetConsoleSwitch(0x02, false);
					return true;
				}
				break;

			case kATUIVK_F4:
				if (!acs) {
					g_sim.GetGTIA().SetConsoleSwitch(0x04, false);
					return true;
				}
				break;
		}
	}

	return false;
}

void ATUIVideoDisplayWindow::ProcessVirtKey(int vkey, uint8 keycode, bool repeat) {
	mLastVkeySent = vkey;

	if (g_kbdOpts.mbRawKeys) {
		if (!repeat)
			g_sim.GetPokey().PushRawKey(keycode);
	} else
		g_sim.GetPokey().PushKey(keycode, repeat);
}

uint32 ATUIVideoDisplayWindow::ComputeCursorImage(const vdpoint32& pt) const {
	int xs, ys;
	bool validBeamPosition = (MapPixelToBeamPosition(pt.x, pt.y, xs, ys, false) && GetModeLineYPos(ys, true) >= 0);
	bool cursorSet = false;

	if (!g_mouseCaptured && validBeamPosition && mpManager->IsKeyDown(kATUIVK_Shift))
		return kATUICursorImage_Query;

	if (g_sim.GetInputManager()->IsMouseMapped()) {
		cursorSet = true;

		if (g_mouseAutoCapture && !g_mouseCaptured) {

			// Auto-capture is on but we have not captured the cursor. In this case we show the
			// arrow. We don't show the I-Beam because left-click selection functionality is
			// overridden by the mouse auto-capture.

			return kATUICursorImage_Arrow;

		} else if (g_sim.GetInputManager()->IsMouseAbsoluteMode()) {

			// We're in absolute mode, and either the mouse is captured or auto-capture is off.
			// In this case we will be passing absolute mouse inputs to the input manager. Show
			// the target or off-target depending on whether the target is active (light pen
			// aimed at screen or otherwise not a light pen/gun).

			if (g_sim.GetInputManager()->IsMouseActiveTarget())
				return kATUICursorImage_Target;
			else
				return kATUICursorImage_TargetOff;

		} else if (IsCursorCaptured()) {

			// We're in relative mode and the cursor is captured. We need to hide the mouse
			// cursor so you don't see the jitters.

			return kATUICursorImage_Hidden;

		}

		// The mouse is bound in relative mode but not captured, so we should fall through.
	}

	if (validBeamPosition)
		return kATUICursorImage_IBeam;

	return kATUICursorImage_Arrow;
}

void ATUIVideoDisplayWindow::UpdateMousePosition(int x, int y) {
	int padX = 0;
	int padY = 0;

	const vdsize32 size = mClientArea.size();

	if (size.w)
		padX = VDRoundToInt(x * 131072.0f / ((float)size.w - 1) - 0x10000);

	if (size.h)
		padY = VDRoundToInt(y * 131072.0f / ((float)size.h - 1) - 0x10000);

	ATInputManager *im = g_sim.GetInputManager();
	im->SetMousePadPos(padX, padY);

	float xc, yc;
	if (!MapPixelToBeamPosition(x, y, xc, yc, false))
		return;

	// map cycles to normalized position (note that this must match the light pen
	// computations)
	float xn = (xc - 128.0f) * (65536.0f / 94.0f);
	float yn = (yc - 128.0f) * (65536.0f / 188.0f);

	im->SetMouseBeamPos(VDRoundToInt(xn), VDRoundToInt(yn));
}

bool ATUIVideoDisplayWindow::MapPixelToBeamPosition(int x, int y, float& hcyc, float& vcyc, bool clamp) const {
	if (!clamp && !mDisplayRect.contains(vdpoint32(x, y)))
		return false;

	x -= mDisplayRect.left;
	y -= mDisplayRect.top;

	if (clamp) {
		if (x < 0)
			x = 0;
		else if (x > mDisplayRect.width())
			x = mDisplayRect.width();

		if (y < 0)
			y = 0;
		else if (y > mDisplayRect.height())
			y = mDisplayRect.height();
	}

	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	const vdrect32 scanArea(gtia.GetFrameScanArea());

	// map position to cycles
	hcyc = (float)scanArea.left + ((float)x + 0.5f) * (float)scanArea.width()  / (float)mDisplayRect.width()  - 0.5f;
	vcyc = (float)scanArea.top  + ((float)y + 0.5f) * (float)scanArea.height() / (float)mDisplayRect.height() - 0.5f;
	return true;
}

bool ATUIVideoDisplayWindow::MapPixelToBeamPosition(int x, int y, int& xc, int& yc, bool clamp) const {
	float xf, yf;

	if (!MapPixelToBeamPosition(x, y, xf, yf, clamp))
		return false;

	xc = (int)floorf(xf + 0.5f);
	yc = (int)floorf(yf + 0.5f);
	return true;
}

void ATUIVideoDisplayWindow::MapBeamPositionToPixel(int xc, int yc, int& x, int& y) const {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	const vdrect32 scanArea(gtia.GetFrameScanArea());

	// map position to cycles
	x = VDRoundToInt(((float)xc + 0.5f - (float)scanArea.left) * (float)mDisplayRect.width()  / (float)scanArea.width() ) + mDisplayRect.left;
	y = VDRoundToInt(((float)yc + 0.5f - (float)scanArea.top ) * (float)mDisplayRect.height() / (float)scanArea.height()) + mDisplayRect.top;
}

void ATUIVideoDisplayWindow::UpdateDragPreview(int x, int y) {
	int xc2, yc2;

	if (!MapPixelToBeamPosition(x, y, xc2, yc2, true)) {
		ClearDragPreview();
		return;
	}

	int xc1 = mDragAnchorX;
	int yc1 = mDragAnchorY;

	yc1 = GetModeLineYPos(yc1, false);
	yc2 = GetModeLineYPos(yc2, false);

	if ((yc1 | yc2) < 0) {
		ClearDragPreview();
		return;
	}

	if (yc1 > yc2 || (yc1 == yc2 && xc1 > xc2)) {
		std::swap(xc1, xc2);
		std::swap(yc1, yc2);
	}

	ATAnticEmulator& antic = g_sim.GetAntic();
	const ATAnticEmulator::DLHistoryEntry *dlhist = antic.GetDLHistory();

	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	const vdrect32 scanArea(gtia.GetFrameScanArea());

	mDragPreviewSpans.clear();

	for(int yc = yc1; yc <= yc2; ++yc) {
		if (!dlhist[yc].mbValid)
			continue;

		bool textModeLine = false;
		switch(dlhist[yc].mControl & 15) {
			case 2:
			case 3:
			case 6:
			case 7:
				textModeLine = true;
				break;
		}

		if (!textModeLine)
			continue;

		int pfwidth = dlhist[yc].mDMACTL & 3;
		if (!pfwidth)
			continue;

		if (pfwidth < 3 && (dlhist[yc].mControl & 0x10))
			++pfwidth;

		int left = (yc == yc1) ? xc1 : scanArea.left;
		int right = (yc == yc2) ? xc2 : scanArea.right;

		const int leftborder = 0x50 - 0x10*pfwidth;
		left = std::max<int>(left, leftborder);
		right = std::min<int>(right, 0xB0 + 0x10*pfwidth);

		bool dblwide = false;
		switch(dlhist[yc].mControl & 15) {
			case 2:
			case 3:
				left = (left + 2) & ~3;
				right = (right + 2) & ~3;
				break;

			case 6:
			case 7:
				left  = (left + 4) & ~7;
				right = (right + 4) & ~7;
				dblwide = true;
				break;
		}

		if (left >= right)
			continue;

		TextSpan& ts = mDragPreviewSpans.push_back();
		ts.mX = left;
		ts.mWidth = right - left;
		ts.mY = yc;
		ts.mHeight = 0;

		if (dblwide) {
			ts.mCharX = (left - leftborder) >> 3;
			ts.mCharWidth = (right - left) >> 3;
		} else {
			ts.mCharX = (left - leftborder) >> 2;
			ts.mCharWidth = (right - left) >> 2;
		}

		for(int i=0; i<16; ++i) {
			++ts.mHeight;

			if (yc + ts.mHeight >= 248 || dlhist[yc + ts.mHeight].mbValid)
				break;
		}
	}

	UpdateDragPreviewRects();
}

void ATUIVideoDisplayWindow::UpdateDragPreviewRects() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	const vdrect32 scanArea(gtia.GetFrameScanArea());

	float scaleX = (float)mDisplayRect.width() / (float)scanArea.width();
	float scaleY = (float)mDisplayRect.height() / (float)scanArea.height();

	ClearXorRects();

	TextSpans::const_iterator it(mDragPreviewSpans.begin()), itEnd(mDragPreviewSpans.end());
	
	for(; it != itEnd; ++it) {
		const TextSpan& ts = *it;

		int hiX1 = ts.mX - scanArea.left;
		int hiY1 = ts.mY - scanArea.top;
		int hiX2 = hiX1 + ts.mWidth;
		int hiY2 = hiY1 + ts.mHeight;

		AddXorRect(
			VDRoundToInt(hiX1 * scaleX) + mDisplayRect.left,
			VDRoundToInt(hiY1 * scaleY) + mDisplayRect.top,
			VDRoundToInt(hiX2 * scaleX) + mDisplayRect.left,
			VDRoundToInt(hiY2 * scaleY) + mDisplayRect.top);
	}
}

void ATUIVideoDisplayWindow::ClearDragPreview() {
	mDragPreviewSpans.clear();

	ClearXorRects();
}

int ATUIVideoDisplayWindow::GetModeLineYPos(int ys, bool checkValidCopyText) const {
	ATAnticEmulator& antic = g_sim.GetAntic();
	const ATAnticEmulator::DLHistoryEntry *dlhist = antic.GetDLHistory();

	for(int i=0; i<16; ++i, --ys) {
		if (ys >= 8 && ys < 248) {
			if (dlhist[ys].mbValid) {
				if (checkValidCopyText) {
					int mode = dlhist[ys].mControl & 15;

					if (mode != 2 && mode != 3 && mode != 6 && mode != 7)
						return -1;
				}

				return ys;
			}
		}
	}

	return -1;
}

/// Read characters from a text mode line into a buffer; returns the number
/// of characters read, or zero if an error occurs (range out of bounds, no
/// mode line, not a supported text mode line). The returned buffer is _not_
/// null terminated.
///
int ATUIVideoDisplayWindow::ReadText(char *dst, int yc, int startChar, int numChars) const {
	ATAnticEmulator& antic = g_sim.GetAntic();
	const ATAnticEmulator::DLHistoryEntry *dlhist = antic.GetDLHistory();
	const ATAnticEmulator::DLHistoryEntry& dle = dlhist[yc];

	// check that mode line is valid
	if (!dle.mbValid)
		return 0;

	// check mode
	switch(dle.mControl & 15) {
		case 2:
		case 3:
		case 6:
		case 7:
			break;

		default:
			return 0;
	}

	// compute width
	static const int kWidthLookup[2][4] = {
		{ 0, 16, 20, 24 },	// no horizontal scrolling
		{ 0, 20, 24, 24 },	// horizontal scrolling
	};

	int len = (dle.mControl & 4 ? 1 : 2) * kWidthLookup[(dle.mControl & 0x10) != 0][dle.mDMACTL & 3];

	// clip
	if (numChars <= 0 || startChar >= len)
		return 0;

	if (startChar < 0) {
		numChars += startChar;
		startChar = 0;
	}

	if (numChars > len - startChar)
		numChars = len - startChar;

	if (numChars <= 0)
		return 0;

	// read out raw bytes
	uint8 data[48];
	g_sim.GetMemoryManager()->DebugAnticReadMemory(data, dle.mPFAddress + startChar, len);

	static const uint8 kInternalToATASCIIXorTab[4]={
		0x20, 0x60, 0x40, 0x00
	};

	uint8 mask = (dle.mControl & 4) ? 0x3f : 0x7f;
	uint8 xor = (dle.mControl & 4) && (dle.mCHBASE & 1) ? 0x40 : 0x00;

	for(int i=0; i<numChars; ++i) {
		uint8 c = data[i];

		// convert ATASCII char to ASCII
		c &= mask;
		c ^= xor;

		c ^= kInternalToATASCIIXorTab[(c & 0x60) >> 5];

		// replace non-printable chars with spaces
		if ((uint8)(c - 0x20) >= 0x5f)
			c = ' ';

		// write char
		*dst++ = (char)c;
	}

	return numChars;
}

void ATUIVideoDisplayWindow::ClearCoordinateIndicator() {
	if (mbCoordIndicatorActive) {
		mbCoordIndicatorActive = false;

		IATUIRenderer *uir = g_sim.GetUIRenderer();
		if (uir)
			uir->SetHoverTip(0, 0, NULL);
	}
}

void ATUIVideoDisplayWindow::SetCoordinateIndicator(int x, int y) {
	int hcyc, vcyc;

	if (!MapPixelToBeamPosition(x, y, hcyc, vcyc, false)) {
		ClearCoordinateIndicator();
		return;
	}

	mbCoordIndicatorActive = true;

	IATUIRenderer *uir = g_sim.GetUIRenderer();
	if (uir) {
		VDStringW s;

		s.sprintf(L"<b>Pos:</b> (%u,%u)\n", hcyc, vcyc);

		ATAnticEmulator& antic = g_sim.GetAntic();
		const ATAnticEmulator::DLHistoryEntry *dlhist = antic.GetDLHistory();

		bool dlvalid = false;
		if (vcyc >= 8 && vcyc < 248) {
			int y = vcyc;

			while(y > 8 && !dlhist[y].mbValid)
				--y;

			const ATAnticEmulator::DLHistoryEntry& dlent = dlhist[y];

			if (dlent.mbValid) {
				uint8 mode = dlent.mControl & 15;
				uint8 special = dlent.mControl & 0xf0;

				dlvalid = true;
				s.append_sprintf(L"<b>DL[$%04X]:</b> ", dlent.mDLAddress);

				if (mode >= 2) {
					const wchar_t *const kPFWidths[]={ L"Disabled", L"Narrow", L"Normal", L"Wide" };

					s.append_sprintf(L"Mode %X %ls @ $%04X", mode, kPFWidths[dlent.mDMACTL & 3], dlent.mPFAddress);
				} else if (mode == 1) {
					if (dlent.mControl & 0x40)
						s.append(L"JVB");
					else
						s.append(L"Jump");

					special &= 0xb0;
				} else {
					s.append_sprintf(L"Blank x%u", ((dlent.mControl >> 4) & 7) + 1);
					special &= 0x80;
				}

				if (special) {
					s.append(L" (");

					if (special & 0x80)
						s.append(L"DLI, ");

					if (special & 0x40)
						s.append(L"LMS, ");

					if (special & 0x20)
						s.append(L"VSCR, ");

					if (special & 0x10)
						s.append(L"HSCR, ");

					s.pop_back();
					s.pop_back();

					s.append(L")");
				}
			}
		}

		if (!dlvalid)
			s.append(L"<b>DL:</b> None");

		uir->SetHoverTip(x, y, s.c_str());
	}
}
