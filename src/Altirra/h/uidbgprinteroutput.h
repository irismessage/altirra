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

#ifndef f_AT_UIDBGCALLSTACK_H
#define f_AT_UIDBGCALLSTACK_H

#include <at/atcore/deviceprinter.h>

#include "console.h"
#include "debugger.h"
#include "uidbgpane.h"

class IVDTextEditor;
class ATPrinterGraphicalOutput;
class ATPrinterOutput;
class ATPrinterOutputManager;
class ATUIPrinterGraphicalOutputWindow;

class ATPrinterOutputWindow : public ATUIPaneWindow {
public:
	ATPrinterOutputWindow();
	~ATPrinterOutputWindow();

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam) override;

	bool OnCreate() override;
	void OnDestroy() override;
	void OnSize() override;
	void OnFontsUpdated() override;
	void OnSetFocus() override;

	void OnAddedOutput(ATPrinterOutput& output);
	void OnRemovingOutput(ATPrinterOutput& output);
	void OnAddedGraphicalOutput(ATPrinterGraphicalOutput& output);
	void OnRemovingGraphicalOutput(ATPrinterGraphicalOutput& output);

	void AttachToAnyOutput();

	void AttachToTextOutput(ATPrinterOutput& output);
	void DetachFromTextOutput();
	void UpdateTextOutput();

	void AttachToGraphicsOutput(ATPrinterGraphicalOutput& output);
	void DetachFromGraphicsOutput();

	vdrefptr<IVDTextEditor> mpTextEditor;
	HWND	mhwndTextEditor;

	uint32		mLineBufIdx;
	wchar_t		mLineBuf[133];

	size_t mLastTextOffset = 0;

	ATPrinterOutput *mpTextOutput = nullptr;
	ATPrinterGraphicalOutput *mpGraphicsOutput = nullptr;

	vdrefptr<ATUIPrinterGraphicalOutputWindow> mpGraphicWindow;
	vdrefptr<ATPrinterOutputManager> mpOutputMgr;

	vdfunction<void(ATPrinterOutput&)> mAddedOutputFn;
	vdfunction<void(ATPrinterOutput&)> mRemovingOutputFn;
	vdfunction<void(ATPrinterGraphicalOutput&)> mAddedGraphicalOutputFn;
	vdfunction<void(ATPrinterGraphicalOutput&)> mRemovingGraphicalOutputFn;
};

#endif
