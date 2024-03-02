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
#include "oshelper.h"
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
#include "uivideodisplaywindow.h"
#include "uimessagebox.h"
#include "uiqueue.h"
#include "resource.h"

extern ATSimulator g_sim;

extern IVDVideoDisplay *g_pDisplay;

extern bool g_ATAutoFrameFlipping;
extern LOGFONTW g_enhancedTextFont;

extern ATDisplayFilterMode g_dispFilterMode;
extern int g_dispFilterSharpness;

extern ATDisplayStretchMode g_displayStretchMode;

extern ATUIWindowCaptionUpdater g_winCaptionUpdater;

ATUIManager g_ATUIManager;

void OnCommandEditPasteText();
bool OnCommand(UINT id);

bool ATUIProcessMessages(bool waitForMessage, int& returnCode);
void ATUIFlushDisplay();

///////////////////////////////////////////////////////////////////////////

ATUIVideoDisplayWindow *g_pATVideoDisplayWindow;

///////////////////////////////////////////////////////////////////////////

struct ATUIStepDoModalWindow : public vdrefcount {
	ATUIStepDoModalWindow()
		: mbModalWindowActive(true)
	{
	}

	void Run() {
		if (mbModalWindowActive) {
			ATUIPushStep(ATUIStep::FromMethod<ATUIStepDoModalWindow, &ATUIStepDoModalWindow::Run>(this));

			int rc;
			ATUIProcessMessages(true, rc);
		}
	}

	bool mbModalWindowActive;
};

class ATUINativeDisplay : public IATUINativeDisplay, public IATUIClipboard {
public:
	ATUINativeDisplay()
		: mhwnd(NULL)
		, mpFrame(NULL)
		, mModalCount(0)
		, mbMouseConstrained(false)
		, mbMouseCaptured(false)
		, mbMouseMotionMode(false)
		, mbCursorImageChangesEnabled(false)
		, mhcurTarget((HCURSOR)LoadImage(VDGetLocalModuleHandleW32(), MAKEINTRESOURCE(IDC_TARGET), IMAGE_CURSOR, 0, 0, LR_SHARED))
		, mhcurTargetOff((HCURSOR)LoadImage(VDGetLocalModuleHandleW32(), MAKEINTRESOURCE(IDC_TARGET_OFF), IMAGE_CURSOR, 0, 0, LR_SHARED))
	{
	}

	virtual void Invalidate() {
		if (g_pDisplay && (!g_ATAutoFrameFlipping || mModalCount))
			g_pDisplay->Invalidate();
	}

	virtual void ConstrainCursor(bool constrain) {
		if (constrain) {
			if (mhwnd) {
				RECT r;

				if (::GetClientRect(mhwnd, &r)) {
					::MapWindowPoints(mhwnd, NULL, (LPPOINT)&r, 2);
					::ClipCursor(&r);
				}
			}
			mbMouseConstrained = true;
		} else {
			::ClipCursor(NULL);
			mbMouseConstrained = false;
		}
	}

	virtual void CaptureCursor(bool motionMode) {
		if (!mbMouseCaptured) {
			if (mhwnd)
				::SetCapture(mhwnd);

			mbMouseCaptured = true;
		}

		mbMouseMotionMode = motionMode;
	}

	virtual void ReleaseCursor() {
		mbMouseCaptured = false;
		mbMouseMotionMode = false;
		mbMouseConstrained = false;

		if (mhwnd)
			::ReleaseCapture();
	}

	virtual vdpoint32 GetCursorPosition() {
		if (!mhwnd)
			return vdpoint32(0, 0);

		DWORD pos = GetMessagePos();
		POINT pt = { (short)LOWORD(pos), (short)HIWORD(pos) };

		::ScreenToClient(mhwnd, &pt);

		return vdpoint32(pt.x, pt.y);
	}

	virtual void SetCursorImage(uint32 id) {
		if (mbCursorImageChangesEnabled)
			SetCursorImageDirect(id);
	}

