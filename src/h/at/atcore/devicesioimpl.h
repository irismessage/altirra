//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2014 Avery Lee
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

#ifndef f_AT_ATCORE_DEVICESIOIMPL_H
#define f_AT_ATCORE_DEVICESIOIMPL_H

#include <at/atcore/devicesio.h>

// This is a simple base implementation of ATDeviceSIO that stubs all of
// the calls. None of the implementations need forwarding from overrides.
class ATDeviceSIO : public IATDeviceSIO {
public:
	virtual CmdResponse OnSerialBeginCommand(const ATDeviceSIOCommand& cmd);
	virtual void OnSerialAbortCommand();
	virtual void OnSerialReceiveComplete(uint32 id, const void *data, uint32 len, bool checksumOK);
	virtual void OnSerialFence(uint32 id); 

	// Calls OnSerialBeginCommand().
	virtual CmdResponse OnSerialAccelCommand(const ATDeviceSIORequest& request);
};

#endif
