//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008-2010 Avery Lee
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

#ifndef AT_RTIME8_H
#define AT_RTIME8_H

class ATRTime8Emulator {
	ATRTime8Emulator(const ATRTime8Emulator&);
	ATRTime8Emulator& operator=(const ATRTime8Emulator&);
public:
	ATRTime8Emulator();
	~ATRTime8Emulator();

	uint8 ReadControl(uint8 addr);
	uint8 DebugReadControl(uint8 addr);
	void WriteControl(uint8 addr, uint8 value);

protected:
	uint8 mAddress;
	uint8 mPhase;
	uint8 mRAM[16];
};

#endif

