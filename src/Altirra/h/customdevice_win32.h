//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2020 Avery Lee
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

#ifndef f_AT_CUSTOMDEVICE_WIN32_H
#define f_AT_CUSTOMDEVICE_WIN32_H

#include <vd2/system/vdtypes.h>
#include <vd2/system/function.h>
#include <vd2/system/refcount.h>

class IATDeviceCustomNetworkEngine : public IVDRefCount {
public:
	virtual void Shutdown() = 0;

	// Return true if a connection is currently present, regardless of whether a restore is needed
	// or not.
	virtual bool IsConnected() = 0;

	// Block until the first connection attempt is made and either succeeds or times out.
	virtual bool WaitForFirstConnectionAttempt() = 0;

	// Check for a new or restarted connection and clear the flag. This is used to ensure that the
	// caller knows that a new connection is in place and requires setup.
	virtual bool Restore() = 0;

	// Send the given buffer, blocking until all bytes are queued. Returns true if successful, false
	// if connection lost.
	virtual bool Send(const void *data, uint32 len) = 0;

	// Receive the specified number of bytes into the buffer, blocking until all bytes are available.
	// Returns true if successful, false if connection lost.
	virtual bool Recv(void *data, uint32 len) = 0;

	virtual void SetRecvHandler(vdfunction<void()> fn) = 0;
	virtual bool SetRecvNotifyEnabled(bool queueCall) = 0;

	// Return the minimal number of bytes that can be read from buffers without blocking.
	virtual uint32 Peek() = 0;
};

vdrefptr<IATDeviceCustomNetworkEngine> ATCreateDeviceCustomNetworkEngine(uint16 localhostPort);

#endif
