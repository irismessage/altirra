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
#include <vd2/system/vectors.h>
#include "ui.h"

void ATInitUIFrameSystem();
void ATShutdownUIFrameSystem();

class ATContainerWindow;
class ATContainerDockingPane;
class ATFrameWindow;

enum {
	kATContainerDockLeft,
	kATContainerDockRight,
	kATContainerDockTop,
	kATContainerDockBottom,
	kATContainerDockCenter
};

class ATContainerSplitterBar : public VDShaderEditorBaseWindow {
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

	ATContainerDockingPane *mpControlledPane;
	bool	mbVertical;
	int		mDistanceOffset;

};

class ATDragHandleWindow : public VDShaderEditorBaseWindow {
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
	void	SetArea(const vdrect32& area, bool parentContainsFullScreen);

	void	Clear();
	void	Relayout();

	// Docking panes automatically disappear when they have no content unless they
	// are pinned.
	void	SetPinned(bool pinned) { mbPinned = pinned; }

	int		GetDockCode() const;
	float	GetDockFraction() const;
	void	SetDockFraction(float dist);

	ATContainerDockingPane *GetParentPane() const { return mpDockParent; }
	ATFrameWindow *GetContent() const { return mpContent; }

	ATContainerDockingPane *GetCenterPane() const;

	uint32	GetChildCount() const;
	ATContainerDockingPane *GetChildPane(uint32 index) const;

	void	SetContent(ATFrameWindow *frame);
	void	Dock(ATContainerDockingPane *pane, int code);
	bool	Undock(ATFrameWindow *pane);

	void	UpdateActivationState(ATFrameWindow *frame);

	void	CreateDragHandles();
	void	DestroyDragHandles();
	void	CreateSplitter();
	void	DestroySplitter();

	bool	HitTestDragHandles(int screenX, int screenY, int& code, ATContainerDockingPane **ppPane);

	void	UpdateFullScreenState();
	bool	IsFullScreen() const;

	void	RemoveAnyEmptyNodes();

protected:
	void	RepositionContent();
	void	RemoveEmptyNode();

	ATContainerWindow *const mpParent;
	vdrefptr<ATDragHandleWindow> mpDragHandle;
	vdrefptr<ATContainerSplitterBar> mpSplitter;
	vdrefptr<ATFrameWindow> mpContent;

	ATContainerDockingPane	*mpDockParent;

	typedef vdfastvector<ATContainerDockingPane *> Children;
	Children mChildren;

	vdrect32	mArea;
	vdrect32	mCenterArea;
	int			mDockCode;
	float		mDockFraction;
	int			mCenterCount;
	bool		mbFullScreen;
	bool		mbFullScreenLayout;
	bool		mbPinned;
};

class ATContainerWindow : public VDShaderEditorBaseWindow {
public:
	enum { kTypeID = 'uicw' };

	ATContainerWindow();
	~ATContainerWindow();

	void *AsInterface(uint32 id);

	VDGUIHandle Create(int x, int y, int cx, int cy, VDGUIHandle parent);
	void Destroy();

	void Clear();
	void Relayout();

	static ATContainerWindow *GetContainerWindow(HWND hwnd);

	ATContainerDockingPane *GetBasePane() const { return mpDockingPane; }
	ATFrameWindow *GetActiveFrame() const { return mpActiveFrame; }

	uint32	GetUndockedPaneCount() const;
	ATFrameWindow *GetUndockedPane(uint32 index) const;

	bool	InitDragHandles();
	void	ShutdownDragHandles();
	void	UpdateDragHandles(int screenX, int screenY);
	ATContainerDockingPane *DockFrame(ATFrameWindow *frame, ATContainerDockingPane *pane, int code);
	ATContainerDockingPane *DockFrame(ATFrameWindow *frame, int code);
	ATContainerDockingPane *DockFrame(ATFrameWindow *frame);
	void	AddUndockedFrame(ATFrameWindow *frame);
	void	UndockFrame(ATFrameWindow *frame, bool visible = true);
	void	SetFullScreenFrame(ATFrameWindow *frame);
	void	ActivateFrame(ATFrameWindow *frame);

	void	RemoveAnyEmptyNodes();

	void	NotifyFrameActivated(ATFrameWindow *frame);
	void	NotifyUndockedFrameDestroyed(ATFrameWindow *frame);

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	virtual bool OnCreate();
	virtual void OnDestroy();
	void OnSize();
	void OnChildDestroy(HWND hwndChild);

	void OnSetFocus(HWND hwndOldFocus);
	void OnKillFocus(HWND hwndNewFocus);
	bool OnActivate(UINT code, bool minimized, HWND hwnd);

	ATContainerDockingPane *mpDockingPane;
	ATContainerDockingPane *mpDragPaneTarget;
	ATFrameWindow *mpActiveFrame;
	ATFrameWindow *mpFullScreenFrame;
	int mDragPaneTargetCode;
	bool mbBlockActiveUpdates;

	typedef vdfastvector<ATFrameWindow *> UndockedFrames;
	UndockedFrames mUndockedFrames;
};

class ATFrameWindow : public VDShaderEditorBaseWindow {
public:
	enum { kTypeID = 'uifr' };

	ATFrameWindow();
	~ATFrameWindow();

	static ATFrameWindow *GetFrameWindow(HWND hwnd);

	void *AsInterface(uint32 iid);

	void SetContainer(ATContainerWindow *container) { mpContainer = container; }
	ATContainerWindow *GetContainer() const { return mpContainer; }

	void SetPane(ATContainerDockingPane *pane) { mpDockingPane = pane; }
	ATContainerDockingPane *GetPane() const { return mpDockingPane; }

	bool IsFullScreen() const;
	void SetFullScreen(bool fs);

	VDGUIHandle Create(const char *title, int x, int y, int cx, int cy, VDGUIHandle parent);
	VDGUIHandle CreateChild(const char *title, int x, int y, int cx, int cy, VDGUIHandle parent);

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	bool OnCreate();
	void OnDestroy();
	void OnSize();
	bool OnNCLButtonDown(int code, int x, int y);
	bool OnMouseMove(int x, int y);

	void EndDrag(bool success);

	int		mDragOffsetX;
	int		mDragOffsetY;
	bool	mbDragging;
	bool	mbFullScreen;
	ATContainerDockingPane *mpDockingPane;
	ATContainerWindow *mpContainer;
	vdrefptr<ATContainerWindow> mpDragContainer;
};

class ATUIPane : public VDShaderEditorBaseWindow {
public:
	ATUIPane(uint32 paneId, const char *name);
	~ATUIPane();

	uint32 GetUIPaneId() const { return mPaneId; }
	const char *GetUIPaneName() const { return mpName; }
	int GetPreferredDockCode() const { return mPreferredDockCode; }

	bool Create(ATFrameWindow *w);

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	virtual bool OnCreate();
	virtual void OnDestroy();
	virtual void OnSize();
	virtual void OnSetFocus();

	void RegisterUIPane();
	void UnregisterUIPane();

	const char *const	mpName;
	uint32 const		mPaneId;
	uint32				mDefaultWindowStyles;
	int					mPreferredDockCode;
};

typedef bool (*ATPaneCreator)(ATUIPane **);

void ATRegisterUIPaneType(uint32 id, ATPaneCreator creator);

#endif