	virtual void *BeginModal() {
		ATUIStepDoModalWindow *step = new ATUIStepDoModalWindow;
		step->AddRef();

		ATUIPushStep(ATUIStep::FromMethod<ATUIStepDoModalWindow, &ATUIStepDoModalWindow::Run>(step));

		if (!mModalCount++) {
			ATUISetMenuEnabled(false);

			if (mpFrame)
				mpFrame->GetContainer()->SetModalFrame(mpFrame);
		}

		ATUIFlushDisplay();

		return step;
	}

	virtual void EndModal(void *cookie) {
		VDASSERT(mModalCount);

		if (!--mModalCount) {
			if (mpFrame)
				mpFrame->GetContainer()->SetModalFrame(NULL);

			ATUISetMenuEnabled(true);
		}

		ATUIStepDoModalWindow *step = (ATUIStepDoModalWindow *)cookie;
		step->mbModalWindowActive = false;
		step->Release();
	}

	virtual bool IsKeyDown(uint32 vk) {
		return GetKeyState(vk) < 0;
	}

	virtual IATUIClipboard *GetClipboard() {
		return this;
	}

public:
	virtual void CopyText(const char *s) {
		if (mhwnd)
			ATCopyTextToClipboard(mhwnd, s);
	}

public:
	bool IsMouseCaptured() const { return mModalCount > 0; }
	bool IsMouseConstrained() const { return mbMouseConstrained; }
	bool IsMouseMotionModeEnabled() const { return mbMouseMotionMode; }

	void SetDisplay(HWND hwnd, ATFrameWindow *frame) {
		if (mpFrame && mModalCount)
			mpFrame->GetContainer()->SetModalFrame(NULL);

		mhwnd = hwnd;
		mpFrame = frame;

		if (mpFrame && mModalCount)
			mpFrame->GetContainer()->SetModalFrame(mpFrame);
	}

	void OnCaptureLost() {
		mbMouseCaptured = false;
		mbMouseMotionMode = false;
		mbMouseConstrained = false;
	}

	void SetCursorImageChangesEnabled(bool enabled) {
		mbCursorImageChangesEnabled = enabled;
	}

	void SetCursorImageDirect(uint32 id) {
		switch(id) {
			case kATUICursorImage_Hidden:
				::SetCursor(NULL);
				break;

			case kATUICursorImage_Arrow:
				::SetCursor(::LoadCursor(NULL, IDC_ARROW));
				break;

			case kATUICursorImage_IBeam:
				::SetCursor(::LoadCursor(NULL, IDC_IBEAM));
				break;

			case kATUICursorImage_Cross:
				::SetCursor(::LoadCursor(NULL, IDC_CROSS));
				break;

			case kATUICursorImage_Query:
				::SetCursor(::LoadCursor(NULL, IDC_HELP));
				break;

			case kATUICursorImage_Target:
				::SetCursor(mhcurTarget);
				break;

			case kATUICursorImage_TargetOff:
				::SetCursor(mhcurTargetOff);
				break;
		}
	}

private:
	HWND mhwnd;
	ATFrameWindow *mpFrame;
	uint32 mModalCount;
	bool mbMouseCaptured;
	bool mbMouseConstrained;
	bool mbMouseMotionMode;
	bool mbCursorImageChangesEnabled;
	HCURSOR	mhcurTarget;
	HCURSOR	mhcurTargetOff;
} g_ATUINativeDisplay;

///////////////////////////////////////////////////////////////////////////

void ATUIInitManager() {
	g_ATUIManager.Init(&g_ATUINativeDisplay);

	g_pATVideoDisplayWindow = new ATUIVideoDisplayWindow;
	g_pATVideoDisplayWindow->AddRef();
	g_pATVideoDisplayWindow->Init(*g_sim.GetEventManager());
	g_ATUIManager.GetMainWindow()->AddChild(g_pATVideoDisplayWindow);
	g_pATVideoDisplayWindow->SetDockMode(kATUIDockMode_Fill);

	g_sim.GetUIRenderer()->SetUIManager(&g_ATUIManager);

	g_pATVideoDisplayWindow->Focus();

//	ATUIProfileCreateWindow(&g_ATUIManager);
}

