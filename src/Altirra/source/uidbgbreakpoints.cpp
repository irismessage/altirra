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

#include <stdafx.h>
#include <at/atdebugger/expression.h>
#include "debuggerexp.h"
#include "resource.h"
#include "uiaccessors.h"
#include "uidbgbreakpoints.h"

///////////////////////////////////////////////////////////////////////////////

ATUIDebuggerBreakpointDialog::ATUIDebuggerBreakpointDialog(sint32 userIdx)
	: VDDialogFrameW32(IDD_DEBUG_BREAKPOINT)
	, mUserIdx(userIdx)
{
	mLocationPCView.SetOnClicked([this] { UpdateEnableForLocation(); });
	mLocationReadView.SetOnClicked([this] { UpdateEnableForLocation(); });
	mLocationWriteView.SetOnClicked([this] { UpdateEnableForLocation(); });
	mLocationInsnView.SetOnClicked([this] { UpdateEnableForLocation(); });
	mConditionEnableView.SetOnClicked([this] { UpdateEnableForCondition(); });
	mActionCommandEnableView.SetOnClicked([this] { UpdateEnableForActionCommand(); });
	mActionTraceEnableView.SetOnClicked([this] { UpdateEnableForActionTrace(); });
}

bool ATUIDebuggerBreakpointDialog::OnLoaded() {
	AddProxy(&mLocationPCView, IDC_LOCATION_PC);
	AddProxy(&mLocationReadView, IDC_LOCATION_READ);
	AddProxy(&mLocationWriteView, IDC_LOCATION_WRITE);
	AddProxy(&mLocationInsnView, IDC_LOCATION_CONDITION);
	AddProxy(&mLocationView, IDC_LOCATION);
	AddProxy(&mConditionEnableView, IDC_ENABLE_CONDITION);
	AddProxy(&mConditionView, IDC_CONDITION);
	AddProxy(&mActionStopView, IDC_ACTION_STOP);
	AddProxy(&mActionCommandEnableView, IDC_ACTION_COMMAND);
	AddProxy(&mActionCommandView, IDC_COMMAND);
	AddProxy(&mActionTraceEnableView, IDC_ACTION_TRACE);
	AddProxy(&mActionTraceView, IDC_TRACE);

	OnDataExchange(false);
	mLocationView.Focus();
	return true;
}

void ATUIDebuggerBreakpointDialog::OnDataExchange(bool write) {
	if (!write) {
		ATDebuggerBreakpointInfo bpInfo {};

		if (mUserIdx >= 0)
			ATGetDebugger()->GetBreakpointInfo((uint32)mUserIdx, bpInfo);

		if (bpInfo.mbBreakOnInsn)
			mLocationInsnView.SetChecked(true);
		else {
			if (bpInfo.mbBreakOnRead)
				mLocationReadView.SetChecked(true);
			else if (bpInfo.mbBreakOnWrite)
				mLocationWriteView.SetChecked(true);
			else
				mLocationPCView.SetChecked(true);

			if (mUserIdx >= 0) {
				VDStringW s;
				s.sprintf(L"$%04X", bpInfo.mAddress);
				mLocationView.SetText(s.c_str());
			}
		}

		if (bpInfo.mpCondition) {
			VDStringA condStr;

			bpInfo.mpCondition->ToString(condStr);

			mConditionView.SetText(VDTextAToW(condStr).c_str());
			mConditionEnableView.SetChecked(true);
		} else {
			mConditionEnableView.SetChecked(false);
		}

		mActionStopView.SetChecked(!bpInfo.mbContinueExecution);

		if (bpInfo.mpCommand && *bpInfo.mpCommand) {
			mActionCommandView.SetText(VDTextAToW(bpInfo.mpCommand).c_str());
			mActionCommandEnableView.SetChecked(true);
		} else {
			mActionCommandEnableView.SetChecked(false);
		}

		UpdateEnables();
	} else {
		auto& dbg = *ATGetDebugger();

		vdautoptr<ATDebugExpNode> condition;
		if (mConditionEnableView.GetChecked()) {
			VDStringW conditionStr = mConditionView.GetText();

			try {
				condition = ATDebuggerParseExpression(VDTextWToA(conditionStr.c_str()).c_str(), ATGetDebuggerSymbolLookup(), dbg.GetExprOpts());
			} catch(const ATDebuggerExprParseException& e) {
				FailValidation(mConditionView.GetWindowId(), (VDStringW(L"Unable to parse condition: ") + e.wc_str()).c_str());
				return;
			}
		}

		ATDebuggerBreakpointInfo bpInfo;
		bpInfo.mTargetIndex = dbg.GetTargetIndex();
		bpInfo.mbContinueExecution = !mActionTraceEnableView.GetChecked();

		VDStringA commandStr;
		if (mActionCommandEnableView.GetChecked())
			commandStr = VDTextWToA(mActionCommandView.GetText());

		if (mActionTraceEnableView.GetChecked()) {
			VDStringW traceStr = mActionTraceView.GetText();

			if (!traceStr.empty()) {
				VDStringA traceCmdStr(".printf \"");

				for(const wchar_t ch : traceStr) {
					if (ch < 0x20 || ch > 0x7E) {
						FailValidation(mActionTraceView.GetWindowId(), L"The trace message contains an unsupported character.");
						return;
					}

					if (ch == L'"')
						traceCmdStr += '\\';
					else if (ch == L'%')
						traceCmdStr += '%';

					traceCmdStr += (char)ch;
				}

				traceCmdStr += '"';

				if (!commandStr.empty()) {
					traceCmdStr += "; ";

					traceCmdStr += commandStr;
				}

				commandStr = std::move(traceCmdStr);
			}
		}

		bpInfo.mpCondition = condition;
		bpInfo.mpCommand = commandStr.c_str();
		bpInfo.mbContinueExecution = !mActionStopView.GetChecked();

		if (bpInfo.mbContinueExecution) {
			if (commandStr.empty()) {
				FailValidation(mActionStopView.GetWindowId(), L"A non-stopping breakpoint must be used with a command or a trace message.");
				return;
			}
		}

		if (mLocationInsnView.GetChecked()) {
			if (!condition) {
				FailValidation(mLocationInsnView.GetWindowId(), L"A condition must be used with a condition-only breakpoint.");
				return;
			}

			bpInfo.mbBreakOnInsn = true;
		} else {
			VDStringW locationStr = mLocationView.GetText();
			if (locationStr.empty()) {
				FailValidation(mLocationView.GetWindowId());
				return;
			}

			uint32 address = 0;

			try {
				address = dbg.EvaluateThrow(VDTextWToA(locationStr).c_str());
			} catch(const MyError& e) {
				FailValidation(mLocationView.GetWindowId(), (VDStringW(L"Unable to parse location: ") + e.wc_str()).c_str());
				return;
			}

			bpInfo.mAddress = address;

			if (mLocationReadView.GetChecked())
				bpInfo.mbBreakOnRead = true;
			else if (mLocationWriteView.GetChecked())
				bpInfo.mbBreakOnWrite = true;
			else
				bpInfo.mbBreakOnPC = true;
		}

		dbg.SetBreakpoint(mUserIdx, bpInfo);
	}
}

