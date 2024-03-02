//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008 Avery Lee
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include "stdafx.h"
#include "scheduler.h"

class ATEvent : public vdlist_node {
public:
	IATSchedulerCallback *mpCB;
	uint32 mId;
	sint32 mNextTime;
};

ATScheduler::ATScheduler()
	: mNextEventCounter(-1000)
	, mTimeBase(1000)
{
}

ATScheduler::~ATScheduler() {
	mFreeEvents.splice(mFreeEvents.end(), mActiveEvents);

	while(!mFreeEvents.empty()) {
		ATEvent *ev = mFreeEvents.back();
		mFreeEvents.pop_back();

		delete ev;
	}
}

void ATScheduler::ProcessNextEvent() {

	sint32 timeToNext = 100000;
	while(!mActiveEvents.empty()) {
		ATEvent *ev = mActiveEvents.front();
		sint32 timeToNextEvent = ev->mNextTime - (mTimeBase + mNextEventCounter);

		VDASSERT(timeToNextEvent<10000000);

		if (timeToNextEvent) {
			if (timeToNext > timeToNextEvent)
				timeToNext = timeToNextEvent;
			break;
		}

		IATSchedulerCallback *cb = ev->mpCB;
		uint32 id = ev->mId;
		ev->mId = 0;

		VDASSERT(id);

		mActiveEvents.erase(ev);
		mFreeEvents.push_back(ev);

		cb->OnScheduledEvent(id);
	}

	VDASSERT((uint32)(timeToNext - 1) < 100000);
	mTimeBase += mNextEventCounter;
	mNextEventCounter = -timeToNext;
	mTimeBase -= mNextEventCounter;
}

ATEvent *ATScheduler::AddEvent(uint32 ticks, IATSchedulerCallback *cb, uint32 id) {
	VDASSERT(ticks > 0 && ticks < 10000000);
	VDASSERT(id);
	if (mFreeEvents.empty()) {
		ATEvent *p = new ATEvent;
		mFreeEvents.push_back(p);
		p->mId = 0;
	}

	ATEvent *ev = mFreeEvents.back();
	mFreeEvents.pop_back();

	VDASSERT(!ev->mId);

	ev->mpCB = cb;
	ev->mId = id;
	ev->mNextTime = mTimeBase + mNextEventCounter + ticks;

	Events::iterator it(mActiveEvents.begin()), itEnd(mActiveEvents.end());
	for(; it != itEnd; ++it) {
		if ((ev->mNextTime - mTimeBase) < ((*it)->mNextTime - mTimeBase))
			break;
	}

	if (it == mActiveEvents.begin()) {
		mTimeBase += mNextEventCounter;
		mNextEventCounter = -(sint32)ticks;
		mTimeBase -= mNextEventCounter;
		VDASSERT((uint32)-mNextEventCounter < 10000000);
	}

	mActiveEvents.insert(it, ev);

	return ev;
}

void ATScheduler::RemoveEvent(ATEvent *p) {
	bool wasFront = false;
	if (!mActiveEvents.empty() && mActiveEvents.front() == p)
		wasFront = true;

	VDASSERT(p->mId);

	mActiveEvents.erase(p);

	p->mId = 0;
	mFreeEvents.push_back(p);

	if (wasFront)
		ProcessNextEvent();
}

int ATScheduler::GetTicksToEvent(ATEvent *ev) const {
	return ev->mNextTime - (mTimeBase + mNextEventCounter);
}
