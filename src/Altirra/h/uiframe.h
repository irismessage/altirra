//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008 Avery Lee
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

#ifndef AT_UIFRAME_H
#define AT_UIFRAME_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/vectors.h>
#include <vd2/system/VDString.h>
#include "uinativewindow.h"

#ifndef ATWM_FONTSUPDATED
#define ATWM_FONTSUPDATED (WM_APP+200)
#endif

#ifndef ATWM_GETAUTOSIZE
#define ATWM_GETAUTOSIZE (WM_APP+201)
#endif

#ifndef ATWM_SETFULLSCREEN
#define ATWM_SETFULLSCREEN (WM_APP+202)
#endif

#define ATWM_PRESYSKEYDOWN (WM_APP + 0x120)
#define ATWM_PRESYSKEYUP (WM_APP + 0x121)
#define ATWM_PREKEYDOWN (WM_APP + 0x122)
#define ATWM_PREKEYUP (WM_APP + 0x123)

void ATInitUIFrameSystem();
void ATShutdownUIFrameSystem();

class ATContainerWindow;
class ATContainerDockingPane;
class ATFrameWindow;

enum {
	kATContainerDockCenter,
	kATContainerDockLeft,
	kATContainerDockRight,
	kATContainerDockTop,
	kATContainerDockBottom
};

class ATContainerResizer {
public:
	ATContainerResizer();

	void LayoutWindow(HWND hwnd, int x, int y, int width, int height, bool visible);
	void ResizeWindow(HWND hwnd, int width, int height);

	void Flush();

protected:
	HDWP mhdwp;
	vdfastvector<HWND> mWindowsToShow;
};

class ATContainerSplitterBar : public ATUINativeWindow {
public:
	ATContainerSplitterBar();
	~ATContainerSplitterBar();

	bool Init(HWND hwndParent, ATContainerDockingPane *pane, bool vertical);
	void Shutdown();

protected:
	static LRESULT StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);
	void OnPaint();
	void OnSize();
	void OnLButtonDown(WPARAM wParam, int x, int y);
	void OnLButtonUp(WPARAM wParam, int x, int y);
	void OnMouseMove(WPARAM wParam, int x, int y);
	void OnCaptureChanged(HWND hwndNewCapture);

	virtual bool IsTouchHitTestCapable() const;

	ATContainerDockingPane *mpControlledPane;
	bool	mbVertical;
	int		mDistanceOffset;

};

class ATDragHandleWindow : public ATUINativeWindow {
public:
	ATDragHandleWindow();
	~ATDragHandleWindow();

	VDGUIHandle Create(int x, int y, int cx, int cy, VDGUIHandle parent, int id);
	void Destroy();

	int HitTest(int screenX, int screenY);

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	void OnCreate();
	void OnMove();
	void OnPaint();

	int mX;
	int mY;
};

class ATContainerDockingPane : public vdrefcounted<IVDRefCount> {
public:
	ATContainerDockingPane(ATContainerWindow *parent);
	~ATContainerDockingPane();

	const vdrect32& GetArea() const { return mArea; }
	void	SetArea(ATContainerResizer& resizer, const vdrect32& area, bool parentContainsFullScreen);

	void	Clear();

	void	InvalidateLayoutAll();
	void	InvalidateLayout();
	void	UpdateLayout(ATContainerResizer& resizer);
	void	Relayout(ATContainerResizer& resizer);

	bool	GetFrameSizeForContent(vdsize32& sz);

	// Docking panes automatically disappear when they have no content unless they
	// are pinned.
	void	SetPinned(bool pinned) { mbPinned = pinned; }

	int		GetDockCode() const;
	float	GetDockFraction() const;
	void	SetDockFraction(float dist);

	ATContainerDockingPane *GetParentPane() const { return mpDockParent; }

	uint32	GetContentCount() const;
	ATFrameWindow *GetContent(uint32 idx) const;
	ATFrameWindow *GetAnyContent(bool requireVisible, ATFrameWindow *exclude) const;

	uint32	GetChildCount() const;
	ATContainerDockingPane *GetChildPane(uint32 index) const;

	void	AddContent(ATFrameWindow *frame, bool deferResize);
	ATContainerDockingPane *Dock(ATFrameWindow *pane, int code);
	bool	Undock(ATFrameWindow *pane);

	void	NotifyFontsUpdated();

	void	RecalcFrame();

	ATFrameWindow *GetVisibleFrame() const;
	void	SetVisibleFrame(ATFrameWindow *frame);