void ATUIShutdownManager() {
	ATUIProfileDestroyWindow();

	vdsaferelease <<= g_pATVideoDisplayWindow;

	g_sim.GetUIRenderer()->SetUIManager(NULL);

	g_ATUIManager.Shutdown();
}

void ATUITriggerButtonDown(uint32 vk) {
	ATUIKeyEvent event;
	event.mVirtKey = vk;
	event.mExtendedVirtKey = vk;
	event.mbIsRepeat = false;
	event.mbIsExtendedKey = false;
	g_ATUIManager.OnKeyDown(event);
}

void ATUITriggerButtonUp(uint32 vk) {
	ATUIKeyEvent event;
	event.mVirtKey = vk;
	event.mExtendedVirtKey = vk;
	event.mbIsRepeat = false;
	event.mbIsExtendedKey = false;
	g_ATUIManager.OnKeyUp(event);
}

void ATUIFlushDisplay() {
	if (g_ATUIManager.IsInvalidated()) {
		if (g_pDisplay)
			g_pDisplay->Invalidate();
	}
}

bool ATUIIsActiveModal() {
	return g_ATUIManager.GetModalWindow() != NULL;
}

class ATDisplayPane : public ATUIPane, public IATDisplayPane {
public:
	ATDisplayPane();
	~ATDisplayPane();

	void *AsInterface(uint32 iid);

	void ReleaseMouse();
	void ToggleCaptureMouse();
	void ResetDisplay();

	bool IsTextSelected() const { return g_pATVideoDisplayWindow->IsTextSelected(); }
	void Copy();
	void Paste();

	void OnSize();
	void UpdateFilterMode();

	bool IsXEPViewEnabled();
	void SetXEPViewEnabled(bool enabled);

	bool IsXEPViewAutoswitchEnabled();
	void SetXEPViewAutoswitchEnabled(bool enabled);

	void OnHardwareChanged();

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	bool OnCreate();
	void OnDestroy();
	void OnMouseMove(WPARAM wParam, LPARAM lParam);
	void OnMouseHover(WPARAM wParam);

	ATUIKeyEvent ConvertVirtKeyEvent(WPARAM wParam, LPARAM lParam);

	void SetHaveMouse();
	void WarpCapturedMouse();
	void UpdateTextDisplay(bool enabled);
	void UpdateTextModeFont();

	void OnMenuActivated(ATUIMenuList *);
	void OnMenuItemSelected(ATUIMenuList *sender, uint32 id);
	void OnAllowContextMenu();
	void OnDisplayContextMenu(const vdpoint32& pt);

	HWND	mhwndDisplay;
	HMENU	mhmenuContext;
	IVDVideoDisplay *mpDisplay;
	int		mLastTrackMouseX;
	int		mLastTrackMouseY;
	int		mMouseCursorLock;
	int		mWidth;
	int		mHeight;

	vdrect32	mDisplayRect;

	bool	mbTextModeEnabled;
	bool	mbHaveMouse;
	bool	mbEatNextInjectedCaps;
	bool	mbTurnOffCaps;
	bool	mbAllowContextMenu;

	vdautoptr<IATUIEnhancedTextEngine> mpEnhTextEngine;
	vdrefptr<ATUIMenuList> mpMenuBar;
};

