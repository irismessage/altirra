//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2014 Avery Lee
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

#ifndef f_AT_UIVIDEODISPLAY_H
#define f_AT_UIVIDEODISPLAY_H

#include "uiwidget.h"
#include "uicontainer.h"
#include "callback.h"
#include "simeventmanager.h"
#include "devicemanager.h"

class IATUIEnhancedTextEngine;
class ATUILabel;
class ATUIOnScreenKeyboard;
class ATUISettingsWindow;
class ATXEP80Emulator;
class ATSimulatorEventManager;
class IATDeviceVideoOutput;

class ATUIVideoDisplayWindow : public ATUIContainer, public IATSimulatorCallback, public IATDeviceChangeCallback {
public:
	enum {
		kActionOpenOSK = kActionCustom,
		kActionCloseOSK,
		kActionOpenSidePanel
	};

	ATUIVideoDisplayWindow();
	~ATUIVideoDisplayWindow();

	bool Init(ATSimulatorEventManager& sem, ATDeviceManager& devmgr);
	void Shutdown();

	void Copy();

	void ToggleCaptureMouse();
	void ReleaseMouse();
	void CaptureMouse();

	void OpenOSK();
	void CloseOSK();

	void OpenSidePanel();
	void CloseSidePanel();

	bool IsTextSelected() const { return !mDragPreviewSpans.empty(); }

	vdrect32 GetOSKSafeArea() const;

	void SetDisplayRect(const vdrect32& r);

	void ClearXorRects();
	void AddXorRect(int x1, int y1, int x2, int y2);

	void SetXEP(IATDeviceVideoOutput *xep);
	void SetEnhancedTextEngine(IATUIEnhancedTextEngine *p) { mpEnhTextEngine = p; }

	void SetOnAllowContextMenu(const vdfunction<void()>& fn) { mpOnAllowContextMenu = fn; }
	void SetOnDisplayContextMenu(const vdfunction<void(const vdpoint32&)>& fn) { mpOnDisplayContextMenu = fn; }
	void SetOnOSKChange(const vdfunction<void()>& fn) { mpOnOSKChange = fn; }

public:
	virtual void OnSimulatorEvent(ATSimulatorEvent ev);

public:
	virtual void OnDeviceAdded(uint32 iid, IATDevice *dev, void *iface) override;
	virtual void OnDeviceRemoved(uint32 iid, IATDevice *dev, void *iface) override;

protected:
	virtual ATUITouchMode GetTouchModeAtPoint(const vdpoint32& pt) const;
	virtual void OnMouseDown(sint32 x, sint32 y, uint32 vk, bool dblclk);
	virtual void OnMouseUp(sint32 x, sint32 y, uint32 vk);
	virtual void OnMouseRelativeMove(sint32 dx, sint32 dy);
	virtual void OnMouseMove(sint32 x, sint32 y);
	virtual void OnMouseLeave();
	virtual void OnMouseHover(sint32 x, sint32 y);

	virtual bool OnContextMenu(const vdpoint32 *pt);

	virtual bool OnKeyDown(const ATUIKeyEvent& event);
	virtual bool OnKeyUp(const ATUIKeyEvent& event);
	virtual bool OnChar(const ATUICharEvent& event);

	virtual void OnActionStart(uint32 id);
	virtual void OnActionStop(uint32 id);

	virtual void OnCreate();
	virtual void OnDestroy();
	virtual void OnSize();

	virtual void OnSetFocus();
	virtual void OnKillFocus();
	virtual void OnCaptureLost();
	
	virtual void OnDeactivate() override;

	virtual void Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h);

protected:
	bool ProcessKeyDown(const ATUIKeyEvent& event, bool enableKeyInput);
	bool ProcessKeyUp(const ATUIKeyEvent& event, bool enableKeyInput);
	void ProcessVirtKey(int vkey, uint8 keycode, bool repeat);

	uint32 ComputeCursorImage(const vdpoint32& pt) const;
	void UpdateMousePosition(int x, int y);
	const vdrect32 GetXEP80Area() const;
	bool MapPixelToBeamPosition(int x, int y, float& hcyc, float& vcyc, bool clamp) const;
	bool MapPixelToBeamPosition(int x, int y, int& xc, int& yc, bool clamp) const;
	void MapBeamPositionToPixel(int xc, int yc, int& x, int& y) const;
	void UpdateDragPreview(int x, int y);
	void UpdateDragPreviewXEP80(int x, int y);
	void UpdateDragPreviewAntic(int x, int y);
	void UpdateDragPreviewRects();
	void ClearDragPreview();
	int GetModeLineYPos(int ys, bool checkValidCopyText) const;
	int ReadText(char *dst, int yc, int startChar, int numChars) const;
	void ClearCoordinateIndicator();
	void SetCoordinateIndicator(int x, int y);

	uint32 mLastVkeyPressed;
	uint32 mLastVkeySent;

	vdrect32 mDisplayRect;

	bool	mbDragActive;
	int		mDragAnchorX;
	int		mDragAnchorY;

	bool	mbMouseHidden;
	int		mMouseHideX;
	int		mMouseHideY;

	bool	mbOpenSidePanelDeferred;

	struct XorRect {
		int mX1;
		int mY1;
		int mX2;
		int mY2;
	};

	typedef vdfastvector<XorRect> XorRects;
	XorRects mXorRects;

	bool	mbCoordIndicatorActive;
	bool	mbCoordIndicatorEnabled;
	vdrect32	mHoverTipArea;
	bool		mbHoverTipActive;

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

	IATUIEnhancedTextEngine *mpEnhTextEngine;
	ATUIOnScreenKeyboard *mpOSK;
	ATUISettingsWindow *mpSidePanel;

	ATSimulatorEventManager *mpSEM;
	ATDeviceManager *mpDevMgr;
	IATDeviceVideoOutput *mpXEP;
	VDDisplayImageView mXEPImageView;
	uint32 mXEPChangeCount;
	uint32 mXEPLayoutChangeCount;
	uint32 mXEPDataReceivedCount;

	ATUILabel *mpUILabelBadSignal;

	vdfunction<void()> mpOnAllowContextMenu;
	vdfunction<void(const vdpoint32&)> mpOnDisplayContextMenu;
	vdfunction<void()> mpOnOSKChange;
};

#endif
