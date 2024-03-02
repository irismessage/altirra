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

#ifndef AT_SCHEDULER_H
#define AT_SCHEDULER_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <vd2/system/vdstl.h>

#define ATSCHEDULER_ADVANCE(pThis) if(++(pThis)->mNextEventCounter);else((pThis)->ProcessNextEvent()); VDASSERT((pThis)->mNextEventCounter < 0);
#define ATSCHEDULER_GETTIME(pThis) ((pThis)->mNextEventCounter + (pThis)->mTimeBase)

class IATSchedulerCallback {
public:
	virtual void OnScheduledEvent(uint32 id) = 0;
};

class ATEvent;

struct ATEventLink {
	ATEventLink *mpNext;
	ATEventLink *mpPrev;
};

class ATScheduler {
public:
	ATScheduler();
	~ATScheduler();

	void ProcessNextEvent();

	void SetEvent(uint32 ticks, IATSchedulerCallback *cb, uint32 id, ATEvent *&ptr);

	ATEvent	*AddEvent(uint32 ticks, IATSchedulerCallback *cb, uint32 id);
	void	RemoveEvent(ATEvent *);

	sint32	GetTick() const { return mNextEventCounter + mTimeBase; }
	int		GetTicksToEvent(ATEvent *) const;

public:
	sint32	mNextEventCounter;
	sint32	mTimeBase;

protected:
	ATEventLink mActiveEvents;
	ATEventLink *mpFreeEvents;
};

#endif
