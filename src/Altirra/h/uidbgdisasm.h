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

#ifndef f_AT_UIDBGDISASM_H
#define f_AT_UIDBGDISASM_H

#include "cpu.h"
#include "console.h"
#include "debugger.h"
#include "debuggersettings.h"
#include "texteditor.h"
#include "uidbgpane.h"

class ATDisassemblyWindow final : public ATUIDebuggerPaneWindow,
							public IATDebuggerClient,
							public IATUIDebuggerDisassemblyPane,
							public IVDTextEditorCallback,
							public IVDTextEditorColorizer,
							public IVDUIMessageFilterW32
{
public:
	ATDisassemblyWindow();
	~ATDisassemblyWindow();

	void *AsInterface(uint32 iid) override;

	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state);
	void OnDebuggerEvent(ATDebugEvent eventId);

	void OnTextEditorUpdated() override;
	void OnTextEditorScrolled(int firstVisiblePara, int lastVisiblePara, int visibleParaCount, int totalParaCount) override;
	void OnLinkSelected(uint32 selectionCode, int para, int offset) override;

	void RecolorLine(int line, const char *text, int length, IVDTextEditorColorization *colorization);

	void SetPosition(uint32 addr) override;

private:
	bool OnPaneCommand(ATUIPaneCommandId id) override;

private:
	enum : uint32 {
		kID_TextView = 100,
		kID_Address,
		kID_ButtonPrev,
		kID_ButtonNext,
	};

	enum : uint8 {
		kSelCode_JumpTarget = 1,
		kSelCode_Expand = 2
	};

	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	bool OnMessage(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam, VDZLRESULT& result);

	bool OnCreate();
	void OnDestroy();
	void OnSize();
	void OnSetFocus();
	void OnFontsUpdated();
	bool OnCommand(UINT cmd, UINT code);
	void PushAndJump(uint32 fromXAddr, uint32 toXAddr);
	void GoPrev();
	void GoNext();
	void RemakeView(uint16 focusAddr);

	struct DisasmResult {
		uint32 mEndingPC;
	};

	struct LineInfo {
		uint32 mXAddress;
		uint32 mTargetXAddr;
		uint8 mNestingLevel;
		uint8 mP;
		bool mbEmulation;
		bool mbIsComment;
		bool mbIsSource;
		bool mbIsExpandable;
		uint8 mOperandSelCode;
		uint32 mOperandStart;
		uint32 mOperandEnd;
		uint32 mCommentPos;
	};

	DisasmResult Disassemble(VDStringA& buf,
		vdfastvector<LineInfo>& lines,
		uint32 nestingLevel,
		const ATCPUHistoryEntry& initialState,
		uint32 startAddr,
		uint32 focusAddr,
		uint32 maxBytes,
		uint32 maxLines,
		bool stopOnProcedureEnd);

	uint32 ComputeViewChecksum() const;

	vdrefptr<IVDTextEditor> mpTextEditor;
	HWND	mhwndTextEditor = nullptr;
	HWND	mhwndAddress = nullptr;
	HWND	mhwndButtonPrev = nullptr;
	HWND	mhwndButtonNext = nullptr;
	HMENU	mhmenu = nullptr;

	uint16	mViewStart = 0;
	uint32	mViewLength = 0;
	uint8	mViewBank = 0;
	uint32	mViewAddrBank = 0;
	uint32	mViewChecksum = 0;
	bool	mbViewCanScrollUp = false;
	uint16	mFocusAddr = 0;
	int		mPCLine = -1;
	uint32	mPCAddr = 0;
	int		mFramePCLine = -1;
	uint32	mFramePCAddr = 0;
	ATCPUSubMode	mLastSubMode = kATCPUSubMode_6502;

	ATDebuggerSettingView<bool> mbShowCodeBytes;
	ATDebuggerSettingView<bool> mbShowLabels;
	ATDebuggerSettingView<bool> mbShowLabelNamespaces;
	ATDebuggerSettingView<bool> mbShowProcedureBreaks;
	ATDebuggerSettingView<bool> mbShowCallPreviews;
	ATDebuggerSettingView<bool> mbShowSourceInDisasm;
	ATDebuggerSettingView<ATDebugger816PredictionMode> m816PredictionMode;
	
	ATDebuggerSystemState mLastState = {};

	vdfastvector<LineInfo> mLines;

	vdvector<VDStringW> mFailedSourcePaths;

	uint32	mHistory[32];
	uint8	mHistoryNext = 0;
	uint8	mHistoryLenForward = 0;
	uint8	mHistoryLenBack = 0;
};

#endif
