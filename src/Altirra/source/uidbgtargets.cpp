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
#include <at/atdebugger/target.h>
#include "debuggerexp.h"
#include "resource.h"
#include "uiaccessors.h"
#include "uidbgtargets.h"

///////////////////////////////////////////////////////////////////////////////

ATUIDebuggerPaneTargets::TgtListItem::TgtListItem(uint32 targetId, bool sel, IATDebugTarget& target)
	: mTargetId(targetId)
	, mbSelected(sel)
{
	mDescription = VDTextAToW(target.GetName());
	mType = ATDebugGetDisasmModeDisplayName(target.GetDisasmMode());

	mType.append_sprintf(L" @ %.2fMHz", target.GetDisplayCPUClock() / 1000000.0);
}

void ATUIDebuggerPaneTargets::TgtListItem::GetText(int subItem, VDStringW& s) const {
	switch(subItem) {
		case 0:
			if (mbSelected)
				s = L"\u25BA";
			else
				s = L"";
			break;

		case 1:
			s.sprintf(L"%u", mTargetId);
			break;

		case 2:
			s = mType;
			break;

		case 3:
			s = mDescription;
			break;
	}
}

///////////////////////////////////////////////////////////////////////////////

ATUIDebuggerPaneTargets::ATUIDebuggerPaneTargets()
	: ATUIDebuggerPaneDialog(kATUIPaneId_Targets, L"Targets", IDD_DBG_TARGETS)
{
	mPreferredDockCode = kATContainerDockRight;

	mTgtView.SetOnItemDoubleClicked(
		[this](int index) { OnItemDoubleClicked(); }
	);
}

ATUIDebuggerPaneTargets::~ATUIDebuggerPaneTargets() {
}

bool ATUIDebuggerPaneTargets::OnLoaded() {
	mResizer.Add(IDC_TARGETS, mResizer.kMC | mResizer.kAvoidFlicker);

	AddProxy(&mTgtView, IDC_TARGETS);

	mTgtView.SetFullRowSelectEnabled(true);
	mTgtView.SetActivateOnEnterEnabled(true);
	mTgtView.InsertColumn(0, L"", 30);
	mTgtView.InsertColumn(1, L"ID", 50);
	mTgtView.InsertColumn(2, L"Type", 100);
	mTgtView.InsertColumn(3, L"Description", -1);

	ATGetDebugger()->AddClient(this, true);
	UpdateTargets();
	return Base::OnLoaded();
}

void ATUIDebuggerPaneTargets::OnDeferredUpdate() {
	if (mbTargetUpdatePending)
		UpdateTargets();
}

void ATUIDebuggerPaneTargets::OnDestroy() {
	ATGetDebugger()->RemoveClient(this);
	Base::OnDestroy();
}

void ATUIDebuggerPaneTargets::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
}

void ATUIDebuggerPaneTargets::OnDebuggerEvent(ATDebugEvent eventId) {
	if (eventId == kATDebugEvent_TargetsChanged) {
		InvalidateTargets();
	} else if (eventId == kATDebugEvent_CurrentTargetChanged) {
		const uint32 targetId = ATGetDebugger()->GetTargetIndex();
		int n = mTgtView.GetItemCount();
		for(int i=0; i<n; ++i) {
			TgtListItem *item = static_cast<TgtListItem *>(mTgtView.GetVirtualItem(i));

			if (item) {
				const bool sel = (item->mTargetId == targetId);
				if (item->mbSelected != sel)  {
					item->mbSelected = sel;

					mTgtView.RefreshItem(i);
				}
			}
		}
	}
}

void ATUIDebuggerPaneTargets::InvalidateTargets() {
	if (!mbTargetUpdatePending) {
		mbTargetUpdatePending = true;
		DeferUpdate();
	}
}

void ATUIDebuggerPaneTargets::UpdateTargets() {
	mbTargetUpdatePending = false;

	mTgtView.SetRedraw(false);
	mTgtView.Clear();

	auto& dbg = *ATGetDebugger();

	vdfastvector<IATDebugTarget *> targets;
	dbg.GetTargetList(targets);

	const auto currentTargetId = dbg.GetTargetIndex();
	size_t n = targets.size();
	for(size_t i=0; i<n; ++i) {
		if (targets[i])
			mTgtView.InsertVirtualItem(-1, new TgtListItem((uint32)i, i == currentTargetId, *targets[i]));
	}

	mTgtView.SetRedraw(true);
}

void ATUIDebuggerPaneTargets::OnItemDoubleClicked() {
	TgtListItem *tgtItem = mTgtView.GetSelectedVirtualItemAs<TgtListItem>();

	if (tgtItem)
		ATGetDebugger()->SetTarget(tgtItem->mTargetId);
}

void ATUIDebuggerRegisterTargetsPane() {
	ATRegisterUIPaneType(kATUIPaneId_Targets, VDRefCountObjectFactory<ATUIDebuggerPaneTargets, ATUIPane>);
}
