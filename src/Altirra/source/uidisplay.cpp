//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2012 Avery Lee
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
#include <windows.h>
#include <vd2/system/math.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/w32assist.h>
#include <vd2/VDDisplay/display.h>
#include "console.h"
#include "inputmanager.h"
#include "simulator.h"
#include "uidisplay.h"
#include "uienhancedtext.h"
#include "uiframe.h"
#include "uirender.h"
#include "uitypes.h"
#include "uicaptionupdater.h"
#include "uimanager.h"
#include "uimenu.h"
#include "uiportmenus.h"
#include "uimenulist.h"
#include "uicontainer.h"
#include "uiprofiler.h"
#include "uitextedit.h"
#include "uibutton.h"
#include "uilistview.h"
#include "uifilebrowser.h"
#include "resource.h"

extern ATSimulator g_sim;

extern IVDVideoDisplay *g_pDisplay;

extern bool g_ATAutoFrameFlipping;
extern bool g_fullscreen;
extern bool g_mouseCaptured;
extern bool g_mouseClipped;
extern bool g_mouseAutoCapture;
extern LOGFONTW g_enhancedTextFont;

extern ATDisplayFilterMode g_dispFilterMode;
extern int g_dispFilterSharpness;

extern ATDisplayStretchMode g_displayStretchMode;

extern ATUIWindowCaptionUpdater g_winCaptionUpdater;

ATUIManager g_ATUIManager;

bool OnKeyDown(HWND hwnd, WPARAM wParam, LPARAM lParam, bool enableKeyInput);
bool OnKeyUp(HWND hwnd, WPARAM wParam, LPARAM lParam, bool enableKeyInput);
void OnChar(HWND hwnd, WPARAM wParam, LPARAM lParam);

void OnCommandEditPasteText();
bool OnCommand(UINT id);

///////////////////////////////////////////////////////////////////////////

class ATUIVideoDisplayWindow : public ATUIWidget {
public:
	ATUIVideoDisplayWindow();
	~ATUIVideoDisplayWindow();

	void ClearXorRects();
	void AddXorRect(int x1, int y1, int x2, int y2);

protected:
	void Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h);

	struct XorRect {
		int mX1;
		int mY1;
		int mX2;
		int mY2;
	};

	typedef vdfastvector<XorRect> XorRects;
	XorRects mXorRects;
};

ATUIVideoDisplayWindow::ATUIVideoDisplayWindow() {
	mbFastClip = true;
	mbHitTransparent = true;
	SetAlphaFillColor(0);
}

