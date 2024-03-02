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

#ifndef f_AT_ATCORE_DEVICEPRINTER_H
#define f_AT_ATCORE_DEVICEPRINTER_H

#include <vd2/system/refcount.h>
#include <vd2/system/unknown.h>

class IATPrinterOutput : public IVDRefCount {
public:
	static constexpr uint32 kTypeID = "IATPrinterOutput"_vdtypeid;

	virtual void WriteASCII(const void *buf, size_t len) = 0;
	virtual void WriteATASCII(const void *buf, size_t len) = 0;
};

class IATDevicePrinterPort {
public:
	enum { kTypeID = 'adpp' };

	virtual void SetPrinterDefaultOutput(IATPrinterOutput *out) = 0;
};

#endif
