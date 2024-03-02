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

#include "stdafx.h"
#include <list>
#include <vd2/system/filesys.h>
#include "console.h"
#include "cpu.h"
#include "simulator.h"
#include "disasm.h"
#include "debugger.h"
#include "decmath.h"
#include "symbols.h"
#include "ksyms.h"
#include "kerneldb.h"

extern ATSimulator g_sim;

void ATSetFullscreen(bool enabled);

///////////////////////////////////////////////////////////////////////////////

class ATDebugger : public IATSimulatorCallback, public IATDebugger, public IATDebuggerSymbolLookup {
public:
	ATDebugger();
	~ATDebugger();

	bool Init();

	void Detach();
	void Break();
	void Run();
	void RunTraced();
	void ClearAllBreakpoints();
	void ToggleBreakpoint(uint16 addr);
	void StepInto();
	void StepOver();
	void StepOut();
	void SetPC(uint16 pc);
	void SetFramePC(uint16 pc);
	uint32 GetCallStack(ATCallStackFrame *dst, uint32 maxCount);
	void DumpCallStack();
	void ListModules();

	void DumpCIOParameters();

	bool IsCIOTracingEnabled() const { return mbTraceCIOCalls; }

	void SetCIOTracingEnabled(bool enabled);

	// symbol handling
	uint32 AddModule(uint32 base, uint32 size, IATSymbolStore *symbolStore);
	void RemoveModule(uint32 base, uint32 size, IATSymbolStore *symbolStore);

	void AddClient(IATDebuggerClient *client, bool requestUpdate);
	void RemoveClient(IATDebuggerClient *client);

	uint32 LoadSymbols(const wchar_t *fileName);
	void UnloadSymbols(uint32 moduleId);

	sint32 ResolveSymbol(const char *s);

public:
	bool LookupSymbol(uint32 moduleOffset, uint32 flags, ATSymbol& symbol);
	bool LookupLine(uint32 addr, uint32& moduleId, ATSourceLineInfo& lineInfo);
	bool LookupFile(const wchar_t *fileName, uint32& moduleId, uint16& fileId);
	void GetLinesForFile(uint32 moduleId, uint16 fileId, vdfastvector<ATSourceLineInfo>& lines);

public:
	void OnSimulatorEvent(ATSimulatorEvent ev);

protected:
	void UpdateClientSystemState(IATDebuggerClient *client = NULL);

	struct Module {
		uint32	mId;
		uint32	mBase;
		uint32	mSize;
		vdrefptr<IATSymbolStore>	mpSymbols;
	};

	uint32	mNextModuleId;
	uint16	mFramePC;
	bool mbTraceCIOCalls;

	typedef std::list<Module> Modules; 
	Modules		mModules;

	typedef std::vector<IATDebuggerClient *> Clients;
	Clients mClients;
};

ATDebugger g_debugger;

IATDebugger *ATGetDebugger() { return &g_debugger; }
IATDebuggerSymbolLookup *ATGetDebuggerSymbolLookup() { return &g_debugger; }

void ATInitDebugger() {
	g_debugger.Init();
}

ATDebugger::ATDebugger()
	: mNextModuleId(1)
	, mbTraceCIOCalls(false)
	, mFramePC(0)
{
}

ATDebugger::~ATDebugger() {
}

bool ATDebugger::Init() {
	g_sim.AddCallback(this);

	mModules.push_back(Module());
	Module& varmod = mModules.back();
	ATCreateDefaultVariableSymbolStore(~varmod.mpSymbols);
	varmod.mBase = varmod.mpSymbols->GetDefaultBase();
	varmod.mSize = varmod.mpSymbols->GetDefaultSize();

	mModules.push_back(Module());
	Module& kernmod = mModules.back();
	ATCreateDefaultKernelSymbolStore(~kernmod.mpSymbols);
	kernmod.mBase = kernmod.mpSymbols->GetDefaultBase();
	kernmod.mSize = kernmod.mpSymbols->GetDefaultSize();

	mModules.push_back(Module());
	Module& hwmod = mModules.back();
	ATCreateDefaultHardwareSymbolStore(~hwmod.mpSymbols);
	hwmod.mBase = hwmod.mpSymbols->GetDefaultBase();
	hwmod.mSize = hwmod.mpSymbols->GetDefaultSize();

	return true;
}

void ATDebugger::Detach() {
	ATCPUEmulator& cpu = g_sim.GetCPU();
	cpu.SetStep(false);
	cpu.SetTrace(false);
	g_sim.Resume();

	ClearAllBreakpoints();
	UpdateClientSystemState();
}

void ATDebugger::Break() {
	if (g_sim.IsRunning()) {
		g_sim.Suspend();

		ATCPUEmulator& cpu = g_sim.GetCPU();
		cpu.DumpStatus();

		mFramePC = cpu.GetInsnPC();
		UpdateClientSystemState();
	}
}

void ATDebugger::Run() {
	if (g_sim.IsRunning())
		return;

	ATCPUEmulator& cpu = g_sim.GetCPU();
	cpu.SetStep(false);
	cpu.SetTrace(false);
	g_sim.Resume();
	UpdateClientSystemState();
}

void ATDebugger::RunTraced() {
	if (g_sim.IsRunning())
		return;

	ATCPUEmulator& cpu = g_sim.GetCPU();
	cpu.SetStep(false);
	cpu.SetTrace(true);
	g_sim.Resume();
	UpdateClientSystemState();
}

void ATDebugger::ClearAllBreakpoints() {
	g_sim.GetCPU().ClearAllBreakpoints();
	g_sim.NotifyEvent(kATSimEvent_CPUPCBreakpointsUpdated);
}

void ATDebugger::ToggleBreakpoint(uint16 addr) {
	ATCPUEmulator& cpu = g_sim.GetCPU();

	if (cpu.IsBreakpointSet(addr))
		cpu.ClearBreakpoint(addr);
	else
		cpu.SetBreakpoint(addr);

	g_sim.NotifyEvent(kATSimEvent_CPUPCBreakpointsUpdated);
}

void ATDebugger::StepInto() {
	if (g_sim.IsRunning())
		return;

	ATCPUEmulator& cpu = g_sim.GetCPU();

	cpu.SetTrace(false);
	cpu.SetStep(true);
	g_sim.Resume();
	UpdateClientSystemState();
}

void ATDebugger::StepOver() {
	if (g_sim.IsRunning())
		return;

	ATCPUEmulator& cpu = g_sim.GetCPU();

	uint8 opcode = g_sim.DebugReadByte(cpu.GetPC());

	if (opcode == 0x20) {
		cpu.SetRTSBreak(cpu.GetS());
		cpu.SetStep(false);
		cpu.SetTrace(false);
	} else {
		cpu.SetStep(true);
		cpu.SetTrace(false);
	}

	g_sim.Resume();
	UpdateClientSystemState();
}

