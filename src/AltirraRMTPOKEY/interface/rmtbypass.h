//	Altirra RMT - POKEY emulator plugin for Raster Music Tracker
//	Copyright (C) 2022 Avery Lee
//
//	This library is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This library is distributed in the hope that it will be useful,
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

#ifndef f_AT_RMTBYPASS_H
#define f_AT_RMTBYPASS_H

class IATRMTBypassLink {
public:
	virtual void LinkInit(uint32 t0) = 0;
	virtual uint8 LinkDebugReadByte(uint32 t, uint32 address) = 0;
	virtual uint8 LinkReadByte(uint32 t, uint32 address) = 0;
	virtual void LinkWriteByte(uint32 t, uint32 address, uint8 v) = 0;
};

#endif
