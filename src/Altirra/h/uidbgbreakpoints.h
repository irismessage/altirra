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

#ifndef f_AT_UIDBGBREAKPOINTS_H
#define f_AT_UIDBGBREAKPOINTS_H

#include <at/atnativeui/uiproxies.h>

#include "debugger.h"
#include "uidbgpane.h"

class ATUIDebuggerBreakpointDialog final : public VDDialogFrameW32 {
public:
	ATUIDebuggerBreakpointDialog(sint32 userIdx);

	bool OnLoaded() override;
	void OnDataExchange(bool write) override;

private:
	void UpdateEnables();
	void UpdateEnableForLocation();
	void UpdateEnableForCondition();
	void UpdateEnableForActionCommand();
	void UpdateEnableForActionTrace();

	sint32 mUserIdx {};

	VDUIProxyButtonControl mLocationPCView;
	VDUIProxyButtonControl mLocationReadView;
	VDUIProxyButtonControl mLocationWriteView;
	VDUIProxyButtonControl mLocationInsnView;
	VDUIProxyEditControl mLocationView;
	VDUIProxyButtonControl mConditionEnableView;
	VDUIProxyEditControl mConditionView;
	VDUIProxyButtonControl mActionStopView;
	VDUIProxyButtonControl mActionCommandEnableView;
	VDUIProxyEditControl mActionCommandView;
	VDUIProxyButtonControl mActionTraceEnableView;
	VDUIProxyEditControl mActionTraceView;
};

class ATUIDebuggerPaneBreakpoints final
	: public ATUIDebuggerPaneDialog
	, public IATDebuggerClient
{
public:
	ATUIDebuggerPaneBreakpoints();
	~ATUIDebuggerPaneBreakpoints();

	bool OnLoaded() override;
	void OnDestroy() override;
	bool OnCommand(uint32 id, uint32 extcode) override;
	void OnDeferredUpdate() override;

	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) override;
	void OnDebuggerEvent(ATDebugEvent eventId) override;

private:
	class BpListItem final : public vdrefcounted<IVDUIListViewVirtualItem> {
	public:
		BpListItem(const char *group, const ATDebuggerBreakpointInfo& bpInfo);

		void GetText(int subItem, VDStringW& s) const override;

		uint32 mUserIdx = 0;
		VDStringW mIDStr;
		VDStringW mTargetStr;
		VDStringW mLocationStr;
	};

	void InvalidateBreakpoints();
	void UpdateBreakpoints();

	void OnItemContextMenu(VDUIProxyListView::ContextMenuEvent& event);
	void OnItemDoubleClicked();

	bool mbBreakpointUpdatePending = false;

	VDUIProxyListView mBpView;
};

#endif
