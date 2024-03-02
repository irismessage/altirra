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

class ATUIDisplayTool : public vdrefcount {
public:
	void InitTool(ATUIManager& mgr, ATUIVideoDisplayWindow& parent);
	void ShutdownTool();

	virtual bool IsMainTool() const;
	virtual bool HasPriorityOverInputManager() const;

	virtual void OnInit();
	virtual void OnShutdown();
	virtual bool OnMouseDownL(sint32 x, sint32 y);
	virtual bool OnMouseMove(sint32 x, sint32 y);
	virtual bool OnMouseUpL(sint32 x, sint32 y);

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

#endif
