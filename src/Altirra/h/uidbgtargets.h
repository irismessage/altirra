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

#ifndef f_AT_UIDBGTARGETS_H
#define f_AT_UIDBGTARGETS_H

#include <at/atnativeui/uiproxies.h>

#include "debugger.h"
#include "uidbgpane.h"

class ATUIDebuggerPaneTargets final
	: public ATUIDebuggerPaneDialog
	, public IATDebuggerClient
{
public:
	ATUIDebuggerPaneTargets();
	~ATUIDebuggerPaneTargets();

	bool OnLoaded() override;
	void OnDestroy() override;
	void OnDeferredUpdate() override;

	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) override;
	void OnDebuggerEvent(ATDebugEvent eventId) override;

private:
	class TgtListItem final : public vdrefcounted<IVDUIListViewVirtualItem> {
	public:
		TgtListItem(uint32 targetId, bool sel, IATDebugTarget& target);

		void GetText(int subItem, VDStringW& s) const override;

		uint32 mTargetId = 0;
		bool mbSelected = false;
		VDStringW mType;
		VDStringW mDescription;
	};

	void InvalidateTargets();
	void UpdateTargets();
	void OnItemDoubleClicked();

	bool mbTargetUpdatePending = false;

	VDUIProxyListView mTgtView;
};

#endif
