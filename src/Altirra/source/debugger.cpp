//	Altirra - Atari 800/800XL/5200 emulator
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

#include "stdafx.h"
#include <list>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/error.h>
#include <vd2/system/strutil.h>
#include "console.h"
#include "cpu.h"
#include "simulator.h"
#include "disasm.h"
#include "debugger.h"
#include "decmath.h"
#include "symbols.h"
#include "ksyms.h"
#include "kerneldb.h"
#include "cassette.h"
#include "vbxe.h"
#include "uirender.h"
#include "resource.h"
#include "oshelper.h"

extern ATSimulator g_sim;

void ATSetFullscreen(bool enabled);
bool ATConsoleCheckBreak();
void ATCreateDebuggerCmdAssemble(uint32 address, IATDebuggerActiveCommand **);

///////////////////////////////////////////////////////////////////////////////

class ATDebugger : public IATSimulatorCallback, public IATDebugger, public IATDebuggerSymbolLookup {
public:
	ATDebugger();
	~ATDebugger();

	bool IsRunning() const;

	bool Init();

	void Detach();
	void SetSourceMode(ATDebugSrcMode sourceMode);
	void Break();
	void Run(ATDebugSrcMode sourceMode);
	void RunTraced();
	void ClearAllBreakpoints();
	void ToggleBreakpoint(uint16 addr);
	void StepInto(ATDebugSrcMode sourceMode, uint32 rgnStart = 0, uint32 rgnSize = 0);
	void StepOver(ATDebugSrcMode sourceMode);
	void StepOut(ATDebugSrcMode sourceMode);
	void SetPC(uint16 pc);
	uint16 GetFramePC() const;
	void SetFramePC(uint16 pc);
	uint32 GetCallStack(ATCallStackFrame *dst, uint32 maxCount);
	void DumpCallStack();

	int AddWatch(uint32 address, int length);
	bool ClearWatch(int idx);
	void ClearAllWatches();
	bool GetWatchInfo(int idx, ATDebuggerWatchInfo& info);

	void ListModules();
	void UnloadSymbolsByIndex(uint32 index);

	void DumpCIOParameters();

	bool IsCIOTracingEnabled() const { return mbTraceCIOCalls; }

	void SetCIOTracingEnabled(bool enabled);

	// symbol handling
	uint32 AddModule(uint32 base, uint32 size, IATSymbolStore *symbolStore, const char *name);
	void RemoveModule(uint32 base, uint32 size, IATSymbolStore *symbolStore);

	void AddClient(IATDebuggerClient *client, bool requestUpdate);
	void RemoveClient(IATDebuggerClient *client);
	void RequestClientUpdate(IATDebuggerClient *client);

	uint32 LoadSymbols(const wchar_t *fileName);
	void UnloadSymbols(uint32 moduleId);

	sint32 ResolveSymbol(const char *s, bool allowGlobal = false);
	uint32 ResolveSymbolThrow(const char *s, bool allowGlobal = false);

	void AddCustomSymbol(uint32 address, uint32 len, const char *name, uint32 rwxmode);
	void RemoveCustomSymbol(uint32 address);
	void LoadCustomSymbols(const wchar_t *filename);
	void SaveCustomSymbols(const wchar_t *filename);

	VDStringA GetAddressText(uint32 globalAddr, bool useHexSpecifier, bool addSymbolInfo = false);

	void GetDirtyStorage(vdfastvector<ATDebuggerStorageId>& ids) const;

	void ExecuteCommand(const char *s) {
		if (mActiveCommands.empty())
			return;

		IATDebuggerActiveCommand *cmd = mActiveCommands.back();

		if (!cmd->ProcessSubCommand(s)) {
			cmd->EndCommand();
			cmd->Release();
			mActiveCommands.pop_back();
		}

		if (mActiveCommands.empty())
			SetPromptDefault();
		else
			SetPrompt(mActiveCommands.back()->GetPrompt());
	}

	IATDebuggerActiveCommand *GetActiveCommand() {
		return mActiveCommands.empty() ? NULL : mActiveCommands.back();
	}

	void StartActiveCommand(IATDebuggerActiveCommand *cmd) {
		cmd->AddRef();
		mActiveCommands.push_back(cmd);

		cmd->BeginCommand(this);
		SetPrompt(cmd->GetPrompt());
	}

	void TerminateActiveCommands() {
		while(!mActiveCommands.empty()) {
			IATDebuggerActiveCommand *cmd = mActiveCommands.back();
			mActiveCommands.pop_back();
			cmd->EndCommand();
			cmd->Release();
		}
	}

	void WriteMemoryCPU(uint16 address, const void *data, uint32 len) {
		const uint8 *data8 = (const uint8 *)data;

		while(len--)
			g_sim.GetCPUMemory().CPUWriteByte(address++, *data8++);
	}

	VDEvent<IATDebugger, const char *>& OnPromptChanged() { return mEventPromptChanged; }

	const char *GetPrompt() const {
		return mPrompt.c_str();
	}

	void SetPromptDefault() {
		SetPrompt("Altirra");
	}

	void SetPrompt(const char *prompt) {
		if (mPrompt != prompt) {
			mPrompt = prompt;
			mEventPromptChanged.Raise(this, prompt);
		}
	}

	void SendRegisterUpdate() {
		UpdateClientSystemState();
	}

	enum {
		kModuleId_KernelDB = 1,
		kModuleId_KernelROM,
		kModuleId_Hardware,
		kModuleId_Manual,
		kModuleId_Custom
	};

public:
	bool GetSourceFilePath(uint32 moduleId, uint16 fileId, VDStringW& path);
	bool LookupSymbol(uint32 moduleOffset, uint32 flags, ATSymbol& symbol);
	bool LookupSymbol(uint32 moduleOffset, uint32 flags, ATDebuggerSymbol& symbol);
	bool LookupLine(uint32 addr, uint32& moduleId, ATSourceLineInfo& lineInfo);
	bool LookupFile(const wchar_t *fileName, uint32& moduleId, uint16& fileId);
	void GetLinesForFile(uint32 moduleId, uint16 fileId, vdfastvector<ATSourceLineInfo>& lines);

public:
	void OnSimulatorEvent(ATSimulatorEvent ev);

protected:
	void UpdateClientSystemState(IATDebuggerClient *client = NULL);
	void ActivateSourceWindow();
	void NotifyEvent(ATDebugEvent eventId);

	struct Module {
		uint32	mId;
		uint32	mBase;
		uint32	mSize;
		bool	mbDirty;
		vdrefptr<IATSymbolStore>	mpSymbols;
		VDStringA	mName;
	};

	uint32	mNextModuleId;
	uint16	mFramePC;
	bool mbTraceCIOCalls;
	bool mbSourceMode;

	typedef std::list<Module> Modules; 
	Modules		mModules;

	typedef std::vector<IATDebuggerClient *> Clients;
	Clients mClients;
	int mClientsBusy;
	bool mbClientsChanged;

	uint32	mWatchAddress[8];
	int		mWatchLength[8];

	VDStringA	mPrompt;
	VDEvent<IATDebugger, const char *> mEventPromptChanged;

	typedef vdfastvector<IATDebuggerActiveCommand *> ActiveCommands;
	ActiveCommands mActiveCommands;
};

ATDebugger g_debugger;

IATDebugger *ATGetDebugger() { return &g_debugger; }
IATDebuggerSymbolLookup *ATGetDebuggerSymbolLookup() { return &g_debugger; }

void ATInitDebugger() {
	g_debugger.Init();
}

ATDebugger::ATDebugger()
	: mNextModuleId(kModuleId_Custom)
	, mFramePC(0)
	, mbTraceCIOCalls(false)
	, mbSourceMode(false)
	, mClientsBusy(0)
	, mbClientsChanged(false)
{
	SetPromptDefault();

	for(int i=0; i<8; ++i)
		mWatchLength[i] = 0;
}

ATDebugger::~ATDebugger() {
	TerminateActiveCommands();
}

bool ATDebugger::IsRunning() const {
	return g_sim.IsRunning();
}

bool ATDebugger::Init() {
	g_sim.AddCallback(this);

	return true;
}

void ATDebugger::Detach() {
	TerminateActiveCommands();

	ATCPUEmulator& cpu = g_sim.GetCPU();
	cpu.SetStep(false);
	cpu.SetTrace(false);
	g_sim.Resume();

	ClearAllBreakpoints();
	UpdateClientSystemState();
}

void ATDebugger::SetSourceMode(ATDebugSrcMode sourceMode) {
	switch(sourceMode) {
		case kATDebugSrcMode_Disasm:
			mbSourceMode = false;
			break;

		case kATDebugSrcMode_Source:
			mbSourceMode = true;
			break;

		case kATDebugSrcMode_Same:
			break;
	}
}

void ATDebugger::Break() {
	if (g_sim.IsRunning()) {
		g_sim.Suspend();

		ATCPUEmulator& cpu = g_sim.GetCPU();
		cpu.DumpStatus();

		mFramePC = cpu.GetInsnPC();
		UpdateClientSystemState();

		TerminateActiveCommands();
	}
}