ATUIVideoDisplayWindow::~ATUIVideoDisplayWindow() {
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

void ATUIVideoDisplayWindow::Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) {
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

ATUIVideoDisplayWindow *g_pATVideoDisplayWindow;

///////////////////////////////////////////////////////////////////////////

class ATUINativeDisplay : public IATUINativeDisplay {
public:
	ATUINativeDisplay()
		: mhwnd(NULL)
		, mpFrame(NULL)
		, mModalCount(0)
	{
	}

	virtual void Invalidate() {
		if (g_pDisplay && (!g_ATAutoFrameFlipping || mModalCount))
			g_pDisplay->Invalidate();
	}

	virtual void CaptureCursor() {
		if (mhwnd)
			::SetCapture(mhwnd);
	}

	virtual void ReleaseCursor() {
		if (mhwnd)
			::ReleaseCapture();
	}

	virtual void BeginModal() {
		if (!mModalCount++) {
			if (mpFrame)
				mpFrame->GetContainer()->SetModalFrame(mpFrame);
		}
	}

	virtual void EndModal() {
		VDASSERT(mModalCount);

		if (!--mModalCount) {
			if (mpFrame)
				mpFrame->GetContainer()->SetModalFrame(NULL);
		}
	}

	virtual bool IsKeyDown(uint32 vk) {
		return GetKeyState(vk) < 0;
	}

	void SetDisplay(HWND hwnd, ATFrameWindow *frame) {
		if (mpFrame && mModalCount)
			mpFrame->GetContainer()->SetModalFrame(NULL);

		mhwnd = hwnd;
		mpFrame = frame;

		if (mpFrame && mModalCount)
			mpFrame->GetContainer()->SetModalFrame(mpFrame);
	}

private:
	HWND mhwnd;
	ATFrameWindow *mpFrame;
	uint32 mModalCount;
} g_ATUINativeDisplay;

///////////////////////////////////////////////////////////////////////////

void ATUIInitManager() {
	g_ATUIManager.Init(&g_ATUINativeDisplay);

	g_pATVideoDisplayWindow = new ATUIVideoDisplayWindow;
	g_pATVideoDisplayWindow->AddRef();
	g_ATUIManager.GetMainWindow()->AddChild(g_pATVideoDisplayWindow);

	g_sim.GetUIRenderer()->SetUIManager(&g_ATUIManager);

//	ATUIProfileCreateWindow(&g_ATUIManager);
}

void ATUIShutdownManager() {
	ATUIProfileDestroyWindow();

	vdsaferelease <<= g_pATVideoDisplayWindow;

	g_sim.GetUIRenderer()->SetUIManager(NULL);

	g_ATUIManager.Shutdown();
}

void ATUIFlushDisplay() {
	if (g_ATUIManager.IsInvalidated()) {
		if (g_pDisplay)
			g_pDisplay->Invalidate();
	}
}

class ATDisplayPane : public ATUIPane, public IATDisplayPane {
public:
	ATDisplayPane();
	~ATDisplayPane();

	void *AsInterface(uint32 iid);

	void CaptureMouse();
	void ResetDisplay();

	bool IsTextSelected() const { return !mDragPreviewSpans.empty(); }
	void Copy();
	void Paste();

	void OnSize();
	void UpdateFilterMode();

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	bool OnCreate();
	void OnDestroy();
	void OnMouseHover(WPARAM wParam);
	void SetHaveMouse();
	void WarpCapturedMouse();
	void UpdateTextDisplay(bool enabled);
	void UpdateTextModeFont();
	void UpdateMousePosition(int x, int y);
	bool MapPixelToBeamPosition(int x, int y, float& hcyc, float& vcyc, bool clamp) const;
	bool MapPixelToBeamPosition(int x, int y, int& xc, int& yc, bool clamp) const;
	void MapBeamPositionToPixel(int xc, int yc, int& x, int& y) const;
	void UpdateDragPreview(int x, int y);
	void UpdateDragPreviewRects();
	void ClearDragPreview();
	int GetModeLineYPos(int yc, bool checkValidCopyText) const;
	int ReadText(char *dst, int yc, int startChar, int numChars) const;
	void ClearCoordinateIndicator();
	void SetCoordinateIndicator(int x, int y);

	void OnMenuItemSelected(ATUIMenuList *sender, uint32 id);

	HWND	mhwndDisplay;
	HMENU	mhmenuContext;
	IVDVideoDisplay *mpDisplay;
	HCURSOR	mhcurTarget;
	HCURSOR	mhcurTargetOff;
	int		mLastHoverX;
	int		mLastHoverY;
	int		mLastTrackMouseX;
	int		mLastTrackMouseY;
	int		mMouseCursorLock;
	int		mWidth;
	int		mHeight;

	bool	mbDragActive;
	int		mDragAnchorX;
	int		mDragAnchorY;

	vdrect32	mDisplayRect;

	struct TextSpan {
		int mX;
		int mY;
		int mWidth;
		int mHeight;
		int mCharX;
		int mCharWidth;
	};

	typedef vdfastvector<TextSpan> TextSpans;
	TextSpans mDragPreviewSpans;

	bool	mbTextModeEnabled;
	bool	mbHaveMouse;
	bool	mbCoordIndicatorActive;
	bool	mbCoordIndicatorEnabled;
	vdrect32	mHoverTipArea;
	bool		mbHoverTipActive;

	vdautoptr<IATUIEnhancedTextEngine> mpEnhTextEngine;
	vdrefptr<ATUIMenuList> mpMenuBar;

	VDDelegate mDelItemSelected;
};

ATDisplayPane::ATDisplayPane()
	: ATUIPane(kATUIPaneId_Display, L"Display")
	, mhwndDisplay(NULL)
	, mhmenuContext(LoadMenu(NULL, MAKEINTRESOURCE(IDR_DISPLAY_CONTEXT_MENU)))
	, mpDisplay(NULL)
	, mhcurTarget(NULL)
	, mhcurTargetOff(NULL)
	, mLastHoverX(INT_MIN)
	, mLastHoverY(INT_MIN)
	, mLastTrackMouseX(0)
	, mLastTrackMouseY(0)
	, mMouseCursorLock(0)
	, mWidth(0)
	, mHeight(0)
	, mbDragActive(false)
	, mDragAnchorX(0)
	, mDragAnchorY(0)
	, mDisplayRect(0,0,0,0)
	, mbTextModeEnabled(false)
	, mbHaveMouse(false)
	, mbCoordIndicatorActive(false)
	, mbCoordIndicatorEnabled(false)
	, mHoverTipArea(0, 0, 0, 0)
	, mbHoverTipActive(false)
{
	mPreferredDockCode = kATContainerDockCenter;
}

ATDisplayPane::~ATDisplayPane() {
	if (mhmenuContext)
		DestroyMenu(mhmenuContext);
}

void *ATDisplayPane::AsInterface(uint32 iid) {
	if (iid == IATDisplayPane::kTypeID)
		return static_cast<IATDisplayPane *>(this);

	return ATUIPane::AsInterface(iid);
}

LRESULT ATDisplayPane::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case ATWM_PRESYSKEYDOWN:
			// Right-Alt kills capture.
			if (wParam == VK_MENU && (lParam & (1 << 24))) {
				if (::GetCapture() == mhwnd) {
					::ReleaseCapture();
					return true;
				}

				if (g_mouseClipped) {
					g_mouseClipped = false;
					g_winCaptionUpdater.SetMouseCaptured(g_mouseClipped || g_mouseCaptured);

					::ClipCursor(NULL);
					return true;
				}
			}
			// fall through
		case ATWM_PREKEYDOWN:
			if (wParam == VK_SHIFT) {
				if (!g_sim.IsRunning() && ATIsDebugConsoleActive() && mbHaveMouse) {
					DWORD pos = ::GetMessagePos();
					POINT pt = { (short)LOWORD(pos), (short)HIWORD(pos) };

					mbCoordIndicatorEnabled = true;

					if (::ScreenToClient(mhwnd, &pt)) {
						::SetCursor(::LoadCursor(NULL, IDC_CROSS));
						SetCoordinateIndicator(pt.x, pt.y);
					}

					// fall through so the simulator still receives the shift key, in case a key is typed
				}
			}

			if (g_ATUIManager.OnKeyDown(wParam))
				return true;

			if (OnKeyDown(mhwnd, wParam, lParam, !mpEnhTextEngine || mpEnhTextEngine->IsRawInputEnabled()) || (mpEnhTextEngine && mpEnhTextEngine->OnKeyDown(wParam, lParam))) {
				ClearDragPreview();
				return true;
			}
			return false;

		case ATWM_PRESYSKEYUP:
		case ATWM_PREKEYUP:
			if (wParam == VK_SHIFT) {
				mbCoordIndicatorEnabled = false;
				ClearCoordinateIndicator();

				g_sim.GetUIRenderer()->SetHoverTip(0, 0, NULL);
				mbHoverTipActive = false;
			}

			if (g_ATUIManager.OnKeyUp(wParam))
				return true;

			if (OnKeyUp(mhwnd, wParam, lParam, !mpEnhTextEngine || mpEnhTextEngine->IsRawInputEnabled()) || (mpEnhTextEngine && mpEnhTextEngine->OnKeyUp(wParam, lParam)))
				return true;
			return false;

		case WM_SIZE:
			{
				int w = LOWORD(lParam);
				int h = HIWORD(lParam);

				if (w != mWidth || h != mHeight) {
					mWidth = w;
					mHeight = h;

					g_ATUIManager.Resize(w, h);
					if (mpMenuBar)
						mpMenuBar->SetArea(vdrect32(0, 0, w, 20));

					if (g_pATVideoDisplayWindow)
						g_pATVideoDisplayWindow->SetArea(vdrect32(0, 0, w, h));

					IATUIRenderer *r = g_sim.GetUIRenderer();

					if (r)
						r->Relayout(w, h);
				}
			}
			break;

		case WM_CHAR:
			ClearDragPreview();

			if (g_ATUIManager.OnChar(wParam))
				return 0;

			if (mpEnhTextEngine && !mpEnhTextEngine->IsRawInputEnabled()) {
				if (mpEnhTextEngine)
					mpEnhTextEngine->OnChar((int)wParam);
			} else {
				OnChar(mhwnd, wParam, lParam);
			}
			return 0;

		case WM_PARENTNOTIFY:
			switch(LOWORD(wParam)) {
			case WM_LBUTTONDOWN:
			case WM_MBUTTONDOWN:
			case WM_RBUTTONDOWN:
				SetFocus(mhwnd);
				break;
			}
			break;

		case WM_LBUTTONUP:
			g_ATUIManager.OnMouseUpL((short)LOWORD(lParam), (short)HIWORD(lParam));

			if (mbDragActive) {
				mbDragActive = false;

				::ReleaseCapture();

				int x = (short)LOWORD(lParam);
				int y = (short)HIWORD(lParam);
				UpdateDragPreview(x, y);
			} else {
				ATInputManager *im = g_sim.GetInputManager();
				if (im->IsMouseAbsoluteMode() || ::GetCapture() == mhwnd) {

					if (im->IsMouseMapped())
						im->OnButtonUp(0, kATInputCode_MouseLMB);
				}
			}
			break;

		case WM_MBUTTONUP:
			{
				ATInputManager *im = g_sim.GetInputManager();
				if (im->IsMouseAbsoluteMode() || ::GetCapture() == mhwnd) {

					if (im->IsMouseMapped())
						im->OnButtonUp(0, kATInputCode_MouseMMB);
				}
			}
			break;

		case WM_RBUTTONUP:
			{
				ATInputManager *im = g_sim.GetInputManager();
				if (im->IsMouseAbsoluteMode() || ::GetCapture() == mhwnd) {

					if (im->IsMouseMapped()) {
						im->OnButtonUp(0, kATInputCode_MouseRMB);

						// eat the message to prevent WM_CONTEXTMENU
						return 0;
					}
				}
			}
			break;

		case WM_XBUTTONUP:
			if (::GetCapture() == mhwnd) {
				ATInputManager *im = g_sim.GetInputManager();

				if (im->IsMouseMapped()) {
					if (HIWORD(wParam) == XBUTTON1)
						im->OnButtonUp(0, kATInputCode_MouseX1B);
					else if (HIWORD(wParam) == XBUTTON2)
						im->OnButtonUp(0, kATInputCode_MouseX2B);
				}
			}
			break;

		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONDBLCLK:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONDBLCLK:
		case WM_XBUTTONDOWN:
		case WM_XBUTTONDBLCLK:
			SetHaveMouse();

			::SetFocus(mhwnd);

			{
				int x = (short)LOWORD(lParam);
				int y = (short)HIWORD(lParam);

				ATInputManager *im = g_sim.GetInputManager();
				if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
					if (g_ATUIManager.OnMouseDownL(x, y))
						return 0;
				}

				if (im->IsMouseMapped() && g_sim.IsRunning()) {
					bool absMap = im->IsMouseAbsoluteMode();

					if (absMap)
						UpdateMousePosition((short)LOWORD(lParam), (short)HIWORD(lParam));

					if (absMap || ::GetCapture() == mhwnd) {
						if (!g_mouseCaptured && !g_mouseClipped && g_mouseAutoCapture)
							CaptureMouse();

						switch(msg) {
							case WM_LBUTTONDOWN:
							case WM_LBUTTONDBLCLK:
								im->OnButtonDown(0, kATInputCode_MouseLMB);
								break;
							case WM_MBUTTONDOWN:
							case WM_MBUTTONDBLCLK:
								im->OnButtonDown(0, kATInputCode_MouseMMB);
								break;
							case WM_RBUTTONDOWN:
							case WM_RBUTTONDBLCLK:
								im->OnButtonDown(0, kATInputCode_MouseRMB);
								break;
							case WM_XBUTTONDOWN:
							case WM_XBUTTONDBLCLK:
								if (HIWORD(wParam) == XBUTTON1)
									im->OnButtonDown(0, kATInputCode_MouseX1B);
								else if (HIWORD(wParam) == XBUTTON2)
									im->OnButtonDown(0, kATInputCode_MouseX2B);
								break;
						}
					} else if (g_mouseAutoCapture) {
						CaptureMouse();
						::SetCursor(NULL);
					}
				} else if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
					mbDragActive = MapPixelToBeamPosition(x, y, mDragAnchorX, mDragAnchorY, true)
						&& GetModeLineYPos(mDragAnchorY, true) >= 0;

					if (mbDragActive) {
						::SetCursor(::LoadCursor(NULL, IDC_IBEAM));
						::SetCapture(mhwnd);
					}
				}
			}
			break;

		case WM_CONTEXTMENU:
			if (mhmenuContext) {
				int x = (short)LOWORD(lParam);
				int y = (short)HIWORD(lParam);

				HMENU hmenuPopup = GetSubMenu(mhmenuContext, 0);

				if (hmenuPopup) {
					++mMouseCursorLock;
					EnableMenuItem(hmenuPopup, ID_DISPLAYCONTEXTMENU_COPY, mDragPreviewSpans.empty() ? MF_DISABLED|MF_GRAYED : MF_ENABLED);
					EnableMenuItem(hmenuPopup, ID_DISPLAYCONTEXTMENU_PASTE, IsClipboardFormatAvailable(CF_TEXT) ? MF_ENABLED : MF_DISABLED|MF_GRAYED);

					TrackPopupMenu(hmenuPopup, TPM_LEFTALIGN | TPM_TOPALIGN, x, y, 0, mhwnd, NULL);
					--mMouseCursorLock;
				}
				return 0;
			}
			break;

		case WM_SETCURSOR:
			if (mMouseCursorLock)
				break;

			if (mbDragActive) {
				::SetCursor(::LoadCursor(NULL, IDC_IBEAM));
				return TRUE;
			} else if (g_mouseAutoCapture ? g_mouseClipped : g_sim.GetInputManager()->IsMouseAbsoluteMode()) {
				SetCursor(g_sim.GetInputManager()->IsMouseActiveTarget() ? mhcurTarget : mhcurTargetOff);
				return TRUE;
			} else if (::GetCapture() == mhwnd) {
				::SetCursor(NULL);
				return TRUE;
			} else if (mbCoordIndicatorEnabled) {
				::SetCursor(::LoadCursor(NULL, IDC_CROSS));
				return TRUE;
			} else {
				DWORD pos = ::GetMessagePos();
				int x = (short)LOWORD(pos);
				int y = (short)HIWORD(pos);

				uint32 id = g_ATUIManager.GetCurrentCursorImageId();

				switch(id) {
					case kATUICursorImage_Arrow:
						::SetCursor(::LoadCursor(NULL, IDC_ARROW));
						return TRUE;

					case kATUICursorImage_IBeam:
						::SetCursor(::LoadCursor(NULL, IDC_IBEAM));
						return TRUE;
				}

				if (x == mLastHoverX || y == mLastHoverY) {
					::SetCursor(NULL);
					return TRUE;
				}

				if (!g_mouseAutoCapture || !g_sim.GetInputManager()->IsMouseMapped()) {
					POINT pt = {x, y};
					::ScreenToClient(mhwnd, &pt);

					int xs, ys;
					if (MapPixelToBeamPosition(pt.x, pt.y, xs, ys, false) && GetModeLineYPos(ys, true) >= 0) {
						::SetCursor(::LoadCursor(NULL, GetKeyState(VK_SHIFT)<0 ? IDC_HELP : IDC_IBEAM));
						return TRUE;
					}
				}

				if (!g_sim.IsRunning()) {
					::SetCursor(::LoadCursor(NULL, IDC_NO));
					return TRUE;
				}
			}
			break;

		case WM_MOUSEMOVE:
			SetHaveMouse();

			if (mbDragActive) {
				int x = (short)LOWORD(lParam);
				int y = (short)HIWORD(lParam);

				UpdateDragPreview(x, y);
			} else if ((wParam & MK_SHIFT) && !g_sim.IsRunning() && ATIsDebugConsoleActive() && ::GetFocus() == mhwnd) {
				int x = (short)LOWORD(lParam);
				int y = (short)HIWORD(lParam);

				SetCoordinateIndicator(x, y);
			} else {
				int x = (short)LOWORD(lParam);
				int y = (short)HIWORD(lParam);

				if (g_ATUIManager.OnMouseMove(x, y)) {
					mLastHoverX = INT_MIN;
					mLastHoverY = INT_MIN;
					break;
				}

				if (g_fullscreen && y < mpMenuBar->GetArea().height()) {
					ATUIUpdateMenu();
					ATUpdatePortMenus();
					mpMenuBar->SetVisible(true);
				}

				if (g_sim.GetInputManager()->IsMouseAbsoluteMode()) {
					UpdateMousePosition(x, y);
				} else if (::GetCapture() == mhwnd) {
					int dx = x - mLastTrackMouseX;
					int dy = y - mLastTrackMouseY;

					if (dx | dy) {
						ATInputManager *im = g_sim.GetInputManager();

						im->OnMouseMove(0, dx, dy);

						WarpCapturedMouse();
					}
				} else if (mbHoverTipActive) {
					if (!mHoverTipArea.contains(vdpoint32(x, y))) {
						g_sim.GetUIRenderer()->SetHoverTip(0, 0, NULL);
						mbHoverTipActive = false;
					}
				} else {
					if (g_ATUIManager.IsCursorCaptured()) {
						mLastHoverX = INT_MIN;
						mLastHoverY = INT_MIN;
					} else {
						if (x != mLastHoverX || y != mLastHoverY) {
							mLastHoverX = INT_MIN;
							mLastHoverY = INT_MIN;
						}

						TRACKMOUSEEVENT tme = {sizeof(TRACKMOUSEEVENT)};
						tme.dwFlags = TME_HOVER;
						tme.hwndTrack = mhwnd;
						tme.dwHoverTime = HOVER_DEFAULT;

						TrackMouseEvent(&tme);
					}
				}
			}
			break;

		case WM_MOUSEHOVER:
			OnMouseHover(wParam);
			return 0;

		case WM_MOUSELEAVE:
			mbHaveMouse = false;
			ClearCoordinateIndicator();
			if (mbHoverTipActive) {
				g_sim.GetUIRenderer()->SetHoverTip(0, 0, NULL);
			}

			g_ATUIManager.OnMouseLeave();
			return 0;

		case WM_KILLFOCUS:
			if (::GetCapture() == mhwnd)
				::ReleaseCapture();

			mbCoordIndicatorEnabled = false;
			ClearCoordinateIndicator();

			if (mbDragActive) {
				mbDragActive = false;
			}

			if (g_mouseClipped) {
				g_mouseClipped = false;
				g_winCaptionUpdater.SetMouseCaptured(g_mouseClipped || g_mouseCaptured);

				::ClipCursor(NULL);
			}
			break;

		case WM_CAPTURECHANGED:
			mbDragActive = false;
			g_mouseCaptured = false;
			g_winCaptionUpdater.SetMouseCaptured(g_mouseClipped || g_mouseCaptured);
			break;

		case WM_ERASEBKGND:
			return 0;

		case WM_PAINT:
			if (mbTextModeEnabled && mpEnhTextEngine) {
				PAINTSTRUCT ps;
				if (HDC hdc = BeginPaint(mhwnd, &ps)) {
					mpEnhTextEngine->Paint(hdc);
					EndPaint(mhwnd, &ps);
				}
				return 0;
			}
			break;

		case WM_COMMAND:
			switch(LOWORD(wParam)) {
				case ID_DISPLAYCONTEXTMENU_COPY:
					Copy();
					return 0;

				case ID_DISPLAYCONTEXTMENU_PASTE:
					Paste();
					return 0;
			}
			break;

		case WM_COPY:
			Copy();
			return 0;

		case WM_PASTE:
			Paste();
			return 0;

		case ATWM_GETAUTOSIZE:
			{
				vdsize32& sz = *(vdsize32 *)lParam;

				sz.w = mDisplayRect.width();
				sz.h = mDisplayRect.height();
				return TRUE;
			}
			break;
	}

	return ATUIPane::WndProc(msg, wParam, lParam);
}

