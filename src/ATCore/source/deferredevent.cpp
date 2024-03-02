//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2016 Avery Lee
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
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#include <stdafx.h>
#include <at/atcore/deferredevent.h>

void ATDeferredEvent::operator+=(const vdfunction<void()> *fn) {
	VDASSERT(*fn);
	mNotifyList.Add(fn);
}

void ATDeferredEvent::operator-=(const vdfunction<void()> *fn) {
	mNotifyList.Remove(fn);
}

///////////////////////////////////////////////////////////////////////////

ATDeferredEventManager::ATDeferredEventManager() {
}

ATDeferredEventManager::~ATDeferredEventManager() {
}

void ATDeferredEventManager::AddPendingEvent(ATDeferredEvent *e) {
	mPendingEvents.Add(e);
}

void ATDeferredEventManager::RemovePendingEvent(ATDeferredEvent *e) {
	mPendingEvents.Remove(e);
}

void ATDeferredEventManager::FlushPendingEvents() {
	mPendingEvents.NotifyAndClear([](ATDeferredEvent *ev) -> bool { ev->Notify(); return false; });
}