void ATUIDebuggerBreakpointDialog::UpdateEnables() {
	UpdateEnableForLocation();
	UpdateEnableForCondition();
	UpdateEnableForActionCommand();
	UpdateEnableForActionTrace();
}

void ATUIDebuggerBreakpointDialog::UpdateEnableForLocation() {
	mLocationView.SetEnabled(!mLocationInsnView.GetChecked());
}

void ATUIDebuggerBreakpointDialog::UpdateEnableForCondition() {
	mConditionView.SetEnabled(mConditionEnableView.GetChecked());
}

void ATUIDebuggerBreakpointDialog::UpdateEnableForActionCommand() {
	mActionCommandView.SetEnabled(mActionCommandEnableView.GetChecked());
}

void ATUIDebuggerBreakpointDialog::UpdateEnableForActionTrace() {
	mActionTraceView.SetEnabled(mActionTraceEnableView.GetChecked());
}

///////////////////////////////////////////////////////////////////////////////

ATUIDebuggerPaneBreakpoints::BpListItem::BpListItem(const char *group, const ATDebuggerBreakpointInfo& bpInfo)
	: mUserIdx(bpInfo.mNumber)
{
	if (*group)
		mIDStr.sprintf(L"%hs.%u", group, bpInfo.mNumber);
	else
		mIDStr.sprintf(L"%u", bpInfo.mNumber);

	mTargetStr.sprintf(L"%u", bpInfo.mTargetIndex);

	if (bpInfo.mbBreakOnInsn) {
		mLocationStr = L"Any insn";
	} else {
		const VDStringW addressStr = VDTextAToW(ATGetDebugger()->GetAddressText(bpInfo.mAddress, true, true));

		if (bpInfo.mbBreakOnRead) {
			if (bpInfo.mbBreakOnWrite)
				mLocationStr = VDStringW(L"Access ") + addressStr;
			else
				mLocationStr = VDStringW(L"Read ") + addressStr;
		} else if (bpInfo.mbBreakOnWrite)
			mLocationStr = VDStringW(L"Write ") + addressStr;
		else
			mLocationStr = addressStr;
	}

	ATDebugExpNode *condition = ATGetDebugger()->GetBreakpointCondition(bpInfo.mNumber);
	if (condition) {
		VDStringA conditionStr;

		condition->ToString(conditionStr);

		mLocationStr.append_sprintf(L" when %hs", conditionStr.c_str());
	}

	if (bpInfo.mpCommand && *bpInfo.mpCommand) {
		mLocationStr += L", run command: ";

		mLocationStr += VDTextAToW(bpInfo.mpCommand);
	}
}

void ATUIDebuggerPaneBreakpoints::BpListItem::GetText(int subItem, VDStringW& s) const {
	switch(subItem) {
		case 0:
			s = mIDStr;
			break;

		case 1:
			s = mTargetStr;
			break;

		case 2:
			s = mLocationStr;
			break;
	}
}