	void	UpdateModalState(ATFrameWindow *modalFrame);
	void	UpdateActivationState(ATFrameWindow *frame);

	void	CreateDragHandles();
	void	DestroyDragHandles();
	void	CreateSplitter();
	void	DestroySplitter();

	bool	HitTestDragHandles(int screenX, int screenY, int& code, ATContainerDockingPane **ppPane);

	void	UpdateFullScreenState();
	bool	IsFullScreen() const;

	void	RemoveAnyEmptyNodes();

	void	OnTabChange(HWND hwndSender);

protected:
	void	RecalcFrameInternal();
	void	RepositionContent(ATContainerResizer& resizer);
	void	RemoveEmptyNode();

	ATContainerWindow *const mpParent;
	vdrefptr<ATDragHandleWindow> mpDragHandle;
	vdrefptr<ATContainerSplitterBar> mpSplitter;

	typedef vdfastvector<ATFrameWindow *> FrameWindows;
	FrameWindows mContent;

	ATContainerDockingPane	*mpDockParent;

	typedef vdfastvector<ATContainerDockingPane *> Children;
	Children mChildren;

	vdrect32	mArea;
	vdrect32	mCenterArea;
	int			mDockCode;
	float		mDockFraction;
	bool		mbFullScreen;
	bool		mbFullScreenLayout;
	bool		mbPinned;
	bool		mbLayoutInvalid;
	bool		mbDescendantLayoutInvalid;
	sint32		mVisibleFrameIndex;

	HWND		mhwndTabControl;
};

class ATContainerWindow : public ATUINativeWindow {
public:
	enum { kTypeID = 'uicw' };

	ATContainerWindow();
	~ATContainerWindow();

	void *AsInterface(uint32 id);

	VDGUIHandle Create(int x, int y, int cx, int cy, VDGUIHandle parent, bool visible);
	VDGUIHandle Create(ATOM wndClass, int x, int y, int cx, int cy, VDGUIHandle parent, bool visible);
	void Destroy();

	void Clear();
	void AutoSize();
	void Relayout();

	static ATContainerWindow *GetContainerWindow(HWND hwnd);

	ATContainerDockingPane *GetBasePane() const { return mpDockingPane; }
	ATFrameWindow *GetActiveFrame() const { return mpActiveFrame; }
	ATFrameWindow *GetModalFrame() const { return mpModalFrame; }

	int GetCaptionHeight() const { return mCaptionHeight; }
	HFONT GetCaptionFont() const { return mhfontCaption; }
	HFONT GetCaptionSymbolFont() const { return mhfontCaptionSymbol; }
	HFONT GetLabelFont() const { return mhfontLabel; }

	uint32	GetUndockedPaneCount() const;
	ATFrameWindow *GetUndockedPane(uint32 index) const;

	bool	IsLayoutSuspended() const { return mLayoutSuspendCount > 0; }
	void	SuspendLayout();
	void	ResumeLayout();

	void	NotifyFontsUpdated();

	bool	InitDragHandles();
	void	ShutdownDragHandles();
	void	UpdateDragHandles(int screenX, int screenY);
	ATContainerDockingPane *DockFrame(ATFrameWindow *frame, ATContainerDockingPane *pane, int code);
	ATContainerDockingPane *DockFrame(ATFrameWindow *frame, int code);
	ATContainerDockingPane *DockFrame(ATFrameWindow *frame);
	void	AddUndockedFrame(ATFrameWindow *frame);
	void	UndockFrame(ATFrameWindow *frame, bool visible = true, bool destroy = false);
	void	CloseFrame(ATFrameWindow *frame);

	ATFrameWindow *GetFullScreenFrame() const { return mpFullScreenFrame; }
	void	SetFullScreenFrame(ATFrameWindow *frame);

	void	SetModalFrame(ATFrameWindow *frame);

	void	ActivateFrame(ATFrameWindow *frame);

	void	RemoveAnyEmptyNodes();

	void	NotifyFrameActivated(ATFrameWindow *frame);
	void	NotifyUndockedFrameDestroyed(ATFrameWindow *frame);

	void	NotifyDockedFrameDestroyed(ATFrameWindow *w);

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	virtual bool OnCreate();
	virtual void OnDestroy();
	void OnSize();

	void OnSetFocus(HWND hwndOldFocus);
	void OnKillFocus(HWND hwndNewFocus);
	bool OnActivate(UINT code, bool minimized, HWND hwnd);

	void RecreateSystemObjects();
	void DestroySystemObjects();