ATDisplayPane::ATDisplayPane()
	: ATUIPane(kATUIPaneId_Display, L"Display")
	, mhwndDisplay(NULL)
	, mhmenuContext(LoadMenu(NULL, MAKEINTRESOURCE(IDR_DISPLAY_CONTEXT_MENU)))
	, mpDisplay(NULL)
	, mLastTrackMouseX(0)
	, mLastTrackMouseY(0)
	, mMouseCursorLock(0)
	, mWidth(0)
	, mHeight(0)
	, mDisplayRect(0,0,0,0)
	, mbTextModeEnabled(false)
	, mbHaveMouse(false)
	, mbEatNextInjectedCaps(false)
	, mbTurnOffCaps(false)
	, mbAllowContextMenu(false)
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
		case ATWM_PREKEYDOWN:
			if (mbEatNextInjectedCaps && LOWORD(wParam) == VK_CAPITAL) {
				// drop injected CAPS LOCK keys
				if (!(lParam & 0x00ff0000))
					return true;
			}

			if (g_ATUIManager.OnKeyDown(ConvertVirtKeyEvent(wParam, lParam))) {
				if (LOWORD(wParam) == VK_CAPITAL)
					mbTurnOffCaps = true;

				return true;
			}
			return false;


		case ATWM_PRESYSKEYUP:
		case ATWM_PREKEYUP:
			if (LOWORD(wParam) == VK_CAPITAL && mbTurnOffCaps && (GetKeyState(VK_CAPITAL) & 1)) {
				mbTurnOffCaps = false;

				// force caps lock back off
				mbEatNextInjectedCaps = true;

				keybd_event(VK_CAPITAL, 0, 0, 0);
				keybd_event(VK_CAPITAL, 0, KEYEVENTF_KEYUP, 0);
			}

			return g_ATUIManager.OnKeyUp(ConvertVirtKeyEvent(wParam, lParam));

		case ATWM_SETFULLSCREEN:
			if (mpMenuBar)
				mpMenuBar->SetVisible(wParam != 0);

			return 0;

		case WM_SIZE:
			{
				int w = LOWORD(lParam);
				int h = HIWORD(lParam);

				if (w != mWidth || h != mHeight) {
					mWidth = w;
					mHeight = h;

					g_ATUIManager.Resize(w, h);

					IATUIRenderer *r = g_sim.GetUIRenderer();

					if (r)
						r->Relayout(w, h);
				}
			}
			break;

		case WM_CHAR:
			{
				ATUICharEvent event;
				event.mCh = wParam;
				event.mbIsRepeat = (lParam & 0x40000000) != 0;

				if (g_ATUIManager.OnChar(event))
					return 0;
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
			g_ATUIManager.OnMouseUp((short)LOWORD(lParam), (short)HIWORD(lParam), kATUIVK_LButton);
			break;

		case WM_MBUTTONUP:
			g_ATUIManager.OnMouseUp((short)LOWORD(lParam), (short)HIWORD(lParam), kATUIVK_MButton);
			break;

		case WM_RBUTTONUP:
			mbAllowContextMenu = false;
			g_ATUIManager.OnMouseUp((short)LOWORD(lParam), (short)HIWORD(lParam), kATUIVK_RButton);

			if (mbAllowContextMenu)
				break;

			return 0;

		case WM_XBUTTONUP:
			switch(HIWORD(wParam)) {
				case XBUTTON1:
					g_ATUIManager.OnMouseUp((short)LOWORD(lParam), (short)HIWORD(lParam), kATUIVK_XButton1);
					break;
				case XBUTTON2:
					g_ATUIManager.OnMouseUp((short)LOWORD(lParam), (short)HIWORD(lParam), kATUIVK_XButton2);
					break;
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

				switch(msg) {
					case WM_LBUTTONDOWN:
					case WM_LBUTTONDBLCLK:
						if (g_ATUIManager.OnMouseDown(x, y, kATUIVK_LButton, msg == WM_LBUTTONDBLCLK))
							return 0;
						break;

					case WM_RBUTTONDOWN:
					case WM_RBUTTONDBLCLK:
						if (g_ATUIManager.OnMouseDown(x, y, kATUIVK_RButton, false))
							return 0;
						break;

					case WM_MBUTTONDOWN:
					case WM_MBUTTONDBLCLK:
						if (g_ATUIManager.OnMouseDown(x, y, kATUIVK_MButton, false))
							return 0;
						break;

					case WM_XBUTTONDOWN:
					case WM_XBUTTONDBLCLK:
						switch(HIWORD(wParam)) {
							case XBUTTON1:
								if (g_ATUIManager.OnMouseDown(x, y, kATUIVK_XButton1, false))
									return 0;
								break;
							case XBUTTON2:
								if (g_ATUIManager.OnMouseDown(x, y, kATUIVK_XButton2, false))
									return 0;
								break;
						}
						break;
				}
			}
			break;

		case WM_MOUSEWHEEL:
			{
				int x = (short)LOWORD(lParam);
				int y = (short)HIWORD(lParam);
				int dz = (short)HIWORD(wParam);

				POINT pt = { x, y };
				ScreenToClient(mhwnd, &pt);

				UINT lines = 0;
				::SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &lines, FALSE);
				g_ATUIManager.OnMouseWheel(pt.x, pt.y, (float)dz / (float)WHEEL_DELTA * (float)lines);
			}
			break;

		case WM_CONTEXTMENU:
			{
				POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };

				if (pt.x == -1 && pt.y == -1) {
					g_ATUIManager.OnContextMenu(NULL);
					return 0;
				}

				ScreenToClient(mhwnd, &pt);

				if ((uint32)pt.x < (uint32)mWidth && (uint32)pt.y < (uint32)mHeight) {
					vdpoint32 pt2(pt.x, pt.y);
					g_ATUIManager.OnContextMenu(&pt2);
					return 0;
				}
			}
			break;

		case WM_SETCURSOR:
			if (mMouseCursorLock)
				break;

			{
				DWORD pos = ::GetMessagePos();
				int x = (short)LOWORD(pos);
				int y = (short)HIWORD(pos);

				uint32 id = g_ATUIManager.GetCurrentCursorImageId();

				if (id) {
					g_ATUINativeDisplay.SetCursorImageDirect(id);
					return 0;
				}
			}
			break;

		case WM_MOUSEMOVE:
			OnMouseMove(wParam, lParam);
			break;

		case WM_MOUSEHOVER:
			OnMouseHover(wParam);
			return 0;

		case WM_MOUSELEAVE:
			g_ATUINativeDisplay.SetCursorImageChangesEnabled(false);
			mbHaveMouse = false;

			g_ATUIManager.OnMouseLeave();
			return 0;

		case WM_SETFOCUS:
			// Remember that we are effectively nesting window managers here -- when the display
			// window gains focus, to our window manager it is like a Win32 window becoming
			// active.
			g_ATUIManager.SetForeground(true);
			break;

		case WM_KILLFOCUS:
			g_ATUIManager.SetForeground(false);

			if (g_ATUINativeDisplay.IsMouseConstrained())
				::ClipCursor(NULL);

			if (::GetCapture() == mhwnd)
				::ReleaseCapture();
			break;

		case WM_CAPTURECHANGED:
			if (g_ATUINativeDisplay.IsMouseCaptured()) {
				g_ATUINativeDisplay.OnCaptureLost();
				g_ATUIManager.OnCaptureLost();
			}
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
	g_ATUIManager.GetMainWindow()->AddChild(mpMenuBar);
	mpMenuBar->SetFont(g_ATUIManager.GetThemeFont(kATUIThemeFont_Menu));
	mpMenuBar->SetMenu(ATUIGetMenu());
	mpMenuBar->SetAutoHide(true);
	mpMenuBar->SetArea(vdrect32(0, 0, 0, 20));
	mpMenuBar->SetDockMode(kATUIDockMode_TopFloat);
	mpMenuBar->OnActivatedEvent() = ATBINDCALLBACK(this, &ATDisplayPane::OnMenuActivated);
	mpMenuBar->OnItemSelected() = ATBINDCALLBACK(this, &ATDisplayPane::OnMenuItemSelected);

	g_pATVideoDisplayWindow->SetXEP(g_sim.GetXEP80());