///////////////////////////////////////////////////////////////////////////////

ATUIDebuggerPaneBreakpoints::ATUIDebuggerPaneBreakpoints()
	: ATUIDebuggerPaneDialog(kATUIPaneId_Breakpoints, L"Breakpoints", IDD_DBG_BREAKPOINTS)
{
	mPreferredDockCode = kATContainerDockRight;

	mBpView.SetOnItemContextMenu(
		[this](auto& event) { OnItemContextMenu(event); }
	);

	mBpView.SetOnItemDoubleClicked(
		[this](int index) { OnItemDoubleClicked(); }
	);
}

ATUIDebuggerPaneBreakpoints::~ATUIDebuggerPaneBreakpoints() {
}

bool ATUIDebuggerPaneBreakpoints::OnLoaded() {
	mResizer.Add(IDC_BPS, mResizer.kMC | mResizer.kAvoidFlicker);

	AddProxy(&mBpView, IDC_BPS);

	mBpView.SetFullRowSelectEnabled(true);
	mBpView.InsertColumn(0, L"ID", 50);
	mBpView.InsertColumn(1, L"Target", 50);
	mBpView.InsertColumn(2, L"Description", -1);

	ATGetDebugger()->AddClient(this, true);
	UpdateBreakpoints();
	return Base::OnLoaded();
}

bool ATUIDebuggerPaneBreakpoints::OnCommand(uint32 id, uint32 extcode) {
	if (id == ID_BREAKPOINT_NEW) {
		ATUIDebuggerBreakpointDialog dlg(-1);
		dlg.ShowDialog(this);
	} else if (id == ID_BREAKPOINT_DELETE) {
		BpListItem *item = mBpView.GetSelectedVirtualItemAs<BpListItem>();
		if (item)
			ATGetDebugger()->ClearUserBreakpoint(item->mUserIdx, true);
		return true;
	}

	return false;
}

void ATUIDebuggerPaneBreakpoints::OnDeferredUpdate() {
	if (mbBreakpointUpdatePending)
		UpdateBreakpoints();
}

void ATUIDebuggerPaneBreakpoints::OnDestroy() {
	ATGetDebugger()->RemoveClient(this);
	Base::OnDestroy();
}

void ATUIDebuggerPaneBreakpoints::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
}

void ATUIDebuggerPaneBreakpoints::OnDebuggerEvent(ATDebugEvent eventId) {
	if (eventId == kATDebugEvent_BreakpointsChanged) {
		InvalidateBreakpoints();
	}
}

void ATUIDebuggerPaneBreakpoints::InvalidateBreakpoints() {
	if (!mbBreakpointUpdatePending) {
		mbBreakpointUpdatePending = true;
		DeferUpdate();
	}
}

void ATUIDebuggerPaneBreakpoints::UpdateBreakpoints() {
	mbBreakpointUpdatePending = false;

	mBpView.SetRedraw(false);
	mBpView.Clear();

	auto& dbg = *ATGetDebugger();

	auto bpGroups = dbg.GetBreakpointGroups();

	std::sort(bpGroups.begin(), bpGroups.end(), vdstringpredi());

	for(const VDStringA& bpGroup : bpGroups) {
		vdfastvector<uint32> bpIndices;
		dbg.GetBreakpointList(bpIndices, bpGroup.c_str());

		ATDebuggerBreakpointInfo bpInfo;
		for(uint32 bpIndex : bpIndices) {
			dbg.GetBreakpointInfo(bpIndex, bpInfo);

			mBpView.InsertVirtualItem(-1, new BpListItem(bpGroup.c_str(), bpInfo));
		}
	}

	mBpView.SetRedraw(true);
}

void ATUIDebuggerPaneBreakpoints::OnItemContextMenu(VDUIProxyListView::ContextMenuEvent& event) {
	const bool haveItem = event.mIndex >= 0;

	this->ActivateCommandPopupMenu(event.mX, event.mY, IDR_BREAKPOINT_CONTEXT_MENU,
		[&](uint32 id, VDMenuItemInitializer& initer) {
			if (id != ID_BREAKPOINT_NEW)
				initer.SetEnabled(haveItem);
		}
	);

	event.mbHandled = true;
}

void ATUIDebuggerPaneBreakpoints::OnItemDoubleClicked() {
	BpListItem *bplItem = mBpView.GetSelectedVirtualItemAs<BpListItem>();

	if (bplItem) {
		ATUIDebuggerBreakpointDialog dlg(bplItem->mUserIdx);
		dlg.ShowDialog(this);
	}
}

void ATUIShowDialogNewBreakpoint() {
	ATUIDebuggerBreakpointDialog dlg(-1);

	dlg.ShowDialog(ATUIGetNewPopupOwner());
}

void ATUIDebuggerRegisterBreakpointsPane() {
	ATRegisterUIPaneType(kATUIPaneId_Breakpoints, VDRefCountObjectFactory<ATUIDebuggerPaneBreakpoints, ATUIPane>);
}