bool ATDisplayPane::OnCreate() {
	if (!ATUIPane::OnCreate())
		return false;

	mhcurTarget = (HCURSOR)LoadImage(VDGetLocalModuleHandleW32(), MAKEINTRESOURCE(IDC_TARGET), IMAGE_CURSOR, 0, 0, LR_SHARED); 
	mhcurTargetOff = (HCURSOR)LoadImage(VDGetLocalModuleHandleW32(), MAKEINTRESOURCE(IDC_TARGET_OFF), IMAGE_CURSOR, 0, 0, LR_SHARED); 

	mhwndDisplay = (HWND)VDCreateDisplayWindowW32(WS_EX_NOPARENTNOTIFY, WS_CHILD|WS_VISIBLE, 0, 0, 0, 0, (VDGUIHandle)mhwnd);
	if (!mhwndDisplay)
		return false;

	mpDisplay = VDGetIVideoDisplay((VDGUIHandle)mhwndDisplay);
	g_pDisplay = mpDisplay;

	mpDisplay->SetReturnFocus(true);
	UpdateFilterMode();
	mpDisplay->SetAccelerationMode(IVDVideoDisplay::kAccelResetInForeground);
	mpDisplay->SetCompositor(&g_ATUIManager);

	// We need to push in an initial frame for two reasons: (1) black out immediately, (2) force full
	// screen mode to size the window correctly.
	mpDisplay->SetSourceSolidColor(0);

	g_ATUINativeDisplay.SetDisplay(mhwnd, ATFrameWindow::GetFrameWindow(GetParent(mhwnd)));

	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	gtia.SetVideoOutput(mpDisplay);
	gtia.UpdateScreen(true, true);

	mpMenuBar = new ATUIMenuList;
	mpMenuBar->SetVisible(false);
	mpMenuBar->SetFont(g_ATUIManager.GetThemeFont(kATUIThemeFont_Menu));
	mpMenuBar->SetMenu(ATUIGetMenu());
	mpMenuBar->OnItemSelected() += mDelItemSelected.Bind(this, &ATDisplayPane::OnMenuItemSelected);

	g_ATUIManager.GetMainWindow()->AddChild(mpMenuBar);

#if 0
	ATUITextEdit *te = new ATUITextEdit;
	g_ATUIManager.GetMainWindow()->AddChild(te);
	te->SetArea(vdrect32(16, 32, 640, 64));
	te->SetFillColor(0xD4DCD0);

	ATUIButton *b = new ATUIButton;
	g_ATUIManager.GetMainWindow()->AddChild(b);
	b->SetArea(vdrect32(16, 64, 80, 96));
	b->SetText(L"OK");

	ATUIListView *lv = new ATUIListView;
	g_ATUIManager.GetMainWindow()->AddChild(lv);
	lv->SetArea(vdrect32(16, 100, 300, 500));
#elif 0
	ATUIFileBrowser *fb = new ATUIFileBrowser;
	fb->SetDockMode(kATUIDockMode_Fill);
	g_ATUIManager.GetMainWindow()->AddChild(fb);
#endif

	return true;
}