	void UpdateMonitorDpi();
	virtual void UpdateMonitorDpi(unsigned dpiY);

	ATContainerDockingPane *mpDockingPane;
	ATContainerDockingPane *mpDragPaneTarget;
	ATFrameWindow *mpActiveFrame;
	ATFrameWindow *mpFullScreenFrame;
	ATFrameWindow *mpModalFrame;
	int mDragPaneTargetCode;
	bool mbBlockActiveUpdates;
	int mCaptionHeight;
	HFONT mhfontCaption;
	HFONT mhfontCaptionSymbol;
	HFONT mhfontLabel;

	int mMonitorDpi;

	uint32 mLayoutSuspendCount;

	typedef vdfastvector<ATFrameWindow *> UndockedFrames;
	UndockedFrames mUndockedFrames;
};

class ATFrameWindow : public ATUINativeWindow {
public:
	enum { kTypeID = 'uifr' };

	enum FrameMode {
		kFrameModeUndocked,
		kFrameModeNone,
		kFrameModeEdge,
		kFrameModeFull
	};

	ATFrameWindow(ATContainerWindow *container);
	~ATFrameWindow();

	static ATFrameWindow *GetFrameWindow(HWND hwnd);

	void *AsInterface(uint32 iid);

	ATContainerWindow *GetContainer() const { return mpContainer; }

	void SetPane(ATContainerDockingPane *pane) { mpDockingPane = pane; }
	ATContainerDockingPane *GetPane() const { return mpDockingPane; }

	const wchar_t *GetTitle() const { return mTitle.c_str(); }

	bool IsFullScreen() const;
	void SetFullScreen(bool fs);

	bool IsVisible() const;
	void SetVisible(bool vis);
	void SetFrameMode(FrameMode fm) { mFrameMode = fm; }

	void NotifyFontsUpdated();

	bool GetIdealSize(vdsize32& sz);
	void RecalcFrame();
	void Relayout(int w, int h);

	VDGUIHandle Create(const wchar_t *title, int x, int y, int cx, int cy, VDGUIHandle parent);
	VDGUIHandle CreateChild(const wchar_t *title, int x, int y, int cx, int cy, VDGUIHandle parent);

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);
	void PaintCaption(HRGN clipRegion);

	bool OnCreate();
	void OnDestroy();
	void OnSize();
	bool OnNCLButtonDown(int code, int x, int y);
	bool OnMouseMove(int x, int y);

	void EndDrag(bool success);

	int		mDragOriginX;
	int		mDragOriginY;
	int		mDragOffsetX;
	int		mDragOffsetY;
	bool	mbDragging;
	bool	mbDragVerified;
	bool	mbFullScreen;
	bool	mbActiveCaption;
	bool	mbCloseDown;
	bool	mbCloseTracking;
	FrameMode	mFrameMode;
	vdrect32	mCaptionRect;
	vdrect32	mClientRect;
	vdrect32	mCloseRect;
	ATContainerDockingPane *mpDockingPane;
	ATContainerWindow *const mpContainer;
	vdrefptr<ATContainerWindow> mpDragContainer;

	VDStringW	mTitle;
};

class ATUIPane : public ATUINativeWindow {
public:
	enum { kTypeID = 'uipn' };

	ATUIPane(uint32 paneId, const wchar_t *name);
	~ATUIPane();

	void *AsInterface(uint32 iid);

	uint32 GetUIPaneId() const { return mPaneId; }
	const wchar_t *GetUIPaneName() const { return mpName; }
	int GetPreferredDockCode() const { return mPreferredDockCode; }

	bool Create(ATFrameWindow *w);

protected:
	void SetName(const wchar_t *name);

	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	virtual bool OnCreate();
	virtual void OnDestroy();
	virtual void OnSize();
	virtual void OnSetFocus();
	virtual void OnFontsUpdated();
	virtual bool OnCommand(uint32 id, uint32 extcode);

	void RegisterUIPane();
	void UnregisterUIPane();

	const wchar_t *		mpName;
	uint32 const		mPaneId;
	uint32				mDefaultWindowStyles;
	int					mPreferredDockCode;
};

typedef bool (*ATPaneCreator)(ATUIPane **);
typedef bool (*ATPaneClassCreator)(uint32 id, ATUIPane **);

void ATRegisterUIPaneType(uint32 id, ATPaneCreator creator);
void ATRegisterUIPaneClass(uint32 id, ATPaneClassCreator creator);

#endif
