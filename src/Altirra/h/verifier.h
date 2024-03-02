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

#ifndef f_AT_VERIFIER_H
#define f_AT_VERIFIER_H

class ATCPUEmulator;
class ATSimulator;

class ATCPUVerifier {
	ATCPUVerifier(const ATCPUVerifier&);
	ATCPUVerifier& operator=(const ATCPUVerifier&);
public:
	ATCPUVerifier();
	~ATCPUVerifier();

	void Init(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, ATSimulator *sim);

	void OnReset();
	void OnIRQEntry();
	void OnNMIEntry();
	void OnReturn();
	void VerifyJump(uint16 target);

protected:
	ATCPUEmulator *mpCPU;
	ATCPUEmulatorMemory *mpMemory;
	ATSimulator *mpSimulator;

	bool	mbInNMIRoutine;
	uint8	mNMIStackLevel;
};

#endif	// f_AT_VERIFIER_H