void ATDisplayPane::OnDestroy() {
	if (mpEnhTextEngine) {
		mpEnhTextEngine->Shutdown();
		mpEnhTextEngine = NULL;
	}

	if (mpDisplay) {
		g_ATUINativeDisplay.SetDisplay(NULL, NULL);
		g_sim.GetGTIA().SetVideoOutput(NULL);

		mpDisplay->Destroy();
		mpDisplay = NULL;
		g_pDisplay = NULL;
		mhwndDisplay = NULL;
	}

	ATUIPane::OnDestroy();
}

void ATDisplayPane::OnMouseHover(WPARAM wParam) {
	DWORD pos = ::GetMessagePos();

	int x = (short)LOWORD(pos);
	int y = (short)HIWORD(pos);

	if (wParam & MK_SHIFT) {
		// tooltip request -- let's try to grab text
		int xc;
		int yc;

		POINT pt = {x, y};
		ScreenToClient(mhwnd, &pt);
		bool valid = false;

		if (MapPixelToBeamPosition(pt.x, pt.y, xc, yc, false)) {
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
					msg = L"No help for this message.";

				if (!msg.empty()) {
					int xp1, xp2, yp1, yp2;
					MapBeamPositionToPixel(0, ymode, xp1, yp1);

					ATAnticEmulator& antic = g_sim.GetAntic();
					const ATAnticEmulator::DLHistoryEntry *dlhist = antic.GetDLHistory();
					while(++ymode < 248 && !dlhist[ymode].mbValid)
						;

					MapBeamPositionToPixel(228, ymode, xp2, yp2);

					mHoverTipArea.set(xp1, yp1, xp2, yp2);

					g_sim.GetUIRenderer()->SetHoverTip(pt.x, pt.y, msg.c_str());
					mbHoverTipActive = true;
					valid = true;
				}
			}
		}

		if (!valid) {
			g_sim.GetUIRenderer()->SetHoverTip(pt.x, pt.y, NULL);
			mbHoverTipActive = false;
		}
	} else if (!mbDragActive && !g_ATUIManager.IsCursorCaptured()) {
		mLastHoverX = x;
		mLastHoverY = y;

		::SetCursor(NULL);
	}
}

