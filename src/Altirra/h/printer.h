//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2009 Avery Lee
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

#ifndef AT_PRINTER_H
#define AT_PRINTER_H

class ATCPUEmulator;
class ATCPUEmulatorMemory;

class IATPrinterOutput {
public:
	virtual void WriteLine(const char *line) = 0;
};

class IATPrinterEmulator {
public:
	virtual ~IATPrinterEmulator() {}

	virtual bool IsEnabled() const = 0;
	virtual void SetEnabled(bool enabled) = 0;

	virtual void SetHookPageByte(uint8 page) = 0;
	virtual void SetOutput(IATPrinterOutput *output) = 0;

	virtual void WarmReset() = 0;
	virtual void ColdReset() = 0;
	virtual uint8 OnCIOCommand(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, uint8 iocb, uint8 command) = 0;
	virtual void OnCIOVector(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, int offset) = 0;
};

IATPrinterEmulator *ATCreatePrinterEmulator();

#endif