void ATDebugger::StepOut() {
	if (g_sim.IsRunning())
		return;

	ATCPUEmulator& cpu = g_sim.GetCPU();
	uint8 s = cpu.GetS();
	if (s == 0xFF)
		return StepInto();

	++s;

	ATCallStackFrame frames[2];
	uint32 framecount = GetCallStack(frames, 2);

	if (framecount >= 2)
		s = frames[1].mS;

	cpu.SetStep(false);
	cpu.SetTrace(false);
	cpu.SetRTSBreak(s);
	g_sim.Resume();
	UpdateClientSystemState();
}

void ATDebugger::SetPC(uint16 pc) {
	if (g_sim.IsRunning())
		return;

	ATCPUEmulator& cpu = g_sim.GetCPU();
	cpu.SetPC(pc);
	UpdateClientSystemState();
}

void ATDebugger::SetFramePC(uint16 pc) {
	mFramePC = pc;

	UpdateClientSystemState();
}

namespace {
	struct StackState {
		uint16 mPC;
		uint8 mS;
		uint8 mP;
	};
}

uint32 ATDebugger::GetCallStack(ATCallStackFrame *dst, uint32 maxCount) {
	const ATCPUEmulator& cpu = g_sim.GetCPU();
	uint8 vS = cpu.GetS();
	uint8 vP = cpu.GetP();
	uint16 vPC = cpu.GetInsnPC();

	uint32 seenFlags[2048] = {0};
	std::deque<StackState> q;

	uint32 frameCount = 0;

	for(uint32 i=0; i<maxCount; ++i) {
		dst->mPC = vPC;
		dst->mS = vS;
		dst->mP = vP;
		++dst;

		q.clear();

		StackState ss = { vPC, vS, vP };
		q.push_back(ss);

		bool found = false;
		int insnLimit = 1000;
		while(!q.empty() && insnLimit--) {
			ss = q.front();
			q.pop_front();

			vPC = ss.mPC;
			vS = ss.mS;
			vP = ss.mP;

			uint32& seenFlagWord = seenFlags[vPC >> 5];
			uint32 seenBit = (1 << (vPC & 31));
			if (seenFlagWord & seenBit)
				continue;

			seenFlagWord |= seenBit;

			uint8 opcode = g_sim.DebugReadByte(vPC);
			uint16 nextPC = vPC + ATGetOpcodeLength(opcode);

			if (opcode == 0x00)				// BRK
				continue;
			else if (opcode == 0x58)		// CLI
				vP &= ~0x04;
			else if (opcode == 0x78)		// SEI
				vP |= 0x04;
			else if (opcode == 0x4C)		// JMP abs
				nextPC = g_sim.DebugReadWord(vPC + 1);
			else if (opcode == 0x6C) {		// JMP (ind)
				nextPC = g_sim.DebugReadWord(g_sim.DebugReadWord(vPC + 1));
			} else if (opcode == 0x40) {	// RTI
				if (vS > 0xFC)
					continue;
				vP = g_sim.DebugReadByte(vS + 0x0101);
				vPC = g_sim.DebugReadWord(vS + 0x0102);
				vS += 3;
				found = true;
				break;
			} else if (opcode == 0x60) {	// RTS
				if (vS > 0xFD)
					continue;
				vPC = g_sim.DebugReadWord(vS + 0x0101) + 1;
				vS += 2;
				found = true;
				break;
			} else if (opcode == 0x08) {	// PHP
				if (!vS)
					continue;
				--vS;
			} else if (opcode == 0x28) {	// PLP
				if (vS == 0xFF)
					continue;
				++vS;
				vP = g_sim.DebugReadByte(0x100 + vS);
			} else if (opcode == 0x48) {	// PHA
				if (!vS)
					continue;
				--vS;
			} else if (opcode == 0x68) {	// PLA
				if (vS == 0xFF)
					continue;
				++vS;
			} else if ((opcode & 0x1f) == 0x10) {	// Bcc
				ss.mS	= vS;
				ss.mP	= vP;
				ss.mPC = nextPC + (sint16)(sint8)g_sim.DebugReadByte(vPC + 1);
				q.push_back(ss);
			}

			ss.mS	= vS;
			ss.mP	= vP;
			ss.mPC	= nextPC;
			q.push_back(ss);
		}

		if (!found)
			return i + 1;
	}

	return maxCount;
}

void ATDebugger::DumpCallStack() {
	ATCallStackFrame frames[16];

	uint32 frameCount = GetCallStack(frames, 16);

	ATConsolePrintf("I SP    PC\n");
	ATConsolePrintf("----------------------\n");
	for(uint32 i=0; i<frameCount; ++i) {
		const ATCallStackFrame& fr = frames[i];
		ATSymbol sym;
		const char *symname = "";
		if (LookupSymbol(fr.mPC, kATSymbol_Execute, sym))
			symname = sym.mpName;
		ATConsolePrintf("%c %04X  %04X (%s)\n", fr.mP & 0x04 ? '*' : ' ', fr.mS + 0x0100, fr.mPC, symname);
	}

	ATConsolePrintf("End of stack trace.\n");
}

void ATDebugger::ListModules() {
	Modules::iterator it(mModules.begin()), itEnd(mModules.end());
	for(; it!=itEnd; ++it) {
		const Module& mod = *it;

		ATConsolePrintf("%04x-%04x  %s\n", mod.mBase, mod.mBase + mod.mSize - 1, mod.mpSymbols ? "(symbols loaded)" : "(no symbols)");
	}
}

void ATDebugger::DumpCIOParameters() {
	const ATCPUEmulator& cpu = g_sim.GetCPU();
	uint8 iocb = cpu.GetX();
	unsigned iocbIdx = iocb >> 4;
	uint8 cmd = g_sim.DebugReadByte(iocb + ATKernelSymbols::ICCMD);

	switch(cmd) {
		case 0x03:
			{
				char fn[128];
				int idx = 0;
				uint16 bufadr = g_sim.DebugReadWord(iocb + ATKernelSymbols::ICBAL);

				while(idx < 127) {
					uint8 c = g_sim.DebugReadByte(bufadr + idx);

					if (c < 0x20 || c >= 0x7f)
						break;

					fn[idx++] = c;
				}

				fn[idx] = 0;

				ATConsolePrintf("CIO: IOCB=%u, CMD=$03 (open), filename=\"%s\"\n", iocbIdx, fn);
			}
			break;

		case 0x05:
			ATConsolePrintf("CIO: IOCB=%u, CMD=$05 (get record)\n", iocbIdx);
			break;

		case 0x07:
			ATConsolePrintf("CIO: IOCB=%u, CMD=$07 (get characters)\n", iocbIdx);
			break;

		case 0x09:
			ATConsolePrintf("CIO: IOCB=%u, CMD=$09 (put record)\n", iocbIdx);
			break;

		case 0x0B:
			ATConsolePrintf("CIO: IOCB=%u, CMD=$07 (put characters)\n", iocbIdx);
			break;

		case 0x0C:
			ATConsolePrintf("CIO: IOCB=%u, CMD=$0C (close)\n", iocbIdx);
			break;

		default:
			ATConsolePrintf("CIO: IOCB=%u, CMD=$%02x (unknown)\n", iocbIdx, cmd);
			break;
	}
}