void ATDisplayPane::CaptureMouse() {
	ATInputManager *im = g_sim.GetInputManager();

	if (im->IsMouseMapped() && g_sim.IsRunning()) {
		if (im->IsMouseAbsoluteMode()) {
			RECT r;

			if (::GetClientRect(mhwnd, &r)) {
				::MapWindowPoints(mhwnd, NULL, (LPPOINT)&r, 2);
				::ClipCursor(&r);

				g_mouseClipped = true;
				g_winCaptionUpdater.SetMouseCaptured(g_mouseClipped || g_mouseCaptured);
			}
		} else {
			::SetCapture(mhwnd);
			::SetCursor(NULL);

			g_mouseCaptured = true;
			g_winCaptionUpdater.SetMouseCaptured(g_mouseClipped || g_mouseCaptured);

			WarpCapturedMouse();
		}
	}
}

void ATDisplayPane::ResetDisplay() {
	if (mpDisplay)
		mpDisplay->Reset();
}

void ATDisplayPane::Copy() {
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

	if (::OpenClipboard((HWND)mhwnd)) {
		if (::EmptyClipboard()) {
			HANDLE hMem;
			void *lpvMem;

			if (hMem = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, s.size() + 1)) {
				if (lpvMem = ::GlobalLock(hMem)) {
					memcpy(lpvMem, s.c_str(), s.size() + 1);

					::GlobalUnlock(lpvMem);
					::SetClipboardData(CF_TEXT, hMem);
					::CloseClipboard();
					return;
				}
				::GlobalFree(hMem);
			}
		}
		::CloseClipboard();
	}
}

