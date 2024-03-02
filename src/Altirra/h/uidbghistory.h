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

#ifndef f_AT_UIDBGHISTORY_H
#define f_AT_UIDBGHISTORY_H

#include "console.h"
#include "debugger.h"
#include "uidbgpane.h"
#include "uihistoryview.h"

class ATHistoryWindow final : public ATUIDebuggerPaneWindow, public IATUIDebuggerHistoryPane, private IATDebuggerClient, private IATUIHistoryModel {
public:
	ATHistoryWindow();
	~ATHistoryWindow();

	void *AsInterface(uint32 iid) override;

	bool JumpToCycle(uint32 cycle) override;

private:
	double DecodeTapeSample(uint32 cycle) override;
	double DecodeTapeSeconds(uint32 cycle) override;
	uint32 ConvertRawTimestamp(uint32 rawCycle) override;
	float ConvertRawTimestampDeltaF(sint32 rawCycleDelta) override;
	ATCPUBeamPosition DecodeBeamPosition(uint32 cycle) override;
	bool IsInterruptPositionVBI(uint32 cycle) override;
	bool UpdatePreviewNode(ATCPUHistoryEntry& he) override;
	uint32 ReadInsns(const ATCPUHistoryEntry **ppInsns, uint32 startIndex, uint32 n) override;
	void OnEsc() override;
	void OnInsnSelected(uint32 index) override {}
	void JumpToInsn(uint32 pc) override;
	void JumpToSource(uint32 pc) override;

private:
	typedef ATHTNode TreeNode;
	typedef ATHTLineIterator LineIterator;

	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam) override;
	bool OnCreate() override;
	void OnDestroy() override;
	void OnSize() override;
	void OnFontsUpdated() override;
	void OnPaint();

	void SetHistoryError(bool error);
	void RemakeHistoryView();
	void UpdateOpcodes();
	void CheckDisasmMode();

	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state);
	void OnDebuggerEvent(ATDebugEvent eventId);

	vdrefptr<IATUIHistoryView> mpHistoryView;
	bool mbHistoryError = false;

	ATDebugDisasmMode mDisasmMode {};
	uint32 mSubCycles = (uint32)-1;
	bool mbDecodeAnticNMI = false;
	
	uint32 mLastHistoryCounter = 0;

	vdfastvector<uint32> mFilteredInsnLookup;
};

#endif