#if 0
	ATUIMessageBox *osk = new ATUIMessageBox;
	g_ATUIManager.GetMainWindow()->AddChild(osk);
	osk->SetFrameMode(kATUIFrameMode_Raised);
	osk->SetCaption(L"Foo bar");
	osk->SetText(L"WTF");
	osk->SetArea(vdrect32(100, 100, 600, 350));
	osk->ShowModal();
#elif 0
	ATUIFileBrowser *fb = new ATUIFileBrowser;
	fb->SetDockMode(kATUIDockMode_Fill);
	g_ATUIManager.GetMainWindow()->AddChild(fb);
	fb->Focus();
#endif

	g_pATVideoDisplayWindow->BindAction(kATUIVK_UIMenu, ATUIMenuList::kActionActivate, 0, mpMenuBar->GetInstanceId());
	g_pATVideoDisplayWindow->OnAllowContextMenuEvent() = ATBINDCALLBACK(this, &ATDisplayPane::OnAllowContextMenu);
	g_pATVideoDisplayWindow->OnDisplayContextMenuEvent() = ATBINDCALLBACK(this, &ATDisplayPane::OnDisplayContextMenu);

	OnHardwareChanged();
	return true;
}

void ATDisplayPane::OnDestroy() {
	if (g_pATVideoDisplayWindow) {
		g_pATVideoDisplayWindow->OnAllowContextMenuEvent() = ATCallbackHandler0<void>();
		g_pATVideoDisplayWindow->OnDisplayContextMenuEvent() = ATCallbackHandler1<void, const vdpoint32&>();
		g_pATVideoDisplayWindow->UnbindAllActions();
	}

	if (mpMenuBar) {
		mpMenuBar->Destroy();
		mpMenuBar.clear();
	}

	if (g_pATVideoDisplayWindow)
		g_pATVideoDisplayWindow->SetXEP(NULL);

	if (mpEnhTextEngine) {
		if (g_pATVideoDisplayWindow)
			g_pATVideoDisplayWindow->SetEnhancedTextEngine(NULL);

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

void ATDisplayPane::OnMouseMove(WPARAM wParam, LPARAM lParam) {
	const int x = (short)LOWORD(lParam);
	const int y = (short)HIWORD(lParam);

	SetHaveMouse();

	if (g_ATUINativeDisplay.IsMouseMotionModeEnabled()) {
		int dx = x - mLastTrackMouseX;
		int dy = y - mLastTrackMouseY;

		if (dx | dy) {
			g_ATUIManager.OnMouseRelativeMove(dx, dy);

			WarpCapturedMouse();
		}
	} else {
		TRACKMOUSEEVENT tme = {sizeof(TRACKMOUSEEVENT)};
		tme.dwFlags = TME_HOVER;
		tme.hwndTrack = mhwnd;
		tme.dwHoverTime = HOVER_DEFAULT;

		TrackMouseEvent(&tme);

		g_ATUIManager.OnMouseMove(x, y);
	}
}

void ATDisplayPane::OnMouseHover(WPARAM wParam) {
	if (!g_ATUINativeDisplay.IsMouseMotionModeEnabled()) {
		DWORD pos = ::GetMessagePos();

		int x = (short)LOWORD(pos);
		int y = (short)HIWORD(pos);

		POINT pt = { x, y };

		if (::ScreenToClient(mhwnd, &pt))
			g_ATUIManager.OnMouseHover(pt.x, pt.y);
	}
}

ATUIKeyEvent ATDisplayPane::ConvertVirtKeyEvent(WPARAM wParam, LPARAM lParam) {
	ATUIKeyEvent event;
	event.mVirtKey = LOWORD(wParam);
	event.mExtendedVirtKey = event.mVirtKey;
	event.mbIsRepeat = (HIWORD(lParam) & KF_REPEAT) != 0;
	event.mbIsExtendedKey = (HIWORD(lParam) & KF_EXTENDED) != 0;

	// Decode extended virt key.
	switch(event.mExtendedVirtKey) {
		case VK_RETURN:
			if (event.mbIsExtendedKey)
				event.mExtendedVirtKey = kATInputCode_KeyNumpadEnter;
			break;

		case VK_SHIFT:
			// Windows doesn't set the ext bit for RShift, so we have to use the scan
			// code instead.
			if (MapVirtualKey(LOBYTE(HIWORD(lParam)), 3) == VK_RSHIFT)
				event.mExtendedVirtKey = kATUIVK_RShift;
			else
				event.mExtendedVirtKey = kATUIVK_LShift;
			break;

		case VK_CONTROL:
			event.mExtendedVirtKey = event.mbIsExtendedKey ? kATUIVK_RControl : kATUIVK_LControl;
			break;

		case VK_MENU:
			event.mExtendedVirtKey = event.mbIsExtendedKey ? kATUIVK_RAlt : kATUIVK_LAlt;
			break;
	}

	return event;
}

void ATDisplayPane::ReleaseMouse() {
	if (g_pATVideoDisplayWindow)
		g_pATVideoDisplayWindow->ReleaseMouse();
}

void ATDisplayPane::ToggleCaptureMouse() {
	if (g_pATVideoDisplayWindow)
		g_pATVideoDisplayWindow->ToggleCaptureMouse();
}

void ATDisplayPane::ResetDisplay() {
	if (mpDisplay)
		mpDisplay->Reset();
}

void ATDisplayPane::Copy() {
	g_pATVideoDisplayWindow->Copy();

}

void ATDisplayPane::Paste() {
	OnCommandEditPasteText();
}

void ATDisplayPane::OnSize() {
	RECT r;
	GetClientRect(mhwnd, &r);

	if (g_ATUINativeDisplay.IsMouseConstrained()) {
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
			g_pATVideoDisplayWindow->SetDisplayRect(mDisplayRect);
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

void ATDisplayPane::OnHardwareChanged() {
	if (g_pATVideoDisplayWindow)
		g_pATVideoDisplayWindow->SetXEP(g_sim.GetXEP80());
}

void ATDisplayPane::SetHaveMouse() {
	if (!mbHaveMouse) {
		mbHaveMouse = true;

		g_ATUINativeDisplay.SetCursorImageChangesEnabled(true);

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
				g_pATVideoDisplayWindow->SetEnhancedTextEngine(NULL);
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
			g_pATVideoDisplayWindow->SetEnhancedTextEngine(mpEnhTextEngine);
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

void ATDisplayPane::OnMenuActivated(ATUIMenuList *) {
	ATUIUpdateMenu();
	ATUpdatePortMenus();
}

void ATDisplayPane::OnMenuItemSelected(ATUIMenuList *sender, uint32 id) {
	::OnCommand(id);
}

void ATDisplayPane::OnAllowContextMenu() {
	mbAllowContextMenu = true;
}

void ATDisplayPane::OnDisplayContextMenu(const vdpoint32& pt) {
	HMENU hmenuPopup = GetSubMenu(mhmenuContext, 0);

	if (hmenuPopup) {
		POINT pt2 = { pt.x, pt.y };

		::ClientToScreen(mhwnd, &pt2);

		++mMouseCursorLock;
		EnableMenuItem(hmenuPopup, ID_DISPLAYCONTEXTMENU_COPY, IsTextSelected() ? MF_ENABLED : MF_DISABLED|MF_GRAYED);
		EnableMenuItem(hmenuPopup, ID_DISPLAYCONTEXTMENU_PASTE, IsClipboardFormatAvailable(CF_TEXT) ? MF_ENABLED : MF_DISABLED|MF_GRAYED);

		TrackPopupMenu(hmenuPopup, TPM_LEFTALIGN | TPM_TOPALIGN, pt2.x, pt2.y, 0, mhwnd, NULL);
		--mMouseCursorLock;
	}
}

///////////////////////////////////////////////////////////////////////////

void ATUIRegisterDisplayPane() {
	ATRegisterUIPaneType(kATUIPaneId_Display, VDRefCountObjectFactory<ATDisplayPane, ATUIPane>);
}