void ATDebugger::SetCIOTracingEnabled(bool enabled) {
	ATCPUEmulator& cpu = g_sim.GetCPU();

	if (mbTraceCIOCalls == enabled)
		return;

	mbTraceCIOCalls = enabled;

	if (enabled)
		cpu.SetBreakpoint(ATKernelSymbols::CIOV);
	else
		cpu.ClearBreakpoint(ATKernelSymbols::CIOV);
}

uint32 ATDebugger::AddModule(uint32 base, uint32 size, IATSymbolStore *symbolStore) {
	Modules::const_iterator it(mModules.begin()), itEnd(mModules.end());
	for(; it!=itEnd; ++it) {
		const Module& mod = *it;

		if (mod.mBase == base && mod.mSize == size && mod.mpSymbols == symbolStore)
			return mod.mId;
	}

	Module newmod;
	newmod.mId = mNextModuleId++;
	newmod.mBase = base;
	newmod.mSize = size;
	newmod.mpSymbols = symbolStore;
	mModules.push_back(newmod);

	return newmod.mId;
}

void ATDebugger::RemoveModule(uint32 base, uint32 size, IATSymbolStore *symbolStore) {
	Modules::iterator it(mModules.begin()), itEnd(mModules.end());
	for(; it!=itEnd; ++it) {
		const Module& mod = *it;

		if (mod.mBase == base && mod.mSize == size && (!symbolStore || mod.mpSymbols == symbolStore)) {
			mModules.erase(it);
			return;
		}
	}
}

void ATDebugger::AddClient(IATDebuggerClient *client, bool requestUpdate) {
	Clients::const_iterator it(std::find(mClients.begin(), mClients.end(), client));

	if (it == mClients.end()) {
		mClients.push_back(client);

		if (requestUpdate)
			UpdateClientSystemState(client);
	}
}

void ATDebugger::RemoveClient(IATDebuggerClient *client) {
	Clients::iterator it(std::find(mClients.begin(), mClients.end(), client));

	if (it != mClients.end()) {
		*it = mClients.back();
		mClients.pop_back();
	}
}

uint32 ATDebugger::LoadSymbols(const wchar_t *fileName) {
	vdrefptr<IATSymbolStore> symStore;

	if (ATLoadSymbols(fileName, ~symStore))
		return g_debugger.AddModule(symStore->GetDefaultBase(), symStore->GetDefaultSize(), symStore);

	return 0;
}

void ATDebugger::UnloadSymbols(uint32 moduleId) {
	Modules::iterator it(mModules.begin()), itEnd(mModules.end());
	for(; it!=itEnd; ++it) {
		const Module& mod = *it;

		if (mod.mId == moduleId) {
			mModules.erase(it);
			return;
		}
	}
}

sint32 ATDebugger::ResolveSymbol(const char *s) {
	if (s[0] == '$') {
		++s;
	} else {
		Modules::const_iterator it(mModules.begin()), itEnd(mModules.end());
		for(; it!=itEnd; ++it) {
			const Module& mod = *it;
			sint32 offset = mod.mpSymbols->LookupSymbol(s);

			if (offset >= 0)
				return 0xffff & (mod.mBase + offset);
		}
	}

	char *t;
	unsigned long result = strtoul(s, &t, 16);

	if (result > 0xffff || *t)
		return -1;

	return result;
}

bool ATDebugger::LookupSymbol(uint32 addr, uint32 flags, ATSymbol& symbol) {
	Modules::const_iterator it(mModules.begin()), itEnd(mModules.end());
	for(; it!=itEnd; ++it) {
		const Module& mod = *it;
		uint32 offset = addr - mod.mBase;

		if (offset < mod.mSize && mod.mpSymbols) {
			if (mod.mpSymbols->LookupSymbol(offset, flags, symbol)) {
				symbol.mOffset += mod.mBase;
				return true;
			}
		}
	}

	return false;
}

bool ATDebugger::LookupLine(uint32 addr, uint32& moduleId, ATSourceLineInfo& lineInfo) {
	Modules::const_iterator it(mModules.begin()), itEnd(mModules.end());
	for(; it!=itEnd; ++it) {
		const Module& mod = *it;
		uint32 offset = addr - mod.mBase;

		if (offset < mod.mSize && mod.mpSymbols) {
			if (mod.mpSymbols->GetLineForOffset(offset, lineInfo)) {
				moduleId = mod.mId;
				return true;
			}
		}
	}

	return false;
}

bool ATDebugger::LookupFile(const wchar_t *fileName, uint32& moduleId, uint16& fileId) {
	Modules::const_iterator it(mModules.begin()), itEnd(mModules.end());
	for(; it!=itEnd; ++it) {
		const Module& mod = *it;

		fileId = mod.mpSymbols->GetFileId(fileName);

		if (fileId) {
			moduleId = mod.mId;
			return true;
		}
	}

	return false;
}

void ATDebugger::GetLinesForFile(uint32 moduleId, uint16 fileId, vdfastvector<ATSourceLineInfo>& lines) {
	Modules::const_iterator it(mModules.begin()), itEnd(mModules.end());
	for(; it!=itEnd; ++it) {
		const Module& mod = *it;

		if (mod.mId == moduleId) {
			mod.mpSymbols->GetLines(fileId, lines);
			break;
		}
	}
}

void ATDebugger::OnSimulatorEvent(ATSimulatorEvent ev) {
	ATCPUEmulator& cpu = g_sim.GetCPU();

	if (ev == kATSimEvent_CPUPCBreakpoint) {
		if (mbTraceCIOCalls && cpu.GetPC() == ATKernelSymbols::CIOV) {
			DumpCIOParameters();
			Run();
			return;
		}
	}

	ATSetFullscreen(false);
	ATShowConsole();

	switch(ev) {
		case kATSimEvent_CPUSingleStep:
			cpu.DumpStatus();
			break;
		case kATSimEvent_CPUStackBreakpoint:
			cpu.DumpStatus();
			break;
		case kATSimEvent_CPUPCBreakpoint:
			ATConsolePrintf("CPU: Breakpoint hit: %04X\n", cpu.GetPC());
			cpu.DumpStatus();
			break;

		case kATSimEvent_CPUIllegalInsn:
			ATConsolePrintf("CPU: Illegal instruction hit: %04X\n", cpu.GetPC());
			cpu.DumpStatus();
			break;

		case kATSimEvent_ReadBreakpoint:
			ATConsolePrintf("CPU: Address read breakpoint hit: %04X\n", g_sim.GetReadBreakAddress());
			cpu.DumpStatus();
			break;

		case kATSimEvent_WriteBreakpoint:
			ATConsolePrintf("CPU: Address write breakpoint hit: %04X\n", g_sim.GetWriteBreakAddress());
			cpu.DumpStatus();
			break;

		case kATSimEvent_DiskSectorBreakpoint:
			ATConsolePrintf("DISK: Sector breakpoint hit: %d\n", g_sim.GetDiskDrive(0).GetSectorBreakpoint());
			break;

		case kATSimEvent_EndOfFrame:
			ATConsoleWrite("End of frame reached.\n");
			break;

		case kATSimEvent_ScanlineBreakpoint:
			ATConsoleWrite("Scanline breakpoint reached.\n");
			break;
	}

	cpu.SetRTSBreak();

	mFramePC = cpu.GetInsnPC();

	UpdateClientSystemState();
}

