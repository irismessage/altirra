//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2022 Avery Lee
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
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#ifndef f_AT_UIDISPLAYTOOL_H
#define f_AT_UIDISPLAYTOOL_H

#include <vd2/system/function.h>
#include <vd2/system/refcount.h>

class ATUIManager;
class ATUIVideoDisplayWindow;
enum ATUICursorImage : uint32;

class ATUIDisplayTool : public vdrefcount {
public:
	void InitTool(ATUIManager& mgr, ATUIVideoDisplayWindow& parent);
	void ShutdownTool();

	// Returns true if this is a main tool. There is only one main tool enabled
	// at a time, and activating a new main tool replaces any existing one.
	// Otherwise, the tool is a background tool, which can coexist with other
	// tools. Background tools are disabled when a main tool is enabled.
	virtual bool IsMainTool() const;

	// Returns true if this tool should be checked before the input manager.
	// Otherwise, the input manager has priority if an input map assigns the
	// same inputs. By default a main tool has priority, background tools not.
	//
	// A tool always has priority when it is processing a drag, regardless of
	// this value.
	virtual bool HasPriorityOverInputManager() const;

	// Cursor image to use over a particular point. The default for main tools
	// is crosshair and the default for background tools is none.
	virtual ATUICursorImage GetCursorImage(sint32 x, sint32 y) const;

	virtual void OnInit();
	virtual void OnShutdown();
	bool OnMouseDown(sint32 x, sint32 y, uint32 vk);
	virtual bool OnMouseDownL(sint32 x, sint32 y);
	virtual bool OnMouseDownR(sint32 x, sint32 y);
	virtual bool OnMouseDownM(sint32 x, sint32 y);
	virtual bool OnMouseMove(sint32 x, sint32 y);
	bool OnMouseUp(sint32 x, sint32 y, uint32 vk);
	virtual bool OnMouseUpL(sint32 x, sint32 y);
	virtual bool OnMouseUpR(sint32 x, sint32 y);
	virtual bool OnMouseUpM(sint32 x, sint32 y);
	virtual bool OnMouseWheel(sint32 x, sint32 y, float delta, bool pages);
	virtual void OnCancelMode();

protected:
	void SetPrompt(const wchar_t *msg);
	void ClearPrompt();
	void Destroy();

	ATUIManager *mpManager = nullptr;
	ATUIVideoDisplayWindow *mpParent = nullptr;

private:
	bool mbPromptSet = false;
};

class ATUIDisplayToolRecalibrateLightPen final : public ATUIDisplayTool {
public:
	bool IsMainTool() const override;

	void OnInit() override;
	void OnShutdown() override;

private:
	void OnTriggerCorrection(bool state, int x, int y);

	int mState = 0;
	int mReferenceX = 0;
	int mReferenceY = 0;

	vdfunction<void(bool, int, int)> mOnTriggerCorrection;
};

class ATUIDisplayToolPanAndZoom final : public ATUIDisplayTool {
public:
	ATUIDisplayToolPanAndZoom(bool main);

	bool IsMainTool() const override;
	ATUICursorImage GetCursorImage(sint32 x, sint32 y) const override;

	void OnInit() override;

	bool OnMouseDownL(sint32 x, sint32 y) override;
	bool OnMouseDownM(sint32 x, sint32 y) override;
	bool OnMouseMove(sint32 x, sint32 y) override;
	bool OnMouseUpL(sint32 x, sint32 y) override;
	bool OnMouseUpM(sint32 x, sint32 y) override;
	bool OnMouseWheel(sint32 x, sint32 y, float delta, bool pages) override;
	void OnCancelMode() override;

private:
	bool mbMainTool = false;
	bool mbDragPanActive = false;
	bool mbDragZoomActive = false;
	sint32 mDragAnchorX = 0;
	sint32 mDragAnchorY = 0;
	sint32 mDragLastX = 0;
	sint32 mDragLastY = 0;
};

#endif
