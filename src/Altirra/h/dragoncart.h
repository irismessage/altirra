//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2013 Avery Lee
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

#ifndef f_AT_DRAGONCART_H
#define f_AT_DRAGONCART_H

#include "cs8900a.h"

class ATMemoryManager;
class ATMemoryLayer;
class ATEthernetBus;
class ATEthernetSimClock;
class IATEthernetGatewayServer;
class IATNetSockWorker;

struct ATDragonCartSettings {
	enum AccessMode {
		kAccessMode_None,
		kAccessMode_HostOnly,
		kAccessMode_NAT,
		kAccessModeCount
	};

	uint32 mNetAddr;		// 0xaabbccdd -> a.b.c.d
	uint32 mNetMask;
	AccessMode mAccessMode;

	void SetDefault();
	bool operator==(const ATDragonCartSettings&) const;
	bool operator!=(const ATDragonCartSettings&) const;
};

class ATDragonCartEmulator {
	ATDragonCartEmulator(const ATDragonCartEmulator&);
	ATDragonCartEmulator& operator=(const ATDragonCartEmulator&);
public:
	ATDragonCartEmulator();
	~ATDragonCartEmulator();

	const ATDragonCartSettings& GetSettings() const { return mSettings; }

	void Init(ATMemoryManager *memmgr, ATScheduler *slowSched, const ATDragonCartSettings& settings);
	void Shutdown();

	void ColdReset();
	void WarmReset();

	void DumpConnectionInfo();

protected:
	static sint32 OnDebugRead(void *thisptr, uint32 addr);
	static sint32 OnRead(void *thisptr, uint32 addr);
	static bool OnWrite(void *thisptr, uint32 addr, uint8 value);

	ATMemoryManager *mpMemMgr;
	ATMemoryLayer *mpMemLayer;

	ATEthernetBus *mpEthernetBus;
	ATEthernetSimClock *mpEthernetClock;
	IATEthernetGatewayServer *mpGateway;
	IATNetSockWorker *mpNetSockWorker;
	uint32 mEthernetClockId;

	ATDragonCartSettings mSettings;

	ATCS8900AEmulator mCS8900A;
};

#endif