void ATDebugger::UpdateClientSystemState(IATDebuggerClient *client) {
	ATCPUEmulator& cpu = g_sim.GetCPU();

	ATDebuggerSystemState sysstate;
	sysstate.mPC = cpu.GetPC();
	sysstate.mInsnPC = cpu.GetInsnPC();
	sysstate.mA = cpu.GetA();
	sysstate.mX = cpu.GetX();
	sysstate.mY = cpu.GetY();
	sysstate.mP = cpu.GetP();
	sysstate.mS = cpu.GetS();
	sysstate.mAH = cpu.GetAH();
	sysstate.mXH = cpu.GetXH();
	sysstate.mYH = cpu.GetYH();
	sysstate.mSH = cpu.GetSH();
	sysstate.mB = cpu.GetB();
	sysstate.mK = cpu.GetK();

	sysstate.mPCModuleId = 0;
	sysstate.mPCFileId = 0;
	sysstate.mPCLine = 0;
	sysstate.mFramePC = mFramePC;
	sysstate.mbRunning = g_sim.IsRunning();

	if (!sysstate.mbRunning) {
		ATSourceLineInfo lineInfo;
		if (LookupLine(sysstate.mPC, sysstate.mPCModuleId, lineInfo)) {
			sysstate.mPCFileId = lineInfo.mFileId;
			sysstate.mPCLine = lineInfo.mLine;
		}
	}

	if (client)
		client->OnDebuggerSystemStateUpdate(sysstate);
	else {
		Clients::const_iterator it(mClients.begin()), itEnd(mClients.end());
		for(; it!=itEnd; ++it) {
			IATDebuggerClient *client = *it;

			client->OnDebuggerSystemStateUpdate(sysstate);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////

void ATConsoleCmdTrace() {
	ATGetDebugger()->StepInto();
}

void ATConsoleCmdGo() {
	ATGetDebugger()->Run();
}

void ATConsoleCmdGoTraced() {
	ATGetDebugger()->RunTraced();
}

void ATConsoleCmdGoFrameEnd() {
	g_sim.SetBreakOnFrameEnd(true);
	g_sim.Resume();
}

void ATConsoleCmdGoReturn() {
	ATGetDebugger()->StepOut();
}

void ATConsoleCmdGoScanline(int argc, const char *const *argv) {
	int scan;
	char dummy;
	if (!argc || 1 != sscanf(argv[0], " %d %c", &scan, &dummy) || scan < 0 || scan >= 312) {
		ATConsoleWrite("Invalid scanline.\n");
		return;
	}

	g_sim.SetBreakOnScanline(scan);
	g_sim.Resume();
}

void ATConsoleCmdCallStack() {
	ATGetDebugger()->DumpCallStack();
}

void ATConsoleCmdStepOver() {
	ATGetDebugger()->StepOver();
}

void ATConsoleCmdBreakpt(int argc, const char *const *argv) {
	if (!argc)
		return;

	if (argc >= 1) {
		sint32 v = g_debugger.ResolveSymbol(argv[0]);

		if (v < 0)
			ATConsoleWrite("Invalid breakpoint address.\n");
		else {
			g_sim.GetCPU().SetBreakpoint((uint16)v);
			g_sim.NotifyEvent(kATSimEvent_CPUPCBreakpointsUpdated);
			ATConsolePrintf("Breakpoint set at %04X.\n", v);
		}
	}
}

void ATConsoleCmdBreakptClear(int argc, const char *const *argv) {
	if (!argc)
		return;

	if (argc >= 1) {
		if (argv[0][0] == '*') {
			ATGetDebugger()->ClearAllBreakpoints();
			ATConsoleWrite("All breakpoints cleared.\n");
		} else {
			unsigned long v = strtoul(argv[0], NULL, 16);
			if (v >= 0x10000)
				ATConsoleWrite("Invalid breakpoint address.\n");
			else {
				g_sim.GetCPU().ClearBreakpoint((uint16)v);
				g_sim.NotifyEvent(kATSimEvent_CPUPCBreakpointsUpdated);
				ATConsolePrintf("Breakpoint cleared at %04X.\n", v);
			}
		}
	}
}

void ATConsoleCmdBreakptAccess(int argc, const char *const *argv) {
	if (argc < 2)
		return;

	bool readMode = true;
	if (argv[0][0] == 'w')
		readMode = false;
	else if (argv[0][0] != 'r') {
		ATConsoleWrite("Access mode must be 'r' or 'w'\n");
		return;
	}

	if (argv[1][0] == '*') {
		if (readMode) {
			ATConsoleWrite("Read breakpoint cleared.\n");
			g_sim.SetReadBreakAddress();
		} else {
			ATConsoleWrite("Write breakpoint cleared.\n");
			g_sim.SetWriteBreakAddress();
		}
	} else {
		sint32 v = g_debugger.ResolveSymbol(argv[1]);

		if (v < 0)
			ATConsolePrintf("Unknown symbol: %s\n", argv[1]);
		else if (readMode) {
			ATConsolePrintf("Read breakpoint set at %04X.\n", (uint16)v);
			g_sim.SetReadBreakAddress((uint16)v);
		} else {
			ATConsolePrintf("Write breakpoint set at %04X.\n", (uint16)v);
			g_sim.SetWriteBreakAddress((uint16)v);
		}
	}
}

void ATConsoleCmdBreakptList() {
	ATCPUEmulator& cpu = g_sim.GetCPU();

	ATConsoleWrite("Breakpoints:\n");
	sint32 addr = -1;
	while((addr = cpu.GetNextBreakpoint(addr)) >= 0) {
		ATConsolePrintf("  %04X\n", addr);
	}

	if (g_sim.IsReadBreakEnabled())
		ATConsolePrintf("Read breakpoint address:  %04X\n", g_sim.GetReadBreakAddress());
	if (g_sim.IsWriteBreakEnabled())
		ATConsolePrintf("Write breakpoint address: %04X\n", g_sim.GetWriteBreakAddress());

	ATDiskEmulator& disk = g_sim.GetDiskDrive(0);
	int sb = disk.GetSectorBreakpoint();

	if (sb >= 0)
		ATConsolePrintf("Sector breakpoint:        %d\n", sb);
}

void ATConsoleCmdBreakptSector(int argc, const char *const *argv) {
	ATDiskEmulator& disk = g_sim.GetDiskDrive(0);
	if (argc >= 1) {
		if (argv[0][0] == '*') {
			disk.SetSectorBreakpoint(-1);
			ATConsolePrintf("Disk sector breakpoint is disabled.\n");
		} else {
			int v = strtol(argv[0], NULL, 0);

			disk.SetSectorBreakpoint(v);
			ATConsolePrintf("Disk sector breakpoint is now %d.\n", v);
		}
	}
}

void ATConsoleCmdUnassemble(int argc, const char *const *argv) {
	ATCPUEmulator& cpu = g_sim.GetCPU();
	uint16 addr = cpu.GetInsnPC();

	if (argc >= 1) {
		sint32 v = g_debugger.ResolveSymbol(argv[0]);

		if (v < 0) {
			ATConsoleWrite("Invalid starting address.\n");
			return;
		}

		addr = (uint16)v;
	}

	for(int i=0; i<20; ++i)
		addr = ATDisassembleInsn(addr);
}

void ATConsoleCmdRegisters(int argc, const char *const *argv) {
	ATCPUEmulator& cpu = g_sim.GetCPU();

	if (!argc) {
		cpu.DumpStatus();
		return;
	}

	if (argc < 2) {
		ATConsoleWrite("Syntax: r reg value\n");
		return;
	}

	uint16 v = (uint16)strtoul(argv[1], NULL, 16);

	if (!strcmp(argv[0], "pc")) {
		cpu.SetPC(v);
	} else {
		ATConsolePrintf("Unknown register '%s'\n", argv[0]);
	}
}

void ATConsoleCmdDumpATASCII(int argc, const char *const *argv) {
	if (!argc)
		return;

	sint32 v = g_debugger.ResolveSymbol(argv[0]);
	if (v < 0) {
		ATConsolePrintf("Unable to resolve symbol: %s\n", argv[0]);
		return;
	}

	uint16 addr = (uint16)v;

	char str[128];
	int idx = 0;

	while(idx < 127) {
		uint8 c = g_sim.DebugReadByte(addr + idx);

		if (c < 0x20 || c >= 0x7f)
			break;

		str[idx++] = c;
	}

	str[idx] = 0;

	ATConsolePrintf("%04X: \"%s\"\n", addr, str);
}

void ATConsoleCmdDumpBytes(int argc, const char *const *argv) {
	if (!argc)
		return;

	sint32 v = g_debugger.ResolveSymbol(argv[0]);
	if (v < 0) {
		ATConsolePrintf("Unable to resolve symbol: %s\n", argv[0]);
		return;
	}

	uint16 addr = (uint16)v;

	ATConsolePrintf("%04X: %02X %02X %02X %02X %02X %02X %02X %02X-%02X %02X %02X %02X %02X %02X %02X %02X\n"
		, addr
		, g_sim.DebugReadByte(addr + 0)
		, g_sim.DebugReadByte(addr + 1)
		, g_sim.DebugReadByte(addr + 2)
		, g_sim.DebugReadByte(addr + 3)
		, g_sim.DebugReadByte(addr + 4)
		, g_sim.DebugReadByte(addr + 5)
		, g_sim.DebugReadByte(addr + 6)
		, g_sim.DebugReadByte(addr + 7)
		, g_sim.DebugReadByte(addr + 8)
		, g_sim.DebugReadByte(addr + 9)
		, g_sim.DebugReadByte(addr + 10)
		, g_sim.DebugReadByte(addr + 11)
		, g_sim.DebugReadByte(addr + 12)
		, g_sim.DebugReadByte(addr + 13)
		, g_sim.DebugReadByte(addr + 14)
		, g_sim.DebugReadByte(addr + 15));
}

void ATConsoleCmdDumpWords(int argc, const char *const *argv) {
	if (!argc)
		return;

	sint32 v = g_debugger.ResolveSymbol(argv[0]);
	if (v < 0) {
		ATConsolePrintf("Unable to resolve symbol: %s\n", argv[0]);
		return;
	}

	uint16 addr = (uint16)v;

	ATConsolePrintf("%04X: %04X %04X %04X %04X-%04X %04X %04X %04X\n"
		, addr
		, g_sim.DebugReadByte(addr + 0) + 256 * g_sim.DebugReadByte(addr + 1)
		, g_sim.DebugReadByte(addr + 2) + 256 * g_sim.DebugReadByte(addr + 3)
		, g_sim.DebugReadByte(addr + 4) + 256 * g_sim.DebugReadByte(addr + 5)
		, g_sim.DebugReadByte(addr + 6) + 256 * g_sim.DebugReadByte(addr + 7)
		, g_sim.DebugReadByte(addr + 8) + 256 * g_sim.DebugReadByte(addr + 9)
		, g_sim.DebugReadByte(addr + 10) + 256 * g_sim.DebugReadByte(addr + 11)
		, g_sim.DebugReadByte(addr + 12) + 256 * g_sim.DebugReadByte(addr + 13)
		, g_sim.DebugReadByte(addr + 14) + 256 * g_sim.DebugReadByte(addr + 15));
}

void ATConsoleCmdDumpFloats(int argc, const char *const *argv) {
	if (!argc)
		return;

	sint32 v = g_debugger.ResolveSymbol(argv[0]);
	if (v < 0) {
		ATConsolePrintf("Unable to resolve symbol: %s\n", argv[0]);
		return;
	}

	uint16 addr = (uint16)v;

	VDStringA s;

	ATConsolePrintf("%04X: %g\n", addr, ATReadDecFloatAsBinary(g_sim.GetCPUMemory(), addr));
}

void ATConsoleCmdListModules() {
	g_debugger.ListModules();
}

void ATConsoleCmdEnter(int argc, const char *const *argv) {
	{
		if (argc < 2)
			goto usage;

		sint32 v = g_debugger.ResolveSymbol(argv[0]);
		if (v < 0) {
			ATConsolePrintf("Unable to resolve symbol: %s\n", argv[0]);
			return;
		}

		uint16 addr = (uint16)v;

		vdfastvector<uint8> data(argc - 1);
		for(int i=1; i<argc; ++i) {
			char dummy;
			unsigned byte;
			if (1 != sscanf(argv[i], "%2x%c", &byte, &dummy))
				goto usage;

			data[i-1] = (uint8)byte;
		}

		ATCPUEmulatorMemory& mem = g_sim.GetCPUMemory();
		for(int i=1; i<argc; ++i) {
			mem.WriteByte(addr + (i - 1), data[i - 1]);
		}
	}
	return;

usage:
	ATConsoleWrite("Syntax: e <address> <byte> [<byte>...]\n");
}

void ATConsoleCmdDumpDisplayList(int argc, const char *const *argv) {
	uint16 addr = g_sim.GetAntic().GetDisplayListPointer();

	if (argc >= 1) {
		unsigned long v = strtoul(argv[0], NULL, 16);
		if (v >= 0x10000) {
			ATConsoleWrite("Invalid starting address.\n");
			return;
		}

		addr = (uint16)v;
	}

	for(int i=0; i<500; ++i) {
		uint16 baseaddr = addr;
		uint8 b = g_sim.DebugAnticReadByte(addr);
		addr = (addr & 0xfc00) + ((addr + 1) & 0x03ff);

		switch(b & 15) {
			case 0:
				ATConsolePrintf("  %04X: blank%s %d\n", baseaddr, b&128 ? ".i" : "", ((b >> 4) & 7) + 1);
				break;
			case 1:
				{
					uint16 addr2 = g_sim.DebugAnticReadByte(addr);
					addr = (addr & 0xfc00) + ((addr + 1) & 0x03ff);
					addr2 += 256*g_sim.DebugAnticReadByte(addr);
					addr = (addr & 0xfc00) + ((addr + 1) & 0x03ff);

					if (b & 64) {
						ATConsolePrintf("  %04X: waitvbl%s %04X\n", baseaddr, b&128 ? ".i" : "", addr2);
						return;
					} else {
						ATConsolePrintf("  %04X: jump%s %04X\n", baseaddr, b&128 ? ".i" : "", addr2);
						addr = addr2;
					}
				}
				break;
			default:
				ATConsolePrintf("  %04X: mode%s%s%s %X\n"
					, baseaddr
					, b&128 ? ".i" : ""
					, b&32 ? ".v" : ""
					, b&16 ? ".h" : ""
					, b&15);

				if (b & 64) {
					uint16 addr2 = g_sim.DebugAnticReadByte(addr);
					addr = (addr & 0xfc00) + ((addr + 1) & 0x03ff);
					addr2 += 256*g_sim.DebugAnticReadByte(addr);
					addr = (addr & 0xfc00) + ((addr + 1) & 0x03ff);

					ATConsolePrintf("        lms %04X\n", addr2);
				}
				break;
		}
	}
	ATConsoleWrite("(display list too long)\n");
}

void ATConsoleCmdDumpDLHistory() {
	const ATAnticEmulator::DLHistoryEntry *history = g_sim.GetAntic().GetDLHistory();

	ATConsolePrintf("Ycoord DLIP PFAD H V DMACTL MODE\n");
	ATConsolePrintf("--------------------------------\n");

	for(int y=0; y<262; ++y) {
		const ATAnticEmulator::DLHistoryEntry& hval = history[y];

		if (!hval.mbValid)
			continue;

		ATConsolePrintf("  %3d: %04x %04x %x %x   %02x   %02x\n"
			, y
			, hval.mDLAddress
			, hval.mPFAddress
			, hval.mHVScroll & 15
			, hval.mHVScroll >> 4
			, hval.mDMACTL
			, hval.mControl
			);
	}
}

void ATConsoleCmdDumpHistory(int argc, const char *const *argv) {
	int histlen = 32;
	const char *wild = NULL;
	bool compressed = false;
	bool interruptsOnly = false;
	int histstart = -1;

	if (argc >= 1 && !strcmp(argv[0], "-i")) {
		--argc;
		++argv;
		interruptsOnly = true;
	}

	if (argc >= 1 && !strcmp(argv[0], "-c")) {
		--argc;
		++argv;
		compressed = true;
	}

	if (argc >= 2 && !strcmp(argv[0], "-s")) {
		histstart = atoi(argv[1]);
		argc -= 2;
		argv += 2;
		compressed = true;
	}

	if (argc >= 1) {
		histlen = atoi(argv[0]);
		if (argc >= 2) {
			wild = argv[1];
		}
	}

	const ATCPUEmulator& cpu = g_sim.GetCPU();
	char buf[512];
	uint16 predictor[4] = {0,0,0,0};
	int predictLen = 0;

	if (histstart < 0)
		histstart = histlen - 1;

	int histend = histstart - histlen + 1;
	if (histend < 0)
		histend = 0;

	uint16 nmi = g_sim.DebugReadWord(0xFFFA);
	uint16 irq = g_sim.DebugReadWord(0xFFFE);

	for(int i=histstart; i >= histend; --i) {
		const ATCPUHistoryEntry& he = cpu.GetHistory(i);
		uint16 pc = he.mPC;

		if (compressed) {
			if (pc == predictor[0] || pc == predictor[1] || pc == predictor[2] || pc == predictor[3])
				++predictLen;
			else {
				if (predictLen > 4)
					ATConsolePrintf("[%d lines omitted]\n", predictLen - 4);
				predictLen = 0;
			}

			predictor[i & 3] = pc;

			if (predictLen > 4)
				continue;
		}

		if (interruptsOnly && pc != nmi && pc != irq)
			continue;

		int y = (he.mTimestamp >> 8) & 0xfff;

		sprintf(buf, "%7d) T=%05d|%3d,%3d A=%02x X=%02x Y=%02x S=%02x P=%02x (%c%c%c%c%c%c%c%c) "
			, i
			, he.mTimestamp >> 20
			, y
			, he.mTimestamp & 0xff
			, he.mA, he.mX, he.mY, he.mS, he.mP
			, he.mP & 0x80 ? 'N' : ' '
			, he.mP & 0x40 ? 'V' : ' '
			, he.mP & 0x20 ? '1' : '0'
			, he.mP & 0x10 ? 'B' : ' '
			, he.mP & 0x08 ? 'D' : ' '
			, he.mP & 0x04 ? 'I' : ' '
			, he.mP & 0x02 ? 'Z' : ' '
			, he.mP & 0x01 ? 'C' : ' '
			);

		ATDisassembleInsn(buf+strlen(buf), he.mPC, true);

		if (wild && !VDFileWildMatch(wild, buf))
			continue;

		ATConsoleWrite(buf);
	}

	if (predictLen > 4)
		ATConsolePrintf("[%d lines omitted]\n", predictLen - 4);
}

void ATConsoleCmdDumpDsm(int argc, const char *const *argv) {
	if (argc < 3) {
		ATConsoleWrite("Syntax: .dumpdsm <filename> <startaddr> <bytelen>\n");
		return;
	}

	uint16 addr = (uint16)strtoul(argv[1], NULL, 16);
	uint16 len = (uint16)strtoul(argv[2], NULL, 16);

	FILE *f = fopen(argv[0], "w");
	if (!f) {
		ATConsolePrintf("Unable to open file for write: %s\n", argv[0]);
	}
	ATDisassembleRange(f, addr, addr+len);
	fclose(f);

	ATConsolePrintf("Disassembled %04X-%04X to %s\n", addr, addr+len-1, argv[0]);
}

void ATConsoleCmdWriteMem(int argc, const char *const *argv) {
	if (argc < 3) {
		ATConsoleWrite("Syntax: .writemem <filename> <startaddr> <bytelen>\n");
		return;
	}

	uint16 addr = (uint16)strtoul(argv[1], NULL, 16);
	uint16 len = (uint16)strtoul(argv[2], NULL, 16);

	FILE *f = fopen(argv[0], "wb");
	if (!f) {
		ATConsolePrintf("Unable to open file for write: %s\n", argv[0]);
	}

	uint16 a = addr;
	uint16 i = len;
	while(i--)
		putc(g_sim.DebugReadByte(a++), f);

	fclose(f);

	ATConsolePrintf("Wrote %04X-%04X to %s\n", addr, addr+len-1, argv[0]);
}

void ATConsoleCmdAntic() {
	g_sim.GetAntic().DumpStatus();
}

void ATConsoleCmdBank() {
	uint8 portb = g_sim.GetBankRegister();
	ATConsolePrintf("Bank state: %02X\n", portb);

	ATMemoryMode mmode = g_sim.GetMemoryMode();

	if (mmode != kATMemoryMode_48K) {
		ATConsolePrintf("  Kernel ROM:    %s\n", (portb & 0x01) ? "enabled" : "disabled");
		ATConsolePrintf("  BASIC ROM:     %s\n", (portb & 0x02) ? "disabled" : "enabled");
		ATConsolePrintf("  CPU bank:      %s\n", mmode == kATMemoryMode_128K || (portb & 0x10) ? "disabled" : "enabled");
		ATConsolePrintf("  Antic bank:    %s\n", mmode == kATMemoryMode_128K || (portb & 0x20) ? "disabled" : "enabled");
		ATConsolePrintf("  Self test ROM: %s\n", (portb & 0x80) ? "disabled" : "enabled");
	}

	ATConsolePrintf("Antic bank: $%06X\n", g_sim.GetAnticBankBase());
	ATConsolePrintf("CPU bank:   $%06X\n", g_sim.GetCPUBankBase());

	int cartBank = g_sim.GetCartBank();
	ATConsolePrintf("Cartridge bank: $%02X ($%06X)\n", cartBank, cartBank << 13);
}

void ATConsoleCmdTraceCIO(int argc, const char **argv) {
	if (!argc) {
		ATConsolePrintf("CIO call tracing is currently %s.\n", g_debugger.IsCIOTracingEnabled() ? "on" : "off");
		return;
	}

	bool newState = false;
	if (!_stricmp(argv[0], "on")) {
		newState = true;
	} else if (_stricmp(argv[0], "off")) {
		ATConsoleWrite("Syntax: .tracecio on|off\n");
		return;
	}

	g_debugger.SetCIOTracingEnabled(newState);
	ATConsolePrintf("CIO call tracing is now %s.\n", newState ? "on" : "off");
}

void ATConsoleCmdTraceSer(int argc, const char **argv) {
	ATPokeyEmulator& pokey = g_sim.GetPokey();
	if (!argc) {
		ATConsolePrintf("Serial I/O tracing is currently %s.\n", pokey.IsTraceSIOEnabled() ? "on" : "off");
		return;
	}

	bool newState = false;
	if (!_stricmp(argv[0], "on")) {
		newState = true;
	} else if (_stricmp(argv[0], "off")) {
		ATConsoleWrite("Syntax: .traceser on|off\n");
		return;
	}

	pokey.SetTraceSIOEnabled(newState);
	ATConsolePrintf("Serial I/O call tracing is now %s.\n", newState ? "on" : "off");
}

void ATConsoleCmdVectors() {
	ATKernelDatabase kdb(&g_sim.GetCPUMemory());

	ATConsolePrintf("NMI vectors:\n");
	ATConsolePrintf("VDSLST  Display list NMI              %04X\n", (uint16)kdb.VDSLST);
	ATConsolePrintf("VVBLKI  Vertical blank immediate      %04X\n", (uint16)kdb.VVBLKI);
	ATConsolePrintf("VVBLKD  Vertical blank deferred       %04X\n", (uint16)kdb.VVBLKD);
	ATConsolePrintf("\n");
	ATConsolePrintf("IRQ vectors:\n");
	ATConsolePrintf("VIMIRQ  IRQ immediate                 %04X\n", (uint16)kdb.VIMIRQ);
	ATConsolePrintf("VKEYBD  Keyboard                      %04X\n", (uint16)kdb.VKEYBD);
	ATConsolePrintf("VSERIN  Serial I/O receive ready      %04X\n", (uint16)kdb.VSERIN);
	ATConsolePrintf("VSEROR  Serial I/O transmit ready     %04X\n", (uint16)kdb.VSEROR);
	ATConsolePrintf("VSEROC  Serial I/O transmit complete  %04X\n", (uint16)kdb.VSEROC);
	ATConsolePrintf("VPRCED  Serial I/O proceed            %04X\n", (uint16)kdb.VPRCED);
	ATConsolePrintf("VINTER  Serial I/O interrupt          %04X\n", (uint16)kdb.VINTER);
	ATConsolePrintf("VBREAK  Break instruction             %04X\n", (uint16)kdb.VBREAK);
	ATConsolePrintf("VTIMR1  POKEY timer 1                 %04X\n", (uint16)kdb.VTIMR1);
	ATConsolePrintf("VTIMR2  POKEY timer 2                 %04X\n", (uint16)kdb.VTIMR2);
}

void ATConsoleCmdGTIA() {
	g_sim.GetGTIA().DumpStatus();
}

void ATConsoleCmdPokey() {
	g_sim.GetPokey().DumpStatus();
}

void ATConsoleCmdPathReset() {
	g_sim.GetCPU().ResetAllPaths();
}

void ATConsoleCmdPathDump(int argc, const char *const *argv) {
	if (argc < 1) {
		ATConsoleWrite("Syntax: .pathdump <filename>\n");
		return;
	}

	// create symbol table based on paths
	vdrefptr<IATCustomSymbolStore> pSymbolStore;
	ATCreateCustomSymbolStore(~pSymbolStore);

	ATCPUEmulator& cpu = g_sim.GetCPU();
	sint32 addr = -1;
	for(;;) {
		addr = cpu.GetNextPathInstruction(addr);
		if (addr < 0)
			break;

		if (cpu.IsPathStart(addr)) {
			char buf[16];
			sprintf(buf, "L%04X", (uint16)addr);

			pSymbolStore->AddSymbol(addr, buf);
		}
	}

	g_debugger.AddModule(0, 0x10000, pSymbolStore);

	FILE *f = fopen(argv[0], "w");
	if (!f) {
		ATConsolePrintf("Unable to open file for write: %s\n", argv[0]);
	} else {
		sint32 addr = -1;
		char buf[256];
		for(;;) {
			addr = cpu.GetNextPathInstruction(addr);
			if (addr < 0)
				break;

			ATDisassembleInsn(buf, addr, true);
			fputs(buf, f);
		}
	}
	fclose(f);

	g_debugger.RemoveModule(0, 0x10000, pSymbolStore);

	ATConsolePrintf("Paths dumped to %s\n", argv[0]);
}

void ATConsoleCmdLoadKernelSymbols(int argc, const char *const *argv) {
	if (!argc) {
		ATConsoleWrite("Syntax: .loadksym <filename>\n");
		return;
	}

	vdrefptr<IATSymbolStore> symbols;
	if (!ATLoadSymbols(VDTextAToW(argv[0]).c_str(), ~symbols)) {
		ATConsolePrintf("Unable to load symbols: %s\n", argv[0]);
		return;
	}

	g_debugger.AddModule(0xD800, 0x2800, symbols);
	ATConsolePrintf("Kernel symbols loaded: %s\n", argv[0]);
}

void ATConsoleCmdDumpHelp() {
	ATConsoleWrite("t    Trace (step one instruction) (F11)\n");
	ATConsoleWrite("g    Go\n");
	ATConsoleWrite("gr   Go until return (step out)\n");
	ATConsoleWrite("gs   Go until scanline\n");
	ATConsoleWrite("gt   Go with tracing enabled\n");
	ATConsoleWrite("k    Call stack\n");
	ATConsoleWrite("s    Step over\n");
	ATConsoleWrite("bp   Set breakpoint\n");
	ATConsoleWrite("bc   Clear breakpoint(s)\n");
	ATConsoleWrite("ba   Break on memory access\n");
	ATConsoleWrite("bl   List breakpoints\n");
	ATConsoleWrite("u    Unassemble\n");
	ATConsoleWrite("r    Show registers\n");
	ATConsoleWrite("db   Display bytes\n");
	ATConsoleWrite("dw   Display words\n");
	ATConsoleWrite("df   Display decimal float\n");
	ATConsoleWrite("lm   List modules\n");
	ATConsoleWrite(".antic       Display Antic status\n");
	ATConsoleWrite(".bank        Show memory bank state\n");
	ATConsoleWrite(".beam        Show Antic scan position\n");
	ATConsoleWrite(".dumpdlist   Dump Antic display list\n");
	ATConsoleWrite(".dumpdsm     Dump disassembly to file\n");
	ATConsoleWrite(".dlhistory   Show Antic display list execution history\n");
	ATConsoleWrite(".history     Show CPU history\n");
	ATConsoleWrite(".gtia        Display GTIA status\n");
	ATConsoleWrite(".loadksym    Load kernel symbols\n");
	ATConsoleWrite(".pokey       Display POKEY status\n");
	ATConsoleWrite(".restart     Restart emulated system\n");
	ATConsoleWrite(".tracecio    Toggle CIO call tracing\n");
	ATConsoleWrite(".traceser    Toggle serial I/O call tracing\n");
	ATConsoleWrite(".vectors     Display kernel vectors\n");
	ATConsoleWrite(".writemem    Write memory to disk\n");
}

void ATConsoleExecuteCommand(char *s) {
	ATConsoleWrite("Altirra> ");
	ATConsolePrintf("%s\n", s);

	const char *argv[64];
	int argc = 0;

	while(argc < 63) {
		while(*s && *s == ' ')
			++s;

		if (!*s)
			break;

		argv[argc++] = s;

		if (*s == '"') {
			while(*s && *s != '"')
				++s;
		} else {
			while(*s && *s != ' ')
				++s;
		}

		if (!*s)
			break;

		*s++ = 0;
	}

	argv[argc] = NULL;

	if (argc) {
		const char *cmd = argv[0];

		if (!strcmp(cmd, "t")) {
			ATConsoleCmdTrace();
		} else if (!strcmp(cmd, "g")) {
			ATConsoleCmdGo();
		} else if (!strcmp(cmd, "gt")) {
			ATConsoleCmdGoTraced();
		} else if (!strcmp(cmd, "gf")) {
			ATConsoleCmdGoFrameEnd();
		} else if (!strcmp(cmd, "gr")) {
			ATConsoleCmdGoReturn();
		} else if (!strcmp(cmd, "gs")) {
			ATConsoleCmdGoScanline(argc-1, argv+1);
		} else if (!strcmp(cmd, "k")) {
			ATConsoleCmdCallStack();
		} else if (!strcmp(cmd, "s")) {
			ATConsoleCmdStepOver();
		} else if (!strcmp(cmd, "bp")) {
			ATConsoleCmdBreakpt(argc-1, argv+1);
		} else if (!strcmp(cmd, "bc")) {
			ATConsoleCmdBreakptClear(argc-1, argv+1);
		} else if (!strcmp(cmd, "ba")) {
			ATConsoleCmdBreakptAccess(argc-1, argv+1);
		} else if (!strcmp(cmd, "bl")) {
			ATConsoleCmdBreakptList();
		} else if (!strcmp(cmd, "bs")) {
			ATConsoleCmdBreakptSector(argc-1, argv+1);
		} else if (!strcmp(cmd, "u")) {
			ATConsoleCmdUnassemble(argc-1, argv+1);
		} else if (!strcmp(cmd, "r")) {
			ATConsoleCmdRegisters(argc-1, argv+1);
		} else if (!strcmp(cmd, "da")) {
			ATConsoleCmdDumpATASCII(argc-1, argv+1);
		} else if (!strcmp(cmd, "db")) {
			ATConsoleCmdDumpBytes(argc-1, argv+1);
		} else if (!strcmp(cmd, "dw")) {
			ATConsoleCmdDumpWords(argc-1, argv+1);
		} else if (!strcmp(cmd, "df")) {
			ATConsoleCmdDumpFloats(argc-1, argv+1);
		} else if (!strcmp(cmd, "lm")) {
			ATConsoleCmdListModules();
		} else if (!strcmp(cmd, "e")) {
			ATConsoleCmdEnter(argc-1, argv+1);
		} else if (!strcmp(cmd, ".antic")) {
			ATConsoleCmdAntic();
		} else if (!strcmp(cmd, ".bank")) {
			ATConsoleCmdBank();
		} else if (!strcmp(cmd, ".dumpdlist")) {
			ATConsoleCmdDumpDisplayList(argc-1, argv+1);
		} else if (!strcmp(cmd, ".dlhistory")) {
			ATConsoleCmdDumpDLHistory();
		} else if (!strcmp(cmd, ".gtia")) {
			ATConsoleCmdGTIA();
		} else if (!strcmp(cmd, ".pokey")) {
			ATConsoleCmdPokey();
		} else if (!strcmp(cmd, ".restart")) {
			g_sim.ColdReset();
		} else if (!strcmp(cmd, ".beam")) {
			ATAnticEmulator& antic = g_sim.GetAntic();
			ATConsolePrintf("Antic position: %d,%d\n", antic.GetBeamX(), antic.GetBeamY()); 
		} else if (!strcmp(cmd, ".dumpdsm")) {
			ATConsoleCmdDumpDsm(argc-1, argv+1);
		} else if (!strcmp(cmd, ".history")) {
			ATConsoleCmdDumpHistory(argc-1, argv+1);
		} else if (!strcmp(cmd, ".pathreset")) {
			ATConsoleCmdPathReset();
		} else if (!strcmp(cmd, ".pathdump")) {
			ATConsoleCmdPathDump(argc-1, argv+1);
		} else if (!strcmp(cmd, ".loadksym")) {
			ATConsoleCmdLoadKernelSymbols(argc-1, argv+1);
		} else if (!strcmp(cmd, ".tracecio")) {
			ATConsoleCmdTraceCIO(argc-1, argv+1);
		} else if (!strcmp(cmd, ".traceser")) {
			ATConsoleCmdTraceSer(argc-1, argv+1);
		} else if (!strcmp(cmd, ".vectors")) {
			ATConsoleCmdVectors();
		} else if (!strcmp(cmd, ".writemem")) {
			ATConsoleCmdWriteMem(argc-1, argv+1);
		} else if (!strcmp(cmd, "?")) {
			ATConsoleCmdDumpHelp();
		} else {
			ATConsoleWrite("Unrecognized command. ? for help\n");
		}
	}
}