void ATDebugger::Run(ATDebugSrcMode sourceMode) {
	if (g_sim.IsRunning())
		return;

	SetSourceMode(sourceMode);

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

void ATDebugger::StepInto(ATDebugSrcMode sourceMode, uint32 regionStart, uint32 regionSize) {
	if (g_sim.IsRunning())
		return;

	SetSourceMode(sourceMode);

	ATCPUEmulator& cpu = g_sim.GetCPU();

	cpu.SetTrace(false);
	cpu.SetStepRange(regionStart, regionSize);
	g_sim.Resume();
	UpdateClientSystemState();
}

void ATDebugger::StepOver(ATDebugSrcMode sourceMode) {
	if (g_sim.IsRunning())
		return;

	SetSourceMode(sourceMode);

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

void ATDebugger::StepOut(ATDebugSrcMode sourceMode) {
	if (g_sim.IsRunning())
		return;

	SetSourceMode(sourceMode);

	ATCPUEmulator& cpu = g_sim.GetCPU();
	uint8 s = cpu.GetS();
	if (s == 0xFF)
		return StepInto(sourceMode);

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
	mFramePC = pc;
	UpdateClientSystemState();
}

uint16 ATDebugger::GetFramePC() const {
	return mFramePC;
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
		uint8 mK;
	};
}

uint32 ATDebugger::GetCallStack(ATCallStackFrame *dst, uint32 maxCount) {
	const ATCPUEmulator& cpu = g_sim.GetCPU();
	uint8 vS = cpu.GetS();
	uint8 vP = cpu.GetP();
	uint16 vPC = cpu.GetInsnPC();
	uint8 vK = cpu.GetK();

	uint32 seenFlags[2048] = {0};
	std::deque<StackState> q;

	uint32 frameCount = 0;

	bool isC02 = cpu.GetCPUMode() != kATCPUMode_6502;
	bool is816 = cpu.GetCPUMode() == kATCPUMode_65C816;
	for(uint32 i=0; i<maxCount; ++i) {
		dst->mPC = vPC;
		dst->mS = vS;
		dst->mP = vP;
		dst->mK = vK;
		++dst;

		q.clear();

		StackState ss = { vPC, vS, vP, vK };
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

			uint8 opcode = g_sim.DebugExtReadByte(vPC + ((uint32)vK << 16));
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
				ss.mK	= vK;
				ss.mPC = nextPC + (sint16)(sint8)g_sim.DebugExtReadByte((uint16)(vPC + 1) + ((uint32)vK << 16));
				q.push_back(ss);
			} else if (isC02) {
				if (opcode == 0x80) {	// BRA
					nextPC += (sint16)(sint8)g_sim.DebugExtReadByte((uint16)(vPC + 1) + ((uint32)vK << 16));
				} else if (is816) {
					if (opcode == 0x82) {	// BRL
						nextPC += (sint16)(g_sim.DebugExtReadByte((uint16)(vPC + 1) + ((uint32)vK << 16)) + 256*(int)g_sim.DebugExtReadByte((uint16)(vPC + 2) + ((uint32)vK << 16)));
					}
				}
			}

			ss.mS	= vS;
			ss.mP	= vP;
			ss.mK	= vK;
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

int ATDebugger::AddWatch(uint32 address, int length) {
	for(int i=0; i<8; ++i) {
		if (!mWatchLength[i]) {
			mWatchAddress[i] = address;
			mWatchLength[i] = length;
			return i;
		}
	}

	return -1;
}

bool ATDebugger::ClearWatch(int idx) {
	if (idx < 0 || idx > 7)
		return false;

	mWatchLength[idx] = 0;
	return true;
}

void ATDebugger::ClearAllWatches() {
	for(int i=0; i<8; ++i)
		mWatchLength[i] = 0;
}

bool ATDebugger::GetWatchInfo(int idx, ATDebuggerWatchInfo& winfo) {
	if ((unsigned)idx >= 8 || !mWatchLength[idx])
		return false;

	winfo.mAddress = mWatchAddress[idx];
	winfo.mLen = mWatchLength[idx];
	return true;
}

void ATDebugger::ListModules() {
	Modules::iterator it(mModules.begin()), itEnd(mModules.end());
	int index = 1;
	for(; it!=itEnd; ++it, ++index) {
		const Module& mod = *it;

		ATConsolePrintf("%3d) %04x-%04x  %-16s  %s%s\n", index, mod.mBase, mod.mBase + mod.mSize - 1, mod.mpSymbols ? "(symbols loaded)" : "(no symbols)", mod.mName.c_str(), mod.mbDirty ? "*" : "");
	}
}

void ATDebugger::UnloadSymbolsByIndex(uint32 index) {
	if (index >= mModules.size())
		return;

	Modules::iterator it(mModules.begin());
	std::advance(it, index);

	UnloadSymbols(it->mId);
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

				const uint8 aux1 = g_sim.DebugReadByte(iocb + ATKernelSymbols::ICAX1);

				ATConsolePrintf("CIO: IOCB=%u, CMD=$03 (open), AUX1=$%02x, filename=\"%s\"\n", iocbIdx, aux1, fn);
			}
			break;

		case 0x05:
			ATConsolePrintf("CIO: IOCB=%u, CMD=$05 (get record), buffer=$%04x, length=$%04x\n"
				, iocbIdx
				, g_sim.DebugReadWord(iocb + ATKernelSymbols::ICBAL)
				, g_sim.DebugReadWord(iocb + ATKernelSymbols::ICBLL)
				);
			break;

		case 0x07:
			ATConsolePrintf("CIO: IOCB=%u, CMD=$07 (get characters), buffer=$%04x, length=$%04x\n"
				, iocbIdx
				, g_sim.DebugReadWord(iocb + ATKernelSymbols::ICBAL)
				, g_sim.DebugReadWord(iocb + ATKernelSymbols::ICBLL)
				);
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

uint32 ATDebugger::AddModule(uint32 base, uint32 size, IATSymbolStore *symbolStore, const char *name) {
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

	if (name)
		newmod.mName = name;

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
		if (mClientsBusy) {
			*it = NULL;
			mbClientsChanged = true;
		} else {
			*it = mClients.back();
			mClients.pop_back();
		}
	}
}

void ATDebugger::RequestClientUpdate(IATDebuggerClient *client) {
	UpdateClientSystemState(client);
}

uint32 ATDebugger::LoadSymbols(const wchar_t *fileName) {
	vdrefptr<IATSymbolStore> symStore;

	if (!wcscmp(fileName, L"kernel")) {
		UnloadSymbols(kModuleId_KernelROM);

		mModules.push_back(Module());
		Module& kernmod = mModules.back();
		ATCreateDefaultKernelSymbolStore(~kernmod.mpSymbols);
		kernmod.mId = kModuleId_KernelROM;
		kernmod.mBase = kernmod.mpSymbols->GetDefaultBase();
		kernmod.mSize = kernmod.mpSymbols->GetDefaultSize();
		kernmod.mName = "Kernel ROM";
		kernmod.mbDirty = false;

		return kModuleId_KernelROM;
	}

	if (!wcscmp(fileName, L"kerneldb")) {
		UnloadSymbols(kModuleId_KernelDB);

		mModules.push_back(Module());
		Module& varmod = mModules.back();
		if (g_sim.GetHardwareMode() == kATHardwareMode_5200) {
			ATCreateDefaultVariableSymbolStore5200(~varmod.mpSymbols);
			varmod.mName = "Kernel Database (5200)";
		} else {
			ATCreateDefaultVariableSymbolStore(~varmod.mpSymbols);
			varmod.mName = "Kernel Database (800)";
		}

		varmod.mId = kModuleId_KernelDB;
		varmod.mBase = varmod.mpSymbols->GetDefaultBase();
		varmod.mSize = varmod.mpSymbols->GetDefaultSize();
		varmod.mbDirty = false;

		return kModuleId_KernelDB;
	}

	if (!wcscmp(fileName, L"hardware")) {
		UnloadSymbols(kModuleId_Hardware);
		mModules.push_back(Module());
		Module& hwmod = mModules.back();

		if (g_sim.GetHardwareMode() == kATHardwareMode_5200) {
			ATCreateDefault5200HardwareSymbolStore(~hwmod.mpSymbols);
			hwmod.mName = "Hardware (5200)";
		} else {
			ATCreateDefaultHardwareSymbolStore(~hwmod.mpSymbols);
			hwmod.mName = "Hardware (800)";
		}

		hwmod.mId = kModuleId_Hardware;
		hwmod.mBase = hwmod.mpSymbols->GetDefaultBase();
		hwmod.mSize = hwmod.mpSymbols->GetDefaultSize();
		hwmod.mbDirty = false;

		return kModuleId_Hardware;
	}

	if (!ATLoadSymbols(fileName, ~symStore))
		return 0;

	return AddModule(symStore->GetDefaultBase(), symStore->GetDefaultSize(), symStore, VDTextWToA(fileName).c_str());
}

void ATDebugger::UnloadSymbols(uint32 moduleId) {
	Modules::iterator it(mModules.begin()), itEnd(mModules.end());
	for(; it!=itEnd; ++it) {
		const Module& mod = *it;

		if (mod.mId == moduleId) {
			mModules.erase(it);
			NotifyEvent(kATDebugEvent_SymbolsChanged);
			return;
		}
	}
}

sint32 ATDebugger::ResolveSymbol(const char *s, bool allowGlobal) {
	// check for type prefix
	uint32 addressSpace = kATAddressSpace_CPU;
	uint32 addressLimit = 0xffff;

	if (allowGlobal) {
		if (!strncmp(s, "v:", 2)) {
			addressSpace = kATAddressSpace_VBXE;
			addressLimit = 0x7ffff;
			s += 2;
		} else if (!strncmp(s, "n:", 2)) {
			addressSpace = kATAddressSpace_ANTIC;
			s += 2;
		} else if (!strncmp(s, "x:", 2)) {
			addressSpace = kATAddressSpace_PORTB;
			addressLimit = 0xfffff;
			s += 2;
		}
	}

	if (!vdstricmp(s, "pc"))
		return g_sim.GetCPU().GetPC();

	if (s[0] == '$') {
		++s;
	} else if (addressSpace == kATAddressSpace_CPU) {
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

	// check for bank
	if (*t == ':' && addressSpace == kATAddressSpace_CPU) {
		if (result > 0xff)
			return -1;

		s = t+1;
		uint32 offset = strtoul(s, &t, 16);
		if (offset > 0xffff || *t)
			return -1;

		return (result << 16) + offset;
	} else {
		if (result > addressLimit || *t)
			return -1;

		return result + addressSpace;
	}
}

uint32 ATDebugger::ResolveSymbolThrow(const char *s, bool allowGlobal) {
	sint32 v = ResolveSymbol(s, allowGlobal);

	if (v < 0)
		throw MyError("Unable to evaluate: %s", s);

	return (uint32)v;
}

void ATDebugger::AddCustomSymbol(uint32 address, uint32 len, const char *name, uint32 rwxmode) {
	Module *mmod = NULL;

	Modules::iterator it(mModules.begin()), itEnd(mModules.end());
	for(; it!=itEnd; ++it) {
		Module& mod = *it;

		if (mod.mId == kModuleId_Manual) {
			mmod = &mod;
			break;
		}
	}

	if (!mmod) {
		mModules.push_back(Module());
		Module& mod = mModules.back();

		mod.mName = "Manual";
		mod.mId = kModuleId_Manual;
		mod.mBase = 0;
		mod.mSize = 0x10000;

		vdrefptr<IATCustomSymbolStore> p;
		ATCreateCustomSymbolStore(~p);

		mod.mpSymbols = p;
		mod.mbDirty = false;
		mmod = &mod;
	}

	IATCustomSymbolStore *css = static_cast<IATCustomSymbolStore *>(&*mmod->mpSymbols);

	css->AddSymbol(address, name, len, rwxmode);
	mmod->mbDirty = true;

	NotifyEvent(kATDebugEvent_SymbolsChanged);
}

void ATDebugger::RemoveCustomSymbol(uint32 address) {
	Module *mmod = NULL;

	Modules::iterator it(mModules.begin()), itEnd(mModules.end());
	for(; it!=itEnd; ++it) {
		Module& mod = *it;

		if (mod.mId == kModuleId_Manual) {
			mmod = &mod;
			break;
		}
	}

	if (!mmod)
		return;

	IATCustomSymbolStore *css = static_cast<IATCustomSymbolStore *>(&*mmod->mpSymbols);

	css->RemoveSymbol(address);
	mmod->mbDirty = true;

	NotifyEvent(kATDebugEvent_SymbolsChanged);
}

void ATDebugger::LoadCustomSymbols(const wchar_t *filename) {
	UnloadSymbols(kModuleId_Manual);

	vdrefptr<IATSymbolStore> css;
	ATLoadSymbols(filename, ~css);

	mModules.push_back(Module());
	Module& mod = mModules.back();

	mod.mName = "Manual";
	mod.mId = kModuleId_Manual;
	mod.mBase = css->GetDefaultBase();
	mod.mSize = css->GetDefaultSize();
	mod.mpSymbols = css;
	mod.mbDirty = false;

	ATConsolePrintf("%d symbol(s) loaded.\n", css->GetSymbolCount());

	NotifyEvent(kATDebugEvent_SymbolsChanged);
}

void ATDebugger::SaveCustomSymbols(const wchar_t *filename) {
	Module *mmod = NULL;

	Modules::iterator it(mModules.begin()), itEnd(mModules.end());
	for(; it!=itEnd; ++it) {
		Module& mod = *it;

		if (mod.mId == kModuleId_Manual) {
			mmod = &mod;
			break;
		}
	}

	if (!mmod)
		return;

	IATCustomSymbolStore *css = static_cast<IATCustomSymbolStore *>(&*mmod->mpSymbols);

	ATSaveSymbols(filename, css);

	mmod->mbDirty = false;
}

VDStringA ATDebugger::GetAddressText(uint32 globalAddr, bool useHexSpecifier, bool addSymbolInfo) {
	VDStringA s;
	const char *prefix = useHexSpecifier ? "$" : "";

	switch(globalAddr & kATAddressSpaceMask) {
		case kATAddressSpace_CPU:
			if (globalAddr & 0xff0000)
				s.sprintf("%s%02X:%04X", prefix, (globalAddr >> 16) & 0xff, globalAddr & 0xffff);
			else
				s.sprintf("%s%04X", prefix, globalAddr & 0xffff);
			break;
		case kATAddressSpace_VBXE:
			s.sprintf("v:%s%05X", prefix, globalAddr & 0x7ffff);
			break;
		case kATAddressSpace_ANTIC:
			s.sprintf("n:%s%04X", prefix, globalAddr & 0xffff);
			break;
		case kATAddressSpace_PORTB:
			s.sprintf("x:%s%05X", prefix, globalAddr & 0xfffff);
			break;
	}

	if (addSymbolInfo) {
		ATSymbol sym;
		if (LookupSymbol(globalAddr, kATSymbol_Any, sym)) {
			if (sym.mOffset != globalAddr)
				s.append_sprintf(" (%s+%d)", sym.mpName, globalAddr - sym.mOffset);
			else
				s.append_sprintf(" (%s)", sym.mpName);
		}
	}

	return s;
}

void ATDebugger::GetDirtyStorage(vdfastvector<ATDebuggerStorageId>& ids) const {
	Modules::const_iterator it(mModules.begin()), itEnd(mModules.end());
	for(; it!=itEnd; ++it) {
		const Module& mod = *it;

		if (mod.mId == kModuleId_Manual && mod.mbDirty) {
			ids.push_back(kATDebuggerStorageId_CustomSymbols);
		}
	}
}

bool ATDebugger::GetSourceFilePath(uint32 moduleId, uint16 fileId, VDStringW& path) {
	Modules::const_iterator it(mModules.begin()), itEnd(mModules.end());
	for(; it!=itEnd; ++it) {
		const Module& mod = *it;

		if (mod.mId == moduleId) {
			if (!mod.mpSymbols)
				return false;

			const wchar_t *s = mod.mpSymbols->GetFileName(fileId);
			if (!s)
				return false;

			path = s;
			return true;
		}
	}

	return false;
}

bool ATDebugger::LookupSymbol(uint32 addr, uint32 flags, ATSymbol& symbol) {
	ATDebuggerSymbol symbol2;

	if (!LookupSymbol(addr, flags, symbol2))
		return false;

	symbol = symbol2.mSymbol;
	return true;
}

bool ATDebugger::LookupSymbol(uint32 addr, uint32 flags, ATDebuggerSymbol& symbol) {
	Modules::const_iterator it(mModules.begin()), itEnd(mModules.end());
	int bestDelta = INT_MAX;
	bool valid = false;

	for(; it!=itEnd; ++it) {
		const Module& mod = *it;
		uint32 offset = addr - mod.mBase;

		if (offset < mod.mSize && mod.mpSymbols) {
			ATDebuggerSymbol tempSymbol;
			if (mod.mpSymbols->LookupSymbol(offset, flags, tempSymbol.mSymbol)) {
				tempSymbol.mSymbol.mOffset += mod.mBase;
				tempSymbol.mModuleId = mod.mId;

				int delta = (int)tempSymbol.mSymbol.mOffset - (int)addr;

				if (bestDelta > delta) {
					bestDelta = delta;
					symbol = tempSymbol;
					valid = true;
				}
			}
		}
	}

	return valid;
}

bool ATDebugger::LookupLine(uint32 addr, uint32& moduleId, ATSourceLineInfo& lineInfo) {
	Modules::const_iterator it(mModules.begin()), itEnd(mModules.end());
	uint32 bestOffset = 0xFFFFFFFFUL;
	bool valid = false;

	for(; it!=itEnd; ++it) {
		const Module& mod = *it;
		uint32 offset = addr - mod.mBase;

		if (offset < mod.mSize && mod.mpSymbols) {
			if (mod.mpSymbols->GetLineForOffset(offset, lineInfo)) {
				uint32 lineOffset = lineInfo.mOffset - offset;

				if (bestOffset > lineOffset) {
					bestOffset = lineOffset;

					moduleId = mod.mId;
					valid = true;
				}
			}
		}
	}

	return valid;
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
	if (ev == kATSimEvent_FrameTick) {
		IATUIRenderer *r = g_sim.GetUIRenderer();

		if (r) {
			for(int i=0; i<8; ++i) {
				switch(mWatchLength[i]) {
					case 2:
						r->SetWatchedValue(i, g_sim.DebugReadWord(mWatchAddress[i]), 2);
						break;
					case 1:
						r->SetWatchedValue(i, g_sim.DebugReadByte(mWatchAddress[i]), 1);
						break;
					default:
						r->SetWatchedValue(i, 0, 0);
						break;
				}
			}
		}

		return;
	}

	if (ev == kATSimEvent_ColdReset) {
		LoadSymbols(L"kernel");
		LoadSymbols(L"kerneldb");
		LoadSymbols(L"hardware");
		return;
	}

	if (ev == kATSimEvent_CPUPCBreakpointsUpdated)
		NotifyEvent(kATDebugEvent_BreakpointsChanged);

	ATCPUEmulator& cpu = g_sim.GetCPU();

	if (ev == kATSimEvent_CPUPCBreakpoint) {
		if (mbTraceCIOCalls && cpu.GetPC() == ATKernelSymbols::CIOV) {
			DumpCIOParameters();
			Run(kATDebugSrcMode_Same);
			return;
		}
	}

	ATSetFullscreen(false);
	ATOpenConsole();

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

		case kATSimEvent_VerifierFailure:
			cpu.DumpStatus();
			g_sim.Suspend();
			break;
	}

	cpu.SetRTSBreak();

	if (ev != kATSimEvent_CPUPCBreakpointsUpdated)
		mFramePC = cpu.GetInsnPC();

	UpdateClientSystemState();

	if (mbSourceMode)
		ActivateSourceWindow();
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
	sysstate.mD = cpu.GetD();

	sysstate.mPCModuleId = 0;
	sysstate.mPCFileId = 0;
	sysstate.mPCLine = 0;
	sysstate.mFramePC = mFramePC;
	sysstate.mbRunning = g_sim.IsRunning();
	sysstate.mbEmulation = cpu.GetEmulationFlag();

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

void ATDebugger::ActivateSourceWindow() {
	uint32 moduleId;
	ATSourceLineInfo lineInfo;
	IATDebuggerSymbolLookup *lookup = ATGetDebuggerSymbolLookup();
	if (lookup->LookupLine(mFramePC, moduleId, lineInfo)) {
		VDStringW path;
		if (lookup->GetSourceFilePath(moduleId, lineInfo.mFileId, path) && lineInfo.mLine) {
			IATSourceWindow *w = ATOpenSourceWindow(path.c_str());
			if (w) {
				w->ActivateLine(lineInfo.mLine - 1);
			}
		}
	}
}

void ATDebugger::NotifyEvent(ATDebugEvent eventId) {
	VDVERIFY(++mClientsBusy < 100);

	// Note that this list may change on the fly.
	size_t n = mClients.size();
	for(uint32 i=0; i<n; ++i) {
		IATDebuggerClient *cb = mClients[i];

		if (cb)
			cb->OnDebuggerEvent(eventId);
	}

	VDVERIFY(--mClientsBusy >= 0);

	if (!mClientsBusy && mbClientsChanged) {
		Clients::iterator src = mClients.begin();
		Clients::iterator dst = src;
		Clients::iterator end = mClients.end();

		for(; src != end; ++src) {
			IATDebuggerClient *cb = *src;

			if (cb) {
				*dst = cb;
				++dst;
			}
		}

		if (dst != end)
			mClients.erase(dst, end);

		mbClientsChanged = false;
	}
}

///////////////////////////////////////////////////////////////////////////////

namespace {
	class ATDebuggerCmdSwitch {
	public:
		ATDebuggerCmdSwitch(const char *name, bool defaultState)
			: mpName(name)
			, mbState(defaultState)
		{
		}

		operator bool() const { return mbState; }

	protected:
		friend class ATDebuggerCmdParser;

		const char *mpName;
		bool mbState;
	};

	class ATDebuggerCmdSwitchNumArg {
	public:
		ATDebuggerCmdSwitchNumArg(const char *name, sint32 minVal, sint32 maxVal, sint32 defaultValue = 0)
			: mpName(name)
			, mValue(defaultValue)
			, mMinVal(minVal)
			, mMaxVal(maxVal)
			, mbValid(false)
		{
		}

		bool IsValid() const { return mbValid; }
		sint32 GetValue() const { return mValue; }

	protected:
		friend class ATDebuggerCmdParser;

		const char *mpName;
		sint32 mValue;
		sint32 mMinVal;
		sint32 mMaxVal;
		bool mbValid;
	};

	class ATDebuggerCmdBool{
	public:
		ATDebuggerCmdBool(bool required, bool defaultValue = false)
			: mbRequired(required)
			, mbValid(false)
			, mbValue(defaultValue)
		{
		}

		bool IsValid() const { return mbValid; }
		operator bool() const { return mbValue; }

	protected:
		friend class ATDebuggerCmdParser;

		bool mbRequired;
		bool mbValid;
		bool mbValue;
	};

	class ATDebuggerCmdNumber{
	public:
		ATDebuggerCmdNumber(bool required, sint32 minVal, sint32 maxVal, sint32 defaultValue = 0)
			: mbRequired(required)
			, mbValid(false)
			, mValue(defaultValue)
			, mMinVal(minVal)
			, mMaxVal(maxVal)
		{
		}

		bool IsValid() const { return mbValid; }
		sint32 GetValue() const { return mValue; }

	protected:
		friend class ATDebuggerCmdParser;

		bool mbRequired;
		bool mbValid;
		sint32 mValue;
		sint32 mMinVal;
		sint32 mMaxVal;
	};

	class ATDebuggerCmdAddress {
	public:
		ATDebuggerCmdAddress(bool general, bool required)
			: mAddress(0)
			, mbGeneral(general)
			, mbRequired(required)
			, mbValid(false)
		{
		}

		bool IsValid() const { return mbValid; }

		operator uint32() const { return mAddress; }

	protected:
		friend class ATDebuggerCmdParser;

		uint32 mAddress;
		bool mbGeneral;
		bool mbRequired;
		bool mbValid;
	};

	class ATDebuggerCmdLength {
	public:
		ATDebuggerCmdLength(uint32 defaultLen, bool required)
			: mLength(defaultLen)
			, mbRequired(required)
			, mbValid(false)
		{
		}

		bool IsValid() const { return mbValid; }

		operator uint32() const { return mLength; }

	protected:
		friend class ATDebuggerCmdParser;

		uint32 mLength;
		bool mbRequired;
		bool mbValid;
	};

	class ATDebuggerCmdName {
	public:
		ATDebuggerCmdName(bool required)
			: mbValid(false)
			, mbRequired(required)
		{
		}

		bool IsValid() const { return mbValid; }

		const VDStringA *operator->() const { return &mName; }

	protected:
		friend class ATDebuggerCmdParser;

		VDStringA mName;
		bool mbRequired;
		bool mbValid;
	};

	class ATDebuggerCmdParser {
	public:
		ATDebuggerCmdParser(int argc, const char *const *argv);

		ATDebuggerCmdParser& operator>>(ATDebuggerCmdSwitch& sw);
		ATDebuggerCmdParser& operator>>(ATDebuggerCmdSwitchNumArg& sw);
		ATDebuggerCmdParser& operator>>(ATDebuggerCmdBool& bo);
		ATDebuggerCmdParser& operator>>(ATDebuggerCmdNumber& nu);
		ATDebuggerCmdParser& operator>>(ATDebuggerCmdAddress& ad);
		ATDebuggerCmdParser& operator>>(ATDebuggerCmdLength& ln);
		ATDebuggerCmdParser& operator>>(ATDebuggerCmdName& nm);
		ATDebuggerCmdParser& operator>>(int);

		template<class T>
		ATDebuggerCmdParser& operator,(T& dst) {
			operator>>(dst);
			return *this;
		}

	protected:
		typedef vdfastvector<const char *> Args;

		Args mArgs;
	};

	ATDebuggerCmdParser::ATDebuggerCmdParser(int argc, const char *const *argv)
		: mArgs(argv, argv + argc)
	{
	}

	ATDebuggerCmdParser& ATDebuggerCmdParser::operator>>(ATDebuggerCmdSwitch& sw) {
		for(Args::iterator it(mArgs.begin()), itEnd(mArgs.end()); it != itEnd; ++it) {
			const char *s = *it;

			if (s[0] == '-' && !strcmp(s + 1, sw.mpName)) {
				sw.mbState = true;
				mArgs.erase(it);
				break;
			}
		}

		return *this;
	}

	ATDebuggerCmdParser& ATDebuggerCmdParser::operator>>(ATDebuggerCmdSwitchNumArg& sw) {
		size_t nameLen = strlen(sw.mpName);

		for(Args::iterator it(mArgs.begin()), itEnd(mArgs.end()); it != itEnd; ++it) {
			const char *s = *it;

			if (s[0] != '-')
				continue;

			if (strncmp(s + 1, sw.mpName, nameLen))
				continue;

			s += nameLen + 1;
			if (*s == ':')
				++s;
			else if (*s)
				continue;
			else {
				it = mArgs.erase(it);

				if (it == mArgs.end())
					throw MyError("Switch -%s requires a numeric argument.", sw.mpName);

				s = *it;
			}

			mArgs.erase(it);

			char *end = (char *)s;
			long v = strtol(s, &end, 10);

			if (*end)
				throw MyError("Invalid numeric switch argument: -%s:%s", sw.mpName, s);

			if (v < sw.mMinVal || v > sw.mMaxVal)
				throw MyError("Numeric switch argument out of range: -%s:%d", sw.mpName, v);

			sw.mbValid = true;
			sw.mValue = v;
			break;
		}

		return *this;
	}

	ATDebuggerCmdParser& ATDebuggerCmdParser::operator>>(ATDebuggerCmdBool& bo) {
		if (mArgs.empty()) {
			if (bo.mbRequired)
				throw MyError("Missing boolean argument.");

			return *this;
		}

		const VDStringSpanA s(mArgs.front());
		mArgs.erase(mArgs.begin());

		bo.mbValid = true;

		if (s == "on" || s == "true")
			bo.mbValue = true;
		else if (s == "off" || s == "false")
			bo.mbValue = false;

		return *this;
	}

	ATDebuggerCmdParser& ATDebuggerCmdParser::operator>>(ATDebuggerCmdNumber& nu) {
		if (mArgs.empty()) {
			if (nu.mbRequired)
				throw MyError("Missing numeric argument.");

			return *this;
		}

		const char *s = mArgs.front();
		mArgs.erase(mArgs.begin());

		char *end = (char *)s;
		long v = strtol(s, &end, 10);

		if (*end)
			throw MyError("Invalid numeric argument: %s", s);

		if (v < nu.mMinVal || v > nu.mMaxVal)
			throw MyError("Numeric argument out of range: %d", v);

		nu.mbValid = true;
		nu.mValue = v;

		return *this;
	}

	ATDebuggerCmdParser& ATDebuggerCmdParser::operator>>(ATDebuggerCmdAddress& ad) {
		for(Args::iterator it(mArgs.begin()), itEnd(mArgs.end()); it != itEnd; ++it) {
			const char *s = *it;

			if (s[0] != '-') {
				ad.mAddress = g_debugger.ResolveSymbolThrow(s, ad.mbGeneral);
				ad.mbValid = true;

				mArgs.erase(it);
				return *this;
			}
		}

		if (ad.mbRequired)
			throw MyError("Address parameter required.");

		return *this;
	}

	ATDebuggerCmdParser& ATDebuggerCmdParser::operator>>(ATDebuggerCmdLength& ln) {
		for(Args::iterator it(mArgs.begin()), itEnd(mArgs.end()); it != itEnd; ++it) {
			const char *s = *it;

			if (s[0] == 'L' || s[0] == 'l') {
				char *t = (char *)s;
				uint32 len = strtoul(s+1, &t, 16);

				if (s == t || *t)
					throw MyError("Invalid length: %s", s);

				ln.mLength = len;
				ln.mbValid = true;

				mArgs.erase(it);
				return *this;
			}
		}

		if (ln.mbRequired)
			throw MyError("Length parameter required.");

		return *this;
	}

	ATDebuggerCmdParser& ATDebuggerCmdParser::operator>>(ATDebuggerCmdName& nm) {
		for(Args::iterator it(mArgs.begin()), itEnd(mArgs.end()); it != itEnd; ++it) {
			const char *s = *it;

			if (s[0] != '-') {
				nm.mName = s;
				nm.mbValid = true;

				mArgs.erase(it);
				return *this;
			}
		}

		if (nm.mbRequired)
			throw MyError("Name parameter required.");

		return *this;
	}

	ATDebuggerCmdParser& ATDebuggerCmdParser::operator>>(int) {
		if (!mArgs.empty())
			throw MyError("Extraneous argument: %s", mArgs.front());

		return *this;
	}
}

///////////////////////////////////////////////////////////////////////////////

void ATConsoleCmdAssemble(int argc, const char *const *argv) {
	ATDebuggerCmdAddress addr(false, true);

	ATDebuggerCmdParser(argc, argv) >> addr;

	vdrefptr<IATDebuggerActiveCommand> cmd;

	ATCreateDebuggerCmdAssemble(addr, ~cmd);
	g_debugger.StartActiveCommand(cmd);
}

void ATConsoleCmdTrace() {
	ATGetDebugger()->StepInto(kATDebugSrcMode_Disasm);
}

void ATConsoleCmdGo() {
	ATGetDebugger()->Run(kATDebugSrcMode_Disasm);
}

void ATConsoleCmdGoTraced() {
	ATGetDebugger()->RunTraced();
}

void ATConsoleCmdGoFrameEnd() {
	g_sim.SetBreakOnFrameEnd(true);
	g_sim.Resume();
}

void ATConsoleCmdGoReturn() {
	ATGetDebugger()->StepOut(kATDebugSrcMode_Disasm);
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
	ATGetDebugger()->StepOver(kATDebugSrcMode_Disasm);
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
		throw MyError("Usage: bc <address> | *");

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
	ATDebuggerCmdAddress address(false, false);
	ATDebuggerCmdLength length(20, false);

	ATDebuggerCmdParser(argc, argv) >> address, length;

	ATCPUEmulator& cpu = g_sim.GetCPU();
	uint32 addr;
	
	if (address.IsValid())
		addr = address;
	else
		addr = cpu.GetInsnPC() + ((uint32)cpu.GetK() << 16);

	uint8 bank = (uint8)(addr >> 16);
	uint32 n = length;
	for(uint32 i=0; i<n; ++i) {
		addr = ATDisassembleInsn(addr, bank);

		if ((i & 15) == 15 && ATConsoleCheckBreak())
			break;
	}
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

	uint16 v = (uint16)g_debugger.ResolveSymbolThrow(argv[1]);

	VDStringSpanA regName(argv[0]);
	if (regName == "pc") {
		g_debugger.SetPC(v);
	} else if (regName == "x") {
		cpu.SetX((uint8)v);
	} else if (regName == "y") {
		cpu.SetY((uint8)v);
	} else if (regName == "s") {
		cpu.SetS((uint8)v);
	} else if (regName == "p") {
		cpu.SetP((uint8)v);
	} else if (regName == "a") {
		cpu.SetA((uint8)v);
	} else {
		ATConsolePrintf("Unknown register '%s'\n", argv[0]);
		return;
	}

	g_debugger.SendRegisterUpdate();
}

void ATConsoleCmdDumpATASCII(int argc, const char *const *argv) {
	ATDebuggerCmdAddress address(true, true);
	ATDebuggerCmdLength length(128, false);

	ATDebuggerCmdParser(argc, argv) >> address, length;

	uint32 addr = (uint32)address;
	uint32 atype = addr & kATAddressSpaceMask;
	uint32 n = length;

	if (n > 128)
		n = 128;

	char str[129];
	uint32 idx = 0;

	while(idx < n) {
		uint8 c = g_sim.DebugGlobalReadByte(atype + ((addr + idx) & kATAddressOffsetMask));

		if (c < 0x20 || c >= 0x7f) {
			if (!length.IsValid())
				break;

			c = '.';
		}

		str[idx++] = c;
	}

	str[idx] = 0;

	ATConsolePrintf("%s: \"%s\"\n", g_debugger.GetAddressText(addr, false).c_str(), str);
}

void ATConsoleCmdDumpINTERNAL(int argc, const char *const *argv) {
	ATDebuggerCmdAddress address(true, true);
	ATDebuggerCmdLength length(128, false);

	ATDebuggerCmdParser(argc, argv) >> address, length;

	uint32 addr = (uint32)address;
	uint32 atype = addr & kATAddressSpaceMask;
	uint32 n = length;

	if (n > 128)
		n = 128;

	char str[129];
	uint32 idx = 0;

	while(idx < n) {
		uint8 c = g_sim.DebugGlobalReadByte(atype + ((addr + idx) & kATAddressOffsetMask));

		static const uint8 kXlat[4]={ 0x20, 0x60, 0x40, 0x00 };

		c ^= kXlat[(c >> 5) & 3];

		if (c < 0x20 || c >= 0x7f) {
			if (!length.IsValid())
				break;

			c = '.';
		}

		str[idx++] = c;
	}

	str[idx] = 0;

	ATConsolePrintf("%s: \"%s\"\n", g_debugger.GetAddressText(addr, false).c_str(), str);
}

void ATConsoleCmdDumpBytes(int argc, const char *const *argv) {
	ATDebuggerCmdAddress address(true, true);
	ATDebuggerCmdLength length(128, false);

	ATDebuggerCmdParser(argc, argv) >> address, length;

	uint32 addr = (uint32)address;
	uint32 atype = addr & kATAddressSpaceMask;

	uint8 buf[16];
	char chbuf[17];

	chbuf[16] = 0;

	uint32 rows = (length + 15) >> 4;

	while(rows--) {
		if (15 == (rows & 15) && ATConsoleCheckBreak())
			break;

		for(int i=0; i<16; ++i) {
			uint8 v = g_sim.DebugGlobalReadByte(atype + ((addr + i) & kATAddressOffsetMask));
			buf[i] = v;

			if ((uint8)(v - 0x20) < 0x5F)
				chbuf[i] = (char)v;
			else
				chbuf[i] = '.';
		}

		ATConsolePrintf("%s: %02X %02X %02X %02X %02X %02X %02X %02X-%02X %02X %02X %02X %02X %02X %02X %02X |%s|\n"
			, g_debugger.GetAddressText(addr, false).c_str()
			, buf[ 0], buf[ 1], buf[ 2], buf[ 3]
			, buf[ 4], buf[ 5], buf[ 6], buf[ 7]
			, buf[ 8], buf[ 9], buf[10], buf[11]
			, buf[12], buf[13], buf[14], buf[15]
			, chbuf
			);

		addr += 16;
	}
}

void ATConsoleCmdDumpWords(int argc, const char *const *argv) {
	ATDebuggerCmdAddress address(true, true);
	ATDebuggerCmdLength length(64, false);

	ATDebuggerCmdParser(argc, argv) >> address, length;

	uint32 addr = (uint32)address;
	uint32 atype = addr & kATAddressSpaceMask;

	uint32 rows = (length + 7) >> 3;

	uint8 buf[16];

	while(rows--) {
		if (15 == (rows & 15) && ATConsoleCheckBreak())
			break;

		for(int i=0; i<16; ++i) {
			uint8 v = g_sim.DebugGlobalReadByte(atype + ((addr + i) & kATAddressOffsetMask));
			buf[i] = v;
		}

		ATConsolePrintf("%s: %04X %04X %04X %04X-%04X %04X %04X %04X\n"
			, g_debugger.GetAddressText(addr, false).c_str()
			, buf[0] + 256*buf[1]
			, buf[2] + 256*buf[3]
			, buf[4] + 256*buf[5]
			, buf[6] + 256*buf[7]
			, buf[8] + 256*buf[9]
			, buf[10] + 256*buf[11]
			, buf[12] + 256*buf[13]
			, buf[14] + 256*buf[15]);

		addr += 16;
	}
}

void ATConsoleCmdDumpFloats(int argc, const char *const *argv) {
	ATDebuggerCmdAddress address(true, true);
	ATDebuggerCmdLength length(1, false);

	ATDebuggerCmdParser(argc, argv) >> address, length;

	uint32 addr = (uint32)address;
	uint32 atype = addr & kATAddressSpaceMask;
	uint8 data[6];

	uint32 rows = length;
	while(rows--) {
		if (15 == (rows & 15) && ATConsoleCheckBreak())
			break;

		for(int i=0; i<6; ++i) {
			data[i] = g_sim.DebugGlobalReadByte(atype + ((addr + i) & kATAddressOffsetMask));
		}

		ATConsolePrintf("%s: %02X %02X %02X %02X %02X %02X  %g\n"
			, g_debugger.GetAddressText(addr, false).c_str()
			, data[0]
			, data[1]
			, data[2]
			, data[3]
			, data[4]
			, data[5]
			, ATReadDecFloatAsBinary(data));

		addr += 6;
	}
}

void ATConsoleCmdListModules() {
	g_debugger.ListModules();
}

void ATConsoleCmdListNearestSymbol(int argc, const char *const *argv) {
	if (!argc)
		return;

	sint32 v = g_debugger.ResolveSymbol(argv[0]);
	if (v < 0) {
		ATConsolePrintf("Unable to resolve symbol: %s\n", argv[0]);
		return;
	}

	uint16 addr = (uint16)v;

	ATDebuggerSymbol sym;
	if (g_debugger.LookupSymbol(addr, kATSymbol_Any, sym)) {
		VDStringW sourceFile;
		ATSourceLineInfo lineInfo;
		uint32 moduleId;

		if (g_debugger.LookupLine(addr, moduleId, lineInfo) &&
			g_debugger.GetSourceFilePath(moduleId, lineInfo.mFileId, sourceFile))
		{
			ATConsolePrintf("%04X = %s + %d [%ls:%d]\n", addr, sym.mSymbol.mpName, (int)addr - (int)sym.mSymbol.mOffset, sourceFile.c_str(), lineInfo.mLine);
		} else {
			ATConsolePrintf("%04X = %s + %d\n", addr, sym.mSymbol.mpName, (int)addr - (int)sym.mSymbol.mOffset);
		}
	} else {
		ATConsolePrintf("No symbol found for address: %04X\n", addr);
	}
}

void ATConsoleCmdWatchByte(int argc, const char *const *argv) {
	if (!argc)
		return;

	sint32 v = g_debugger.ResolveSymbol(argv[0]);
	if (v < 0) {
		ATConsolePrintf("Unable to resolve symbol: %s\n", argv[0]);
		return;
	}

	int idx = g_debugger.AddWatch(v, 1);

	if (idx >= 0)
		ATConsolePrintf("Watch entry %d set.\n", idx);
	else
		ATConsoleWrite("No free watch slots available.\n");
}

void ATConsoleCmdWatchWord(int argc, const char *const *argv) {
	if (!argc)
		return;

	sint32 v = g_debugger.ResolveSymbol(argv[0]);
	if (v < 0) {
		ATConsolePrintf("Unable to resolve symbol: %s\n", argv[0]);
		return;
	}

	int idx = g_debugger.AddWatch(v, 2);

	if (idx >= 0)
		ATConsolePrintf("Watch entry %d set.\n", idx);
	else
		ATConsoleWrite("No free watch slots available.\n");
}

void ATConsoleCmdWatchClear(int argc, const char *const *argv) {
	if (!argc)
		return;

	if (!strcmp(argv[0], "*")) {
		g_debugger.ClearAllWatches();
		ATConsoleWrite("All watch entries cleared.\n");
		return;
	}

	char *s;
	int idx = strtol(argv[0], &s, 10);
	if (s == argv[0] || *s || !g_debugger.ClearWatch(idx)) {
		ATConsolePrintf("Invalid watch index: %s\n", argv[0]);
		return;
	}

	ATConsolePrintf("Watch entry %d cleared.\n", idx);
}

void ATConsoleCmdWatchList(int argc, const char *const *argv) {
	ATConsoleWrite("#  Len Address\n");

	for(int i=0; i<8; ++i) {
		ATDebuggerWatchInfo winfo;
		if (g_debugger.GetWatchInfo(i, winfo))
			ATConsolePrintf("%d  %2d  %s\n", i, winfo.mLen, g_debugger.GetAddressText(winfo.mAddress, false, true).c_str());
	}
}

void ATConsoleCmdSymbolAdd(int argc, const char *const *argv) {
	ATDebuggerCmdName name(true);
	ATDebuggerCmdAddress addr(false, true);
	ATDebuggerCmdLength len(1, false);

	ATDebuggerCmdParser(argc, argv) >> name, addr, len;

	VDStringA s(name->c_str());

	for(VDStringA::iterator it(s.begin()), itEnd(s.end()); it != itEnd; ++it)
		*it = toupper((unsigned char)*it);

	g_debugger.AddCustomSymbol(addr, len, s.c_str(), kATSymbol_Any);
}

void ATConsoleCmdSymbolClear(int argc, const char *const *argv) {
	g_debugger.UnloadSymbols(ATDebugger::kModuleId_Manual);
	ATConsoleWrite("Custom symbols cleared.\n");
}

void ATConsoleCmdSymbolRemove(int argc, const char *const *argv) {
	ATDebuggerCmdAddress addr(false, true);

	ATDebuggerCmdParser(argc, argv) >> addr;

	g_debugger.RemoveCustomSymbol(addr);
}

void ATConsoleCmdSymbolRead(int argc, const char *const *argv) {
	ATDebuggerCmdName name(true);

	ATDebuggerCmdParser(argc, argv) >> name;

	g_debugger.LoadCustomSymbols(VDTextAToW(name->c_str()).c_str());
}

void ATConsoleCmdSymbolWrite(int argc, const char *const *argv) {
	ATDebuggerCmdName name(true);

	ATDebuggerCmdParser(argc, argv) >> name;

	g_debugger.SaveCustomSymbols(VDTextAToW(name->c_str()).c_str());
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

		uint32 addr = (uint32)v;

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
			uint32 ea = addr + (i - 1);

			if (addr & kATAddressSpaceMask)
				g_sim.DebugGlobalWriteByte(ea, data[i - 1]);
			else
				mem.ExtWriteByte((uint16)ea, (uint8)(ea >> 16), data[i - 1]);
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
	ATDebuggerCmdSwitch switchI("i", false);
	ATDebuggerCmdSwitch switchC("c", false);
	ATDebuggerCmdSwitchNumArg switchS("s", 0, 0x7FFFFFFF);
	ATDebuggerCmdNumber histLenArg(false, 0, 0x7FFFFFFF);
	ATDebuggerCmdName wildArg(false);

	ATDebuggerCmdParser(argc, argv) >> switchI >> switchC >> switchS >> histLenArg >> wildArg;

	int histlen = 32;
	const char *wild = NULL;
	bool compressed = switchC;
	bool interruptsOnly = switchI;
	int histstart = -1;

	if (switchS.IsValid()) {
		compressed = true;
		histstart = switchS.GetValue();
	}

	if (histLenArg.IsValid()) {
		histlen = histLenArg.GetValue();

		if (wildArg.IsValid())
			wild = wildArg->c_str();
	}

	const ATCPUEmulator& cpu = g_sim.GetCPU();
	uint16 predictor[4] = {0,0,0,0};
	int predictLen = 0;

	if (histstart < 0)
		histstart = histlen - 1;

	int histend = histstart - histlen + 1;
	if (histend < 0)
		histend = 0;

	uint16 nmi = g_sim.DebugReadWord(0xFFFA);
	uint16 irq = g_sim.DebugReadWord(0xFFFE);

	VDStringA buf;
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

		buf.sprintf("%7d) T=%05d|%3d,%3d A=%02x X=%02x Y=%02x S=%02x P=%02x (%c%c%c%c%c%c%c%c) "
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

		ATDisassembleInsn(buf, he, true, false, true, true, true);

		if (wild && !VDFileWildMatch(wild, buf.c_str()))
			continue;

		buf += '\n';

		ATConsoleWrite(buf.c_str());
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

	uint32 addr = strtoul(argv[1], NULL, 16);
	uint32 len = strtoul(argv[2], NULL, 16);

	if (addr >= 0x10000)
		throw MyError("Invalid start address: %s\n", argv[1]);

	if (addr + len > 0x10000)
		len = 0x10000 - addr;

	FILE *f = fopen(argv[0], "wb");
	if (!f) {
		ATConsolePrintf("Unable to open file for write: %s\n", argv[0]);
		return;
	}

	uint16 a = addr;
	uint32 i = len;

	while(i--)
		putc(g_sim.DebugReadByte(a++), f);

	fclose(f);

	ATConsolePrintf("Wrote %04X-%04X to %s\n", addr, addr+len-1, argv[0]);
}

void ATConsoleCmdLoadSymbols(int argc, const char *const *argv) {
	if (argc > 0) {
		uint32 idx = ATGetDebugger()->LoadSymbols(VDTextAToW(argv[0]).c_str());

		if (idx)
			ATConsolePrintf("Loaded symbol file %s.\n", argv[0]);
	}
}

void ATConsoleCmdUnloadSymbols(int argc, const char *const *argv) {
	if (argc != 1)
		throw MyError("Syntax: .unloadsym index");

	const char *s = argv[0];
	char *t = (char *)s;
	unsigned long index;
	
	if (!strcmp(s, "kernel"))
		g_debugger.UnloadSymbols(ATDebugger::kModuleId_KernelROM);
	else if (!strcmp(s, "kerneldb"))
		g_debugger.UnloadSymbols(ATDebugger::kModuleId_KernelDB);
	else if (!strcmp(s, "hardware"))
		g_debugger.UnloadSymbols(ATDebugger::kModuleId_Hardware);
	else if (!strcmp(s, "manual"))
		g_debugger.UnloadSymbols(ATDebugger::kModuleId_Manual);
	else {
		index = strtoul(s, &t, 0);

		if (*t)
			throw MyError("Invalid index: %s\n", s);

		g_debugger.UnloadSymbolsByIndex(index);
	}
}

void ATConsoleCmdAntic() {
	g_sim.GetAntic().DumpStatus();
}

void ATConsoleCmdBank() {
	uint8 portb = g_sim.GetBankRegister();
	ATConsolePrintf("Bank state: %02X\n", portb);

	ATMemoryMode mmode = g_sim.GetMemoryMode();

	if (mmode != kATMemoryMode_48K && mmode != kATMemoryMode_52K) {
		ATConsolePrintf("  Kernel ROM:    %s\n", (portb & 0x01) ? "enabled" : "disabled");
		ATConsolePrintf("  BASIC ROM:     %s\n", (portb & 0x02) ? "disabled" : "enabled");

		if (mmode != kATMemoryMode_64K)
			ATConsolePrintf("  CPU bank:      %s\n", (portb & 0x10) ? "disabled" : "enabled");

		if (mmode == kATMemoryMode_128K || mmode == kATMemoryMode_320K)
			ATConsolePrintf("  Antic bank:    %s\n", (portb & 0x20) ? "disabled" : "enabled");

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

	if (g_sim.GetHardwareMode() == kATHardwareMode_5200) {
		ATKernelDatabase5200 kdb5(&g_sim.GetCPUMemory());

		ATConsolePrintf("NMI vectors:\n");
		ATConsolePrintf("VDSLST  Display list NMI              %04X\n", (uint16)kdb5.VDSLST);
		ATConsolePrintf("VVBLKI  Vertical blank immediate      %04X\n", (uint16)kdb5.VVBLKI);
		ATConsolePrintf("VVBLKD  Vertical blank deferred       %04X\n", (uint16)kdb5.VVBLKD);
		ATConsolePrintf("\n");
		ATConsolePrintf("IRQ vectors:\n");
		ATConsolePrintf("VIMIRQ  IRQ immediate                 %04X\n", (uint16)kdb5.VIMIRQ);
		ATConsolePrintf("VKYBDI  Keyboard immediate            %04X\n", (uint16)kdb5.VKYBDI);
		ATConsolePrintf("VKYBDF  Keyboard deferred             %04X\n", (uint16)kdb5.VKYBDF);
		ATConsolePrintf("VTRIGR  Controller trigger            %04X\n", (uint16)kdb5.VTRIGR);
		ATConsolePrintf("VBRKOP  Break instruction             %04X\n", (uint16)kdb5.VBRKOP);
		ATConsolePrintf("VSERIN  Serial I/O receive ready      %04X\n", (uint16)kdb5.VSERIN);
		ATConsolePrintf("VSEROR  Serial I/O transmit ready     %04X\n", (uint16)kdb5.VSEROR);
		ATConsolePrintf("VSEROC  Serial I/O transmit complete  %04X\n", (uint16)kdb5.VSEROC);
		ATConsolePrintf("VTIMR1  POKEY timer 1                 %04X\n", (uint16)kdb5.VTIMR1);
		ATConsolePrintf("VTIMR2  POKEY timer 2                 %04X\n", (uint16)kdb5.VTIMR2);
		ATConsolePrintf("VTIMR4  POKEY timer 4                 %04X\n", (uint16)kdb5.VTIMR2);
	} else {
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

	ATConsolePrintf("\n");

	if (g_sim.GetCPU().GetCPUMode() == kATCPUMode_65C816) {
		ATConsolePrintf("Native COP     %04X\n", g_sim.DebugReadWord(0xFFE4));
		ATConsolePrintf("Native NMI     %04X\n", g_sim.DebugReadWord(0xFFEA));
		ATConsolePrintf("Native Reset   %04X\n", g_sim.DebugReadWord(0xFFEC));
		ATConsolePrintf("Native IRQ     %04X\n", g_sim.DebugReadWord(0xFFEE));
		ATConsolePrintf("COP            %04X\n", g_sim.DebugReadWord(0xFFF4));
	}

	ATConsolePrintf("NMI            %04X\n", g_sim.DebugReadWord(0xFFFA));
	ATConsolePrintf("Reset          %04X\n", g_sim.DebugReadWord(0xFFFC));
	ATConsolePrintf("IRQ            %04X\n", g_sim.DebugReadWord(0xFFFE));
}

void ATConsoleCmdGTIA() {
	g_sim.GetGTIA().DumpStatus();
}

void ATConsoleCmdPokey() {
	g_sim.GetPokey().DumpStatus();
}

void ATConsoleCmdPathRecord(int argc, const char *const *argv) {
	ATDebuggerCmdBool mode(false);
	ATDebuggerCmdParser(argc, argv) >> mode;

	ATCPUEmulator& cpu = g_sim.GetCPU();
	if (mode.IsValid())
		cpu.SetPathfindingEnabled(mode);

	ATConsolePrintf("CPU path recording is now %s.\n", cpu.IsPathfindingEnabled() ? "on" : "off");
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

	g_debugger.AddModule(0, 0x10000, pSymbolStore, NULL);

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

void ATConsoleCmdPathBreak(int argc, const char **argv) {
	ATCPUEmulator& cpu = g_sim.GetCPU();

	if (!argc) {
		ATConsolePrintf("Breaking on new paths is %s.\n", cpu.IsPathBreakEnabled() ? "on" : "off");
		return;
	}

	bool newState = false;
	if (!_stricmp(argv[0], "on")) {
		newState = true;
	} else if (_stricmp(argv[0], "off")) {
		ATConsoleWrite("Syntax: .pathbreak on|off\n");
		return;
	}

	cpu.SetPathBreakEnabled(newState);
	ATConsolePrintf("Breaking on new paths is now %s.\n", newState ? "on" : "off");
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

	g_debugger.AddModule(0xD800, 0x2800, symbols, "Kernel");
	ATConsolePrintf("Kernel symbols loaded: %s\n", argv[0]);
}

void ATConsoleCmdDiskOrder(int argc, const char *const *argv) {
	if (argc < 1) {
		ATConsoleWrite("Syntax: .diskorder <sector> <indices>...\n");
		return;
	}

	ATDiskEmulator& disk = g_sim.GetDiskDrive(0);
	uint32 sector = strtoul(argv[0], NULL, 10);
	uint32 phantomCount = disk.GetSectorPhantomCount(sector);

	if (!phantomCount) {
		ATConsolePrintf("Invalid sector number: %u\n", sector);
		return;
	}

	if (argc == 1) {
		for(uint32 i=0; i<phantomCount; ++i)
			disk.SetForcedPhantomSector(sector, i, -1);

		ATConsolePrintf("Automatic sector ordering restored for sector %u.\n", sector);
		return;
	}

	vdfastvector<uint8> indices;

	for(int i=1; i<argc; ++i) {
		uint32 index = strtoul(argv[i], NULL, 10);

		if (!index || index > phantomCount) {
			ATConsolePrintf("Invalid phantom sector index: %u\n", index);
			return;
		}

		uint8 i8 = (uint8)(index - 1);
		if (std::find(indices.begin(), indices.end(), i8) != indices.end()) {
			ATConsolePrintf("Invalid repeated phantom sector index: %u\n", index);
			return;
		}

		indices.push_back(i8);
	}

	for(uint32 i=0; i<phantomCount; ++i) {
		vdfastvector<uint8>::const_iterator it(std::find(indices.begin(), indices.end(), i));

		if (it == indices.end())
			disk.SetForcedPhantomSector(sector, i, -1);
		else
			disk.SetForcedPhantomSector(sector, i, it - indices.begin());
	}
}

namespace {
	struct SecInfo {
		int mVirtSec;
		int mPhantomIndex;
		float mPos;

		bool operator<(const SecInfo& x) const {
			return mPos < x.mPos;
		}
	};
}

void ATConsoleCmdDiskTrack(int argc, const char *const *argv) {
	if (argc < 1) {
		ATConsoleWrite("Syntax: .disktrack <track>...\n");
		return;
	}

	ATDiskEmulator& disk = g_sim.GetDiskDrive(0);
	uint32 track = strtoul(argv[0], NULL, 10);

	ATConsolePrintf("Track %d\n", track);

	uint32 vsecBase = track * 18;

	vdfastvector<SecInfo> sectors;

	for(uint32 i=0; i<18; ++i) {
		uint32 vsec = vsecBase + i;
		uint32 phantomCount = disk.GetSectorPhantomCount(vsec);

		for(uint32 phantomIdx = 0; phantomIdx < phantomCount; ++phantomIdx) {
			float timing = disk.GetSectorTiming(vsec, phantomIdx);

			if (timing >= 0) {
				SecInfo& si = sectors.push_back();

				si.mVirtSec = vsec;
				si.mPhantomIndex = phantomIdx;
				si.mPos = timing;
			}
		}
	}

	std::sort(sectors.begin(), sectors.end());

	vdfastvector<SecInfo>::const_iterator it(sectors.begin()), itEnd(sectors.end());
	for(; it != itEnd; ++it) {
		const SecInfo& si = *it;

		ATConsolePrintf("%4d/%d   %5.3f\n", si.mVirtSec, si.mPhantomIndex, si.mPos);
	}
}

void ATConsoleCmdCasLogData(int argc, const char *const *argv) {
	ATCassetteEmulator& cas = g_sim.GetCassette();

	bool newSetting = !cas.IsLogDataEnabled();
	cas.SetLogDataEnable(newSetting);

	ATConsolePrintf("Verbose cassette read data logging is now %s.\n", newSetting ? "enabled" : "disabled");
}

void ATConsoleCmdDumpPIAState() {
	g_sim.DumpPIAState();
}

void ATConsoleCmdDumpVBXEState() {
	ATVBXEEmulator *vbxe = g_sim.GetVBXE();

	if (!vbxe)
		ATConsoleWrite("VBXE is not enabled.\n");
	else
		vbxe->DumpStatus();
}

void ATConsoleCmdDumpVBXEBL() {
	ATVBXEEmulator *vbxe = g_sim.GetVBXE();

	if (!vbxe)
		ATConsoleWrite("VBXE is not enabled.\n");
	else
		vbxe->DumpBlitList();
}

void ATConsoleCmdDumpVBXEXDL() {
	ATVBXEEmulator *vbxe = g_sim.GetVBXE();

	if (!vbxe)
		ATConsoleWrite("VBXE is not enabled.\n");
	else
		vbxe->DumpXDL();
}

void ATConsoleCmdVBXETraceBlits(int argc, const char **argv) {
	ATVBXEEmulator *vbxe = g_sim.GetVBXE();

	if (!vbxe) {
		ATConsoleWrite("VBXE is not enabled.\n");
		return;
	}

	if (argc) {
		bool newState = false;
		if (!_stricmp(argv[0], "on")) {
			newState = true;
		} else if (_stricmp(argv[0], "off")) {
			ATConsoleWrite("Syntax: .vbxe_traceblits on|off\n");
			return;
		}

		vbxe->SetBlitLoggingEnabled(newState);
	}

	ATConsolePrintf("VBXE blit tracing is currently %s.\n", vbxe->IsBlitLoggingEnabled() ? "on" : "off");
}

void ATConsoleCmdIOCB(int argc, const char **argv) {
	ATConsoleWrite("CIO IOCBs:\n");
	ATConsoleWrite(" #  Dev  Cd St Bufr PutR BfLn X1 X2 X3 X4 X5 X6\n");

	VDStringA s;

	for(int i=-1; i<=7; ++i) {
		uint16 addr;

		if (i < 0) {
			s = "ZP  ";
			addr = ATKernelSymbols::ICHIDZ;
		} else {
			s.sprintf("%2d  ", i);
			addr = ATKernelSymbols::ICHID + 16*i;
		}

		uint8 iocb[12];
		for(int j=0; j<12; ++j)
			iocb[j] = g_sim.DebugReadByte(addr + j);

		bool driveValid = false;
		if (iocb[0] != 0xFF) {
			uint8 specifier = g_sim.DebugReadByte(ATKernelSymbols::HATABS + iocb[0]);
			if (specifier >= 0x20 && specifier < 0x7F) {
				driveValid = true;

				if (iocb[1] <= 1)
					s.append_sprintf("%c:", specifier);
				else
					s.append_sprintf("%c%d:", specifier, iocb[1]);
			}
		}

		size_t pad = s.size();
		while(pad < 9) {
			++pad;
			s.push_back(' ');
		}

		s.append_sprintf("%02X %02X %02X%02X %02X%02X %02X%02X %02X %02X %02X %02X %02X %02X\n"
			, iocb[2]			// command
			, iocb[3]			// status
			, iocb[5], iocb[4]	// buffer pointer
			, iocb[7], iocb[6]	// put routine
			, iocb[9], iocb[8]	// buffer length
			, iocb[10]
			, iocb[11]
			, iocb[12]
			, iocb[13]
			, iocb[14]
			, iocb[15]
			);

		ATConsoleWrite(s.c_str());
	}
}

void ATConsoleCmdBasic(int argc, const char *const *argv) {
	ATDebuggerCmdParser(argc, argv) >> 0;

	uint16 lomem	= g_sim.DebugReadWord(0x0080);
	uint16 vntp		= g_sim.DebugReadWord(0x0082);
	uint16 vvtp		= g_sim.DebugReadWord(0x0086);
	uint16 stmtab	= g_sim.DebugReadWord(0x0088);
	uint16 starp	= g_sim.DebugReadWord(0x008C);
	uint16 runstk	= g_sim.DebugReadWord(0x008E);
	uint16 memtop	= g_sim.DebugReadWord(0x0090);

	ATConsoleWrite("BASIC table pointers:\n");
	ATConsolePrintf("  LOMEM   Low memory bound      %04X\n", lomem);
	ATConsolePrintf("  VNTP    Variable name table   %04X (%d bytes)\n", vntp, vvtp - vntp);
	ATConsolePrintf("  VVTP    Variable value table  %04X (%d bytes)\n", vvtp, stmtab - vvtp);
	ATConsolePrintf("  STMTAB  Statement table       %04X (%d bytes)\n", stmtab, starp - stmtab);
	ATConsolePrintf("  STARP   String/array table    %04X (%d bytes)\n", starp, runstk - starp);
	ATConsolePrintf("  RUNSTK  Runtime stack         %04X (%d bytes)\n", runstk, memtop - runstk);
	ATConsolePrintf("  MEMTOP  Top of used memory    %04X\n", memtop);
}

void ATConsoleCmdBasicVars(int argc, const char *const *argv) {
	ATConsoleWrite("BASIC variables:\n");

	uint16 lomem = g_sim.DebugReadWord(0x0080);
	uint16 vntp = g_sim.DebugReadWord(0x0082);
	uint16 vvtp = g_sim.DebugReadWord(0x0086);
	uint16 stmtab = g_sim.DebugReadWord(0x0088);

	// validate tables
	if (lomem > vntp || vntp > vvtp || vvtp > stmtab) {
		ATConsoleWrite("Tables are invalid. See .basic output.\n");
		return;
	}

	VDStringA s;
	for(uint8 i=0; i<128; ++i) {
		s.sprintf("$%02X  ", i + 0x80);

		for(int j=0; j<64; ++j) {
			uint8 c = g_sim.DebugReadByte(vntp++);
			if (!c)
				return;

			uint8 d = c & 0x7F;

			if ((uint8)(d - 0x20) < 0x5F)
				s += d;
			else
				s.append_sprintf("<%02X>", d);

			if (c & 0x80)
				break;
		}

		s += '\n';

		ATConsoleWrite(s.c_str());
	}
}

void ATConsoleCmdDumpHelp(int argc, const char *const *argv) {
	vdfastvector<uint8> helpdata;
	if (!ATLoadMiscResource(IDR_DEBUG_HELP, helpdata)) {
		ATConsoleWrite("Unable to load help.\n");
		return;
	}

	const char *cmd = NULL;
	if (argc >= 1)
		cmd = argv[0];

	VDMemoryStream ms(helpdata.data(), helpdata.size());
	VDTextStream ts(&ms);

	bool enabled = !cmd;
	bool anyout = false;

	for(;;) {
		const char *s = ts.GetNextLine();

		if (!s)
			break;

		char c = *s;

		if (c) {
			++s;

			if (*s == ' ')
				++s;

			if (c == '^' || c == '+' || c == '>') {
				if (cmd) {
					if (c == '>')
						continue;

					if (enabled)
						break;

					const char *t = s;
					for(;;) {
						const char *cmdstart = t;
						while(*t && *t != ' ' && *t != ',')
							++t;
						const char *cmdend = t;

						VDStringSpanA cmdcheck(cmdstart, cmdend);

						if (cmdcheck.comparei(cmd) == 0)
							enabled = true;

						if (*t != ',')
							break;

						++t;
						while(*t == ' ')
							++t;
					}
				} else if (c == '^') {
					continue;
				}
			} else if (c == '!') {
				if (cmd)
					continue;
			} else if (c != '.') {
				if (!cmd)
					continue;

				if (enabled)
					anyout = true;
			}
		} else if (!cmd)
			continue;

		if (!enabled)
			continue;

		ATConsolePrintf("%s\n", s);
	}

	if (cmd && !anyout)
		ATConsolePrintf("No detailed help available for command: %s.\n", cmd);
}

void ATConsoleExecuteCommand(char *s) {
	IATDebuggerActiveCommand *cmd = g_debugger.GetActiveCommand();

	if (cmd) {
		g_debugger.ExecuteCommand(s);
		return;
	}

	if (!*s)
		return;

	ATConsolePrintf("%s> ", g_debugger.GetPrompt());
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

		if (!strcmp(cmd, "a")) {
			ATConsoleCmdAssemble(argc-1, argv+1);
		} else if (!strcmp(cmd, "t")) {
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
		} else if (!strcmp(cmd, "h")) {
			ATConsoleCmdDumpHistory(argc-1, argv+1);
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
		} else if (!strcmp(cmd, "di")) {
			ATConsoleCmdDumpINTERNAL(argc-1, argv+1);
		} else if (!strcmp(cmd, "lm")) {
			ATConsoleCmdListModules();
		} else if (!strcmp(cmd, "ln")) {
			ATConsoleCmdListNearestSymbol(argc-1, argv+1);
		} else if (!strcmp(cmd, "wb")) {
			ATConsoleCmdWatchByte(argc-1, argv+1);
		} else if (!strcmp(cmd, "ww")) {
			ATConsoleCmdWatchWord(argc-1, argv+1);
		} else if (!strcmp(cmd, "wc")) {
			ATConsoleCmdWatchClear(argc-1, argv+1);
		} else if (!strcmp(cmd, "wl")) {
			ATConsoleCmdWatchList(argc-1, argv+1);
		} else if (!strcmp(cmd, "ya")) {
			ATConsoleCmdSymbolAdd(argc-1, argv+1);
		} else if (!strcmp(cmd, "yc")) {
			ATConsoleCmdSymbolClear(argc-1, argv+1);
		} else if (!strcmp(cmd, "yd")) {
			ATConsoleCmdSymbolRemove(argc-1, argv+1);
		} else if (!strcmp(cmd, "yr")) {
			ATConsoleCmdSymbolRead(argc-1, argv+1);
		} else if (!strcmp(cmd, "yw")) {
			ATConsoleCmdSymbolWrite(argc-1, argv+1);
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
		} else if (!strcmp(cmd, ".pathrecord")) {
			ATConsoleCmdPathRecord(argc-1, argv+1);
		} else if (!strcmp(cmd, ".pathreset")) {
			ATConsoleCmdPathReset();
		} else if (!strcmp(cmd, ".pathdump")) {
			ATConsoleCmdPathDump(argc-1, argv+1);
		} else if (!strcmp(cmd, ".pathbreak")) {
			ATConsoleCmdPathBreak(argc-1, argv+1);
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
		} else if (!strcmp(cmd, ".loadsym")) {
			ATConsoleCmdLoadSymbols(argc-1, argv+1);
		} else if (!strcmp(cmd, ".unloadsym")) {
			ATConsoleCmdUnloadSymbols(argc-1, argv+1);
		} else if (!strcmp(cmd, ".diskorder")) {
			ATConsoleCmdDiskOrder(argc-1, argv+1);
		} else if (!strcmp(cmd, ".disktrack")) {
			ATConsoleCmdDiskTrack(argc-1, argv+1);
		} else if (!strcmp(cmd, ".dma")) {
			g_sim.GetAntic().DumpDMAPattern();
		} else if (!strcmp(cmd, ".caslogdata")) {
			ATConsoleCmdCasLogData(argc-1, argv+1);
		} else if (!strcmp(cmd, ".pia")) {
			ATConsoleCmdDumpPIAState();
		} else if (!strcmp(cmd, ".vbxe")) {
			ATConsoleCmdDumpVBXEState();
		} else if (!strcmp(cmd, ".vbxe_xdl")) {
			ATConsoleCmdDumpVBXEXDL();
		} else if (!strcmp(cmd, ".vbxe_bl")) {
			ATConsoleCmdDumpVBXEBL();
		} else if (!strcmp(cmd, ".vbxe_traceblits")) {
			ATConsoleCmdVBXETraceBlits(argc-1, argv+1);
		} else if (!strcmp(cmd, ".iocb")) {
			ATConsoleCmdIOCB(argc-1, argv+1);
		} else if (!strcmp(cmd, ".basic")) {
			ATConsoleCmdBasic(argc-1, argv+1);
		} else if (!strcmp(cmd, ".basic_vars")) {
			ATConsoleCmdBasicVars(argc-1, argv+1);
		} else if (!strcmp(cmd, "?")) {
			ATConsoleCmdDumpHelp(argc-1, argv+1);
		} else {
			ATConsoleWrite("Unrecognized command. ? for help\n");
		}
	}
}