void ATDisplayPane::Paste() {
	OnCommandEditPasteText();
}

void ATDisplayPane::OnSize() {
	RECT r;
	GetClientRect(mhwnd, &r);

	if (g_mouseClipped) {
		RECT rs = r;

		MapWindowPoints(mhwnd, NULL, (LPPOINT)&rs, 2);
		ClipCursor(&rs);
	}

	if (mhwndDisplay) {
		if (mpDisplay) {
			UpdateFilterMode();

			vdrect32 rd(r.left, r.top, r.right, r.bottom);
			sint32 w = rd.width();
			sint32 h = rd.height();

			int sw = 1;
			int sh = 1;
			g_sim.GetGTIA().GetFrameSize(sw, sh);

			if (w && h) {
				if (g_displayStretchMode == kATDisplayStretchMode_PreserveAspectRatio || g_displayStretchMode == kATDisplayStretchMode_IntegralPreserveAspectRatio) {
					const bool pal = g_sim.GetVideoStandard() != kATVideoStandard_NTSC;
					const float desiredAspect = (pal ? 1.03964f : 0.857141f);
					const float fsw = (float)sw * desiredAspect;
					const float fsh = (float)sh;
					const float fw = (float)w;
					const float fh = (float)h;
					float zoom = std::min<float>(fw / fsw, fh / fsh);

					if (g_displayStretchMode == kATDisplayStretchMode_IntegralPreserveAspectRatio && zoom > 1)
						zoom = floorf(zoom);

					sint32 w2 = (sint32)(0.5f + fsw * zoom);
					sint32 h2 = (sint32)(0.5f + fsh * zoom);

					rd.left		= (w - w2) >> 1;
					rd.right	= rd.left + w2;
					rd.top		= (h - h2) >> 1;
					rd.bottom	= rd.top + h2;
				} else if (g_displayStretchMode == kATDisplayStretchMode_SquarePixels || g_displayStretchMode == kATDisplayStretchMode_Integral) {
					int ratio = std::min<int>(w / sw, h / sh);

					if (ratio < 1 || g_displayStretchMode == kATDisplayStretchMode_SquarePixels) {
						if (w*sh < h*sw) {		// (w / sw) < (h / sh) -> w*sh < h*sw
							// width is smaller ratio -- compute restricted height
							int restrictedHeight = (sh * w + (sw >> 1)) / sw;

							rd.top = (h - restrictedHeight) >> 1;
							rd.bottom = rd.top + restrictedHeight;
						} else {
							// height is smaller ratio -- compute restricted width
							int restrictedWidth = (sw * h + (sh >> 1)) / sh;

							rd.left = (w - restrictedWidth) >> 1;
							rd.right = rd.left+ restrictedWidth;
						}
					} else {
						int finalWidth = sw * ratio;
						int finalHeight = sh * ratio;

						rd.left = (w - finalWidth) >> 1;
						rd.right = rd.left + finalWidth;

						rd.top = (h - finalHeight) >> 1;
						rd.bottom = rd.top + finalHeight;
					}
				}
			}

			mDisplayRect = rd;
			mpDisplay->SetDestRect(&rd, 0);
			UpdateDragPreviewRects();
		}

		SetWindowPos(mhwndDisplay, NULL, 0, 0, r.right, r.bottom, SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
	}

	if (mbTextModeEnabled) {
		if (mpEnhTextEngine)
			mpEnhTextEngine->OnSize(r.right, r.bottom);

		InvalidateRect(mhwnd, NULL, TRUE);
	}
}

void ATDisplayPane::UpdateFilterMode() {
	if (!mpDisplay)
		return;

	switch(g_dispFilterMode) {
		case kATDisplayFilterMode_Point:
			mpDisplay->SetFilterMode(IVDVideoDisplay::kFilterPoint);
			mpDisplay->SetPixelSharpness(1.0f, 1.0f);
			break;

		case kATDisplayFilterMode_Bilinear:
			mpDisplay->SetFilterMode(IVDVideoDisplay::kFilterBilinear);
			mpDisplay->SetPixelSharpness(1.0f, 1.0f);
			break;

		case kATDisplayFilterMode_SharpBilinear:
			mpDisplay->SetFilterMode(IVDVideoDisplay::kFilterBilinear);
			{
				ATGTIAEmulator& gtia = g_sim.GetGTIA();
				int pw = 2;
				int ph = 2;

				gtia.GetPixelAspectMultiple(pw, ph);

				static const float kFactors[5] = { 1.259f, 1.587f, 2.0f, 2.520f, 3.175f };

				const float factor = kFactors[std::max(0, std::min(4, g_dispFilterSharpness + 2))];

				mpDisplay->SetPixelSharpness(std::max(1.0f, factor / (float)pw), std::max(1.0f, factor / (float)ph));
			}
			break;

		case kATDisplayFilterMode_Bicubic:
			mpDisplay->SetFilterMode(IVDVideoDisplay::kFilterBicubic);
			mpDisplay->SetPixelSharpness(1.0f, 1.0f);
			break;

		case kATDisplayFilterMode_AnySuitable:
			mpDisplay->SetFilterMode(IVDVideoDisplay::kFilterAnySuitable);
			mpDisplay->SetPixelSharpness(1.0f, 1.0f);
			break;
	}
}

void ATDisplayPane::SetHaveMouse() {
	if (!mbHaveMouse) {
		mbHaveMouse = true;

		TRACKMOUSEEVENT tme = {sizeof(TRACKMOUSEEVENT)};
		tme.dwFlags = TME_LEAVE;
		tme.hwndTrack = mhwnd;
		::TrackMouseEvent(&tme);
	}
}

void ATDisplayPane::WarpCapturedMouse() {
	RECT r;

	if (::GetClientRect(mhwnd, &r)) {
		mLastTrackMouseX = r.right >> 1;
		mLastTrackMouseY = r.bottom >> 1;
		POINT pt = {mLastTrackMouseX, mLastTrackMouseY};
		::ClientToScreen(mhwnd, &pt);
		::SetCursorPos(pt.x, pt.y);
	}
}

void ATDisplayPane::UpdateTextDisplay(bool enabled) {
	if (!enabled) {
		if (mbTextModeEnabled) {
			mbTextModeEnabled = false;

			if (mpEnhTextEngine) {
				mpEnhTextEngine->Shutdown();
				mpEnhTextEngine = NULL;
			}

			::ShowWindow(mhwndDisplay, SW_SHOWNOACTIVATE);
		}

		return;
	}

	bool forceInvalidate = false;
	if (!mbTextModeEnabled) {
		mbTextModeEnabled = true;

		if (!mpEnhTextEngine) {
			mpEnhTextEngine = ATUICreateEnhancedTextEngine();
			mpEnhTextEngine->Init(mhwnd, &g_sim);
		}

		UpdateTextModeFont();

		forceInvalidate = true;

		::ShowWindow(mhwndDisplay, SW_HIDE);
	}

	if (mpEnhTextEngine)
		mpEnhTextEngine->Update(forceInvalidate);
}

void ATDisplayPane::UpdateTextModeFont() {
	if (mpEnhTextEngine)
		mpEnhTextEngine->SetFont(&g_enhancedTextFont);
}

void ATDisplayPane::UpdateMousePosition(int x, int y) {
	int padX = 0;
	int padY = 0;

	if (mWidth)
		padX = VDRoundToInt(x * 131072.0f / ((float)mWidth - 1) - 0x10000);

	if (mHeight)
		padY = VDRoundToInt(y * 131072.0f / ((float)mHeight - 1) - 0x10000);

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

bool ATDisplayPane::MapPixelToBeamPosition(int x, int y, float& hcyc, float& vcyc, bool clamp) const {
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

bool ATDisplayPane::MapPixelToBeamPosition(int x, int y, int& xc, int& yc, bool clamp) const {
	float xf, yf;

	if (!MapPixelToBeamPosition(x, y, xf, yf, clamp))
		return false;

	xc = (int)floorf(xf + 0.5f);
	yc = (int)floorf(yf + 0.5f);
	return true;
}

void ATDisplayPane::MapBeamPositionToPixel(int xc, int yc, int& x, int& y) const {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	const vdrect32 scanArea(gtia.GetFrameScanArea());

	// map position to cycles
	x = VDRoundToInt(((float)xc + 0.5f - (float)scanArea.left) * (float)mDisplayRect.width()  / (float)scanArea.width() ) + mDisplayRect.left;
	y = VDRoundToInt(((float)yc + 0.5f - (float)scanArea.top ) * (float)mDisplayRect.height() / (float)scanArea.height()) + mDisplayRect.top;
}

void ATDisplayPane::UpdateDragPreview(int x, int y) {
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

void ATDisplayPane::UpdateDragPreviewRects() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	const vdrect32 scanArea(gtia.GetFrameScanArea());

	float scaleX = (float)mDisplayRect.width() / (float)scanArea.width();
	float scaleY = (float)mDisplayRect.height() / (float)scanArea.height();

	g_pATVideoDisplayWindow->ClearXorRects();

	TextSpans::const_iterator it(mDragPreviewSpans.begin()), itEnd(mDragPreviewSpans.end());
	
	for(; it != itEnd; ++it) {
		const TextSpan& ts = *it;

		int hiX1 = ts.mX - scanArea.left;
		int hiY1 = ts.mY - scanArea.top;
		int hiX2 = hiX1 + ts.mWidth;
		int hiY2 = hiY1 + ts.mHeight;

		g_pATVideoDisplayWindow->AddXorRect(
			VDRoundToInt(hiX1 * scaleX) + mDisplayRect.left,
			VDRoundToInt(hiY1 * scaleY) + mDisplayRect.top,
			VDRoundToInt(hiX2 * scaleX) + mDisplayRect.left,
			VDRoundToInt(hiY2 * scaleY) + mDisplayRect.top);
	}
}

void ATDisplayPane::ClearDragPreview() {
	mDragPreviewSpans.clear();

	g_pATVideoDisplayWindow->ClearXorRects();
}

int ATDisplayPane::GetModeLineYPos(int ys, bool checkValidCopyText) const {
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
int ATDisplayPane::ReadText(char *dst, int yc, int startChar, int numChars) const {
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

void ATDisplayPane::ClearCoordinateIndicator() {
	if (mbCoordIndicatorActive) {
		mbCoordIndicatorActive = false;

		IATUIRenderer *uir = g_sim.GetUIRenderer();
		if (uir)
			uir->SetHoverTip(0, 0, NULL);

		if (mpDisplay)
			mpDisplay->Invalidate();

		if (mbHaveMouse) {
			POINT pt;
			if (GetCursorPos(&pt))
				SetCursorPos(pt.x, pt.y);
		}
	}
}

void ATDisplayPane::SetCoordinateIndicator(int x, int y) {
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

	if (mpDisplay)
		mpDisplay->Invalidate();
}

void ATDisplayPane::OnMenuItemSelected(ATUIMenuList *sender, uint32 id) {
	::OnCommand(id);
}

///////////////////////////////////////////////////////////////////////////

void ATUIRegisterDisplayPane() {
	ATRegisterUIPaneType(kATUIPaneId_Display, VDRefCountObjectFactory<ATDisplayPane, ATUIPane>);
}
