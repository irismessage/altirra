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

#ifndef f_AT_ATCORE_ASYNCDISPATCHER_H
#define f_AT_ATCORE_ASYNCDISPATCHER_H

#include <vd2/system/function.h>

class ATAsyncCallback;

class IATAsyncDispatcher 
{
public:
	static constexpr uint32 kTypeID = 'asdp';

	virtual ~IATAsyncDispatcher() = default;

	virtual void Queue(uint64 *token, vdfunction<void()> fn) = 0;
	virtual void Cancel(uint64 *token) = 0;
};

IATAsyncDispatcher *ATCreateAsyncDispatcher();

#endif
