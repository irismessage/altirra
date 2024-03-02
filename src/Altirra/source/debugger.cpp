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
#include <vd2/system/binary.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/error.h>
#include <vd2/system/strutil.h>
#include <vd2/system/vdstl_hashset.h>
#include "console.h"
#include "cpu.h"
#include "simulator.h"
#include "disasm.h"
#include "debugger.h"
#include "debuggerexp.h"
#include "decmath.h"
#include "symbols.h"
#include "ksyms.h"
#include "kerneldb.h"
#include "cassette.h"
#include "vbxe.h"
#include "uirender.h"
#include "resource.h"
#include "oshelper.h"
#include "bkptmanager.h"
#include "mmu.h"
#include "verifier.h"
#include "pclink.h"

extern ATSimulator g_sim;

void ATSetFullscreen(bool enabled);
bool ATConsoleCheckBreak();
void ATCreateDebuggerCmdAssemble(uint32 address, IATDebuggerActiveCommand **);
void ATConsoleExecuteCommand(const char *s, bool echo = true);

///////////////////////////////////////////////////////////////////////////////

int ATDebuggerParseArgv(const char *s, vdfastvector<char>& tempstr, vdfastvector<const char *>& argptrs) {
	vdfastvector<size_t> argoffsets;
	const char *t = s;
	for(;;) {
		while(*t && *t == ' ')
			++t;

		if (!*t)
			break;

		argoffsets.push_back(tempstr.size());

		if (*t == '"') {
			++t;

			tempstr.push_back('"');
			for(;;) {
				char c = *t;
				if (!c)
					break;
				++t;

				if (c == '"')
					break;

				if (c == '\\') {
					c = *t;
					if (!c)
						break;
					++t;

					if (c == 'n')
						c = '\n';
				}

				tempstr.push_back(c);
			}

			tempstr.push_back('"');
		} else {
			const char *start = t;
			while(*t && *t != ' ')
				++t;

			tempstr.insert(tempstr.end(), start, t);
		}

		tempstr.push_back(0);

		if (!*t)
			break;
	}

	const int argc = argoffsets.size();
	argptrs.clear();
	argptrs.resize(argc + 1, NULL);
	for(int i=0; i<argc; ++i)
		argptrs[i] = tempstr.data() + argoffsets[i];

	return argc;
}

///////////////////////////////////////////////////////////////////////////////

class ATDebugger : public IATSimulatorCallback, public IATDebugger, public IATDebuggerSymbolLookup {
public:
	ATDebugger();
	~ATDebugger();

	ATBreakpointManager *GetBreakpointManager() { return mpBkptManager; }

	bool IsRunning() const;

	bool Init();
	void Shutdown();

	void Detach();

	bool Tick();

	void SetSourceMode(ATDebugSrcMode sourceMode);
	void Break();
	void Run(ATDebugSrcMode sourceMode);
	void RunTraced();
	void StepInto(ATDebugSrcMode sourceMode, uint32 rgnStart = 0, uint32 rgnSize = 0);
	void StepOver(ATDebugSrcMode sourceMode, uint32 rgnStart = 0, uint32 rgnSize = 0);
	void StepOut(ATDebugSrcMode sourceMode);
	uint16 GetPC() const;
	void SetPC(uint16 pc);
	uint16 GetFramePC() const;
	void SetFramePC(uint16 pc);
	uint32 GetCallStack(ATCallStackFrame *dst, uint32 maxCount);
	void DumpCallStack();

	// breakpoints
	void ClearAllBreakpoints();
	void ToggleBreakpoint(uint16 addr);
	void ToggleAccessBreakpoint(uint16 addr, bool write);
	sint32 LookupUserBreakpoint(uint32 useridx) const;
	uint32 RegisterSystemBreakpoint(uint32 sysidx, ATDebugExpNode *condexp = NULL, const char *command = NULL);
	void UnregisterSystemBreakpoint(uint32 sysidx);
	void GetBreakpointList(vdfastvector<uint32>& bps) const;
	ATDebugExpNode *GetBreakpointCondition(uint32 useridx) const;
	const char *GetBreakpointCommand(uint32 useridx) const;

	bool IsBreakOnEXERunAddrEnabled() const { return mbBreakOnEXERunAddr; }
	void SetBreakOnEXERunAddrEnabled(bool en) { mbBreakOnEXERunAddr = en; }

	int AddWatch(uint32 address, int length);
	bool ClearWatch(int idx);
	void ClearAllWatches();
	bool GetWatchInfo(int idx, ATDebuggerWatchInfo& info);

	void ListModules();
	void UnloadSymbolsByIndex(uint32 index);

	void DumpCIOParameters();

	bool IsCIOTracingEnabled() const { return mSysBPTraceCIO >= 0; }

	void SetCIOTracingEnabled(bool enabled);

	// symbol handling
	uint32 AddModule(uint32 base, uint32 size, IATSymbolStore *symbolStore, const char *name);
	void RemoveModule(uint32 base, uint32 size, IATSymbolStore *symbolStore);

	void AddClient(IATDebuggerClient *client, bool requestUpdate);
	void RemoveClient(IATDebuggerClient *client);
	void RequestClientUpdate(IATDebuggerClient *client);

	uint32 LoadSymbols(const wchar_t *fileName, bool processDirectives);
	void UnloadSymbols(uint32 moduleId);
	void ProcessSymbolDirectives(uint32 id);

	sint32 ResolveSymbol(const char *s, bool allowGlobal = false);
	uint32 ResolveSymbolThrow(const char *s, bool allowGlobal = false);

	uint32 AddCustomModule(const char *name);
	uint32 GetCustomModuleIdByName(const char *name);
	void AddCustomSymbol(uint32 address, uint32 len, const char *name, uint32 rwxmode, uint32 moduleId = 0);
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
		ATCPUEmulatorMemory& mem = g_sim.GetCPUMemory();

		while(len--)
			mem.WriteByte(address++, *data8++);
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

	VDEvent<IATDebugger, bool>& OnRunStateChanged() { return mEventRunStateChanged; }
	VDEvent<IATDebugger, ATDebuggerOpenEvent *>& OnDebuggerOpen() { return mEventOpen; }

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
	bool LookupLine(uint32 addr, bool searchUp, uint32& moduleId, ATSourceLineInfo& lineInfo);
	bool LookupFile(const wchar_t *fileName, uint32& moduleId, uint16& fileId);
	void GetLinesForFile(uint32 moduleId, uint16 fileId, vdfastvector<ATSourceLineInfo>& lines);

public:
	void OnSimulatorEvent(ATSimulatorEvent ev);

protected:
	struct Module {
		uint32	mId;
		uint32	mBase;
		uint32	mSize;
		bool	mbDirty;
		vdrefptr<IATSymbolStore>	mpSymbols;
		VDStringA	mName;
		vdfastvector<uint16> mSilentlyIgnoredFiles;
	};

	void ClearAllBreakpoints(bool notify);
	void UpdateClientSystemState(IATDebuggerClient *client = NULL);
	void ActivateSourceWindow();
	Module *GetModuleById(uint32 id);
	void NotifyEvent(ATDebugEvent eventId);
	void OnBreakpointHit(ATBreakpointManager *sender, ATBreakpointEvent *event);
	static bool CPUSourceStepIntoCallback(ATCPUEmulator *cpu, uint32 pc, void *data);

	uint32	mNextModuleId;
	uint16	mFramePC;
	uint32	mSysBPTraceCIO;
	uint32	mSysBPEEXRun;
	bool	mbSourceMode;
	bool	mbBreakOnEXERunAddr;
	bool	mbClientUpdatePending;
	bool	mbClientLastRunState;

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
	VDEvent<IATDebugger, bool> mEventRunStateChanged;
	VDEvent<IATDebugger, ATDebuggerOpenEvent *> mEventOpen;

	typedef vdfastvector<IATDebuggerActiveCommand *> ActiveCommands;
	ActiveCommands mActiveCommands;

	struct UserBP {
		uint32	mSysBP;
		uint32	mModuleId;
		ATDebugExpNode	*mpCondition;
		VDStringA mCommand;
	};

	struct UserBPFreePred {
		bool operator()(const UserBP& x) {
			return x.mSysBP == (uint32)-1;
		}
	};

	typedef vdvector<UserBP> UserBPs;
	UserBPs mUserBPs;

	typedef vdhashmap<uint32, uint32> SysBPToUserBPMap;
	SysBPToUserBPMap mSysBPToUserBPMap;

	ATBreakpointManager *mpBkptManager;
	VDDelegate	mDelBreakpointHit;

	std::deque<VDStringA> mCommandQueue;
};

ATDebugger g_debugger;

IATDebugger *ATGetDebugger() { return &g_debugger; }
IATDebuggerSymbolLookup *ATGetDebuggerSymbolLookup() { return &g_debugger; }

void ATInitDebugger() {
	g_debugger.Init();
}

void ATShutdownDebugger() {
	g_debugger.Shutdown();
}

ATDebugger::ATDebugger()
	: mNextModuleId(kModuleId_Custom)
	, mFramePC(0)
	, mSysBPTraceCIO(0)
	, mSysBPEEXRun(0)
	, mbSourceMode(false)
	, mbClientUpdatePending(false)
	, mbClientLastRunState(false)
	, mClientsBusy(0)
	, mbClientsChanged(false)
	, mpBkptManager(NULL)
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

	if (!mpBkptManager) {
		mpBkptManager = new ATBreakpointManager;
		mpBkptManager->Init(&g_sim.GetCPU(), g_sim.GetMemoryManager(), &g_sim);
		mpBkptManager->OnBreakpointHit() += mDelBreakpointHit.Bind(this, &ATDebugger::OnBreakpointHit);
	}

	return true;
}

void ATDebugger::Shutdown() {
	ClearAllBreakpoints(false);

	if (mpBkptManager) {
		mpBkptManager->OnBreakpointHit() -= mDelBreakpointHit;
		mpBkptManager->Shutdown();
		delete mpBkptManager;
		mpBkptManager = NULL;
	}
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

bool ATDebugger::Tick() {
	if (!mActiveCommands.empty() || mCommandQueue.empty()) {
		if (mbClientUpdatePending)
			UpdateClientSystemState();
		return false;
	}

	VDStringA s;
	s.swap(mCommandQueue.front());
	mCommandQueue.pop_front();

	ATConsoleExecuteCommand(s.c_str(), false);
	return true;
}

void ATDebugger::Break() {
	if (g_sim.IsRunning()) {
		g_sim.Suspend();

		ATCPUEmulator& cpu = g_sim.GetCPU();
		cpu.DumpStatus();

		mFramePC = cpu.GetInsnPC();
		mbClientUpdatePending = true;
	}

	TerminateActiveCommands();
	mCommandQueue.clear();
}

void ATDebugger::Run(ATDebugSrcMode sourceMode) {
	if (g_sim.IsRunning())
		return;

	SetSourceMode(sourceMode);

	ATCPUEmulator& cpu = g_sim.GetCPU();
	cpu.SetStep(false);
	cpu.SetTrace(false);
	g_sim.Resume();
	mbClientUpdatePending = true;

	if (!mbClientLastRunState)
		UpdateClientSystemState();
}

void ATDebugger::RunTraced() {
	if (g_sim.IsRunning())
		return;

	ATCPUEmulator& cpu = g_sim.GetCPU();
	cpu.SetStep(false);
	cpu.SetTrace(true);
	g_sim.Resume();
	mbClientUpdatePending = true;

	if (!mbClientLastRunState)
		UpdateClientSystemState();
}

void ATDebugger::ClearAllBreakpoints() {
	ClearAllBreakpoints(false);
}

void ATDebugger::ClearAllBreakpoints(bool notify) {
	uint32 n = mUserBPs.size();

	for(uint32 i=0; i<n; ++i) {
		UserBP& bp = mUserBPs[i];

		if (bp.mSysBP != (uint32)-1) {
			mpBkptManager->Clear(bp.mSysBP);
			UnregisterSystemBreakpoint(bp.mSysBP);
		}
	}

	if (notify)
		g_sim.NotifyEvent(kATSimEvent_CPUPCBreakpointsUpdated);
}

void ATDebugger::ToggleBreakpoint(uint16 addr) {
	ATBreakpointIndices indices;

	mpBkptManager->GetAtPC(addr, indices);

	// try to find an index we know about
	sint32 useridx = -1;
	uint32 sysidx;
	while(!indices.empty()) {
		sysidx = indices.back();
		indices.pop_back();

		SysBPToUserBPMap::const_iterator it(mSysBPToUserBPMap.find(sysidx));
		if (it != mSysBPToUserBPMap.end()) {
			useridx = it->second;
			break;
		}

		indices.pop_back();
	}

	if (useridx >= 0) {
		mpBkptManager->Clear(sysidx);
		UnregisterSystemBreakpoint(sysidx);
	} else {
		sysidx = mpBkptManager->SetAtPC(addr);
		RegisterSystemBreakpoint(sysidx);
	}

	g_sim.NotifyEvent(kATSimEvent_CPUPCBreakpointsUpdated);
}

void ATDebugger::ToggleAccessBreakpoint(uint16 addr, bool write) {
	ATBreakpointIndices indices;

	mpBkptManager->GetAtAccessAddress(addr, indices);

	// try to find an index we know about and is the right type
	sint32 useridx = -1;
	uint32 sysidx;
	while(!indices.empty()) {
		sysidx = indices.back();
		indices.pop_back();

		ATBreakpointInfo info;
		VDVERIFY(mpBkptManager->GetInfo(sysidx, info));

		if (write) {
			if (!info.mbBreakOnWrite)
				continue;
		} else {
			if (!info.mbBreakOnRead)
				continue;
		}

		SysBPToUserBPMap::const_iterator it(mSysBPToUserBPMap.find(sysidx));
		if (it != mSysBPToUserBPMap.end()) {
			useridx = it->second;
			break;
		}
	}

	if (useridx >= 0) {
		mpBkptManager->Clear(sysidx);
		UnregisterSystemBreakpoint(sysidx);
	} else {
		sysidx = mpBkptManager->SetAccessBP(addr, !write, write);
		RegisterSystemBreakpoint(sysidx);
	}

	g_sim.NotifyEvent(kATSimEvent_CPUPCBreakpointsUpdated);
}

sint32 ATDebugger::LookupUserBreakpoint(uint32 useridx) const {
	if (useridx >= mUserBPs.size())
		return -1;

	return mUserBPs[useridx].mSysBP;
}

uint32 ATDebugger::RegisterSystemBreakpoint(uint32 sysidx, ATDebugExpNode *condexp, const char *command) {
	UserBPs::iterator it(std::find_if(mUserBPs.begin(), mUserBPs.end(), UserBPFreePred()));
	uint32 useridx = it - mUserBPs.begin();

	if (it == mUserBPs.end())
		mUserBPs.push_back();

	UserBP& ubp = mUserBPs[useridx];
	ubp.mSysBP = sysidx;
	ubp.mpCondition = condexp;
	ubp.mCommand = command ? command : "";
	ubp.mModuleId = 0;

	mSysBPToUserBPMap[sysidx] = useridx;

	return useridx;
}

void ATDebugger::UnregisterSystemBreakpoint(uint32 sysidx) {
	SysBPToUserBPMap::iterator it(mSysBPToUserBPMap.find(sysidx));
	VDASSERT(it != mSysBPToUserBPMap.end());

	const uint32 useridx = it->second;
	mSysBPToUserBPMap.erase(it);

	UserBP& bp = mUserBPs[useridx];
	bp.mSysBP = (uint32)-1;
	bp.mModuleId = 0;

	if (bp.mpCondition) {
		delete bp.mpCondition;
		bp.mpCondition = NULL;
	}
}

void ATDebugger::GetBreakpointList(vdfastvector<uint32>& bps) const {
	const uint32 n = mUserBPs.size();

	for(uint32 i=0; i<n; ++i) {
		if (mUserBPs[i].mSysBP != (uint32)-1)
			bps.push_back(i);
	}
}

ATDebugExpNode *ATDebugger::GetBreakpointCondition(uint32 useridx) const {
	if (useridx >= mUserBPs.size())
		return NULL;

	return mUserBPs[useridx].mpCondition;
}

const char *ATDebugger::GetBreakpointCommand(uint32 useridx) const {
	if (useridx >= mUserBPs.size())
		return NULL;

	const char *s = mUserBPs[useridx].mCommand.c_str();

	return *s ? s : NULL;
}

void ATDebugger::StepInto(ATDebugSrcMode sourceMode, uint32 regionStart, uint32 regionSize) {
	if (g_sim.IsRunning())
		return;

	SetSourceMode(sourceMode);

	ATCPUEmulator& cpu = g_sim.GetCPU();

	cpu.SetTrace(false);

	if (mbSourceMode && regionSize > 0)
		cpu.SetStepRange(regionStart, regionSize, CPUSourceStepIntoCallback, this);
	else
		cpu.SetStep(true);

	g_sim.Resume();
	mbClientUpdatePending = true;

	if (!mbClientLastRunState)
		UpdateClientSystemState();
}

void ATDebugger::StepOver(ATDebugSrcMode sourceMode, uint32 regionStart, uint32 regionSize) {
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
		cpu.SetStepRange(regionStart, regionSize, NULL, NULL);
		cpu.SetTrace(false);
	}

	g_sim.Resume();
	mbClientUpdatePending = true;

	if (!mbClientLastRunState)
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
	mbClientUpdatePending = true;

	if (!mbClientLastRunState)
		UpdateClientSystemState();
}

uint16 ATDebugger::GetPC() const {
	return g_sim.GetCPU().GetInsnPC();
}

void ATDebugger::SetPC(uint16 pc) {
	if (g_sim.IsRunning())
		return;

	ATCPUEmulator& cpu = g_sim.GetCPU();
	cpu.SetPC(pc);
	mFramePC = pc;
	mbClientUpdatePending = true;
}

uint16 ATDebugger::GetFramePC() const {
	return mFramePC;
}

void ATDebugger::SetFramePC(uint16 pc) {
	mFramePC = pc;

	mbClientUpdatePending = true;
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
	if (enabled) {
		if (mSysBPTraceCIO)
			return;

		mSysBPTraceCIO = mpBkptManager->SetAtPC(ATKernelSymbols::CIOV);
	} else {
		if (mSysBPTraceCIO)
			return;

		mpBkptManager->Clear(mSysBPTraceCIO);
		mSysBPTraceCIO = 0;
	}
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

uint32 ATDebugger::LoadSymbols(const wchar_t *fileName, bool processDirectives) {
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

	uint32 moduleId = AddModule(symStore->GetDefaultBase(), symStore->GetDefaultSize(), symStore, VDTextWToA(fileName).c_str());

	if (moduleId && processDirectives)
		ProcessSymbolDirectives(moduleId);

	return moduleId;
}

void ATDebugger::UnloadSymbols(uint32 moduleId) {
	Modules::iterator it(mModules.begin()), itEnd(mModules.end());
	for(; it!=itEnd; ++it) {
		const Module& mod = *it;

		if (mod.mId == moduleId) {
			mModules.erase(it);
			NotifyEvent(kATDebugEvent_SymbolsChanged);

			// scan all breakpoints and clear those from this module
			bool bpsRemoved = false;

			uint32 n = mUserBPs.size();

			for(uint32 i=0; i<n; ++i) {
				UserBP& ubp = mUserBPs[i];

				if (ubp.mModuleId == moduleId && ubp.mSysBP != (uint32)-1) {
					mpBkptManager->Clear(ubp.mSysBP);
					UnregisterSystemBreakpoint(ubp.mSysBP);
				}
			}

			return;
		}
	}
}

void ATDebugger::ProcessSymbolDirectives(uint32 id) {
	Module *mod = GetModuleById(id);
	if (!mod)
		return;

	if (!mod->mpSymbols)
		return;

	uint32 directiveCount = mod->mpSymbols->GetDirectiveCount();

	for(uint32 i = 0; i < directiveCount; ++i) {
		ATSymbolDirectiveInfo dirInfo;

		mod->mpSymbols->GetDirective(i, dirInfo);

		switch(dirInfo.mType) {
			case kATSymbolDirType_Assert:
				{
					vdautoptr<ATDebugExpNode> expr;

					try {
						expr = ATDebuggerParseExpression(dirInfo.mpArguments, this);
						
						vdautoptr<ATDebugExpNode> expr2(ATDebuggerInvertExpression(expr));
						expr.release();
						expr.swap(expr2);

						VDStringA cmd;

						cmd.sprintf(".printf \"Assert failed at $%04X: ", dirInfo.mOffset);

						for(const char *s = dirInfo.mpArguments; *s; ++s) {
							unsigned char c = *s;

							if (c == '%')
								cmd += '%';
							else if (c == '\\' || c == '"')
								cmd += '\\';
							else if (c < 0x20 || (c >= 0x7f && c < 0xa0))
								continue;

							cmd += c;
						}

						cmd += "\"";

						uint32 sysidx = mpBkptManager->SetAtPC(dirInfo.mOffset);
						uint32 useridx = RegisterSystemBreakpoint(sysidx, expr, cmd.c_str());
						expr.release();

						mUserBPs[useridx].mModuleId = id;
					} catch(const ATDebuggerExprParseException&) {
						ATConsolePrintf("Invalid assert directive expression: %s\n", dirInfo.mpArguments);
					}
				}
				break;

			case kATSymbolDirType_Trace:
				{
					vdfastvector<char> argstore;
					vdfastvector<const char *> argv;

					ATDebuggerParseArgv(dirInfo.mpArguments, argstore, argv);

					VDStringA cmd;

					cmd = ".printf ";

					argv.pop_back();
					for(vdfastvector<const char *>::const_iterator it(argv.begin()), itEnd(argv.end()); it != itEnd; ++it) {
						const char *arg = *it;
						const char *argEnd = arg + strlen(arg);
						bool useQuotes = false;

						if (*arg == '"') {
							++arg;

							if (argEnd != arg && argEnd[-1] == '"')
								--argEnd;

							useQuotes = true;
						} else if (strchr(arg, ';'))
							useQuotes = true;
							

						if (useQuotes)
							cmd += '"';

						cmd.append(arg, argEnd - arg);

						if (useQuotes)
							cmd += '"';

						cmd += ' ';
					}

					cmd += "; g";

					uint32 sysidx = mpBkptManager->SetAtPC(dirInfo.mOffset);
					uint32 useridx = RegisterSystemBreakpoint(sysidx, NULL, cmd.c_str());

					mUserBPs[useridx].mModuleId = id;
				}
				break;
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
		return g_sim.GetCPU().GetInsnPC();

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

uint32 ATDebugger::AddCustomModule(const char *name) {
	uint32 existingId = GetCustomModuleIdByName(name);
	if (existingId)
		return existingId;

	mModules.push_back(Module());
	Module& mod = mModules.back();

	mod.mName = name;
	mod.mId = mNextModuleId++;
	mod.mBase = 0;
	mod.mSize = 0x10000;

	vdrefptr<IATCustomSymbolStore> p;
	ATCreateCustomSymbolStore(~p);

	p->Init(0, 0x10000);

	mod.mpSymbols = p;
	mod.mbDirty = false;

	return mod.mId;
}

uint32 ATDebugger::GetCustomModuleIdByName(const char *name) {
	Modules::iterator it(mModules.begin()), itEnd(mModules.end());
	for(; it!=itEnd; ++it) {
		Module& mod = *it;

		if (!vdstricmp(name, mod.mName.c_str()))
			return mod.mId;
	}

	return 0;
}

void ATDebugger::AddCustomSymbol(uint32 address, uint32 len, const char *name, uint32 rwxmode, uint32 moduleId) {
	Module *mmod = NULL;

	if (moduleId) {
		mmod = GetModuleById(moduleId);
		if (!mmod)
			return;
	} else {
		Modules::iterator it(mModules.begin()), itEnd(mModules.end());
		for(; it!=itEnd; ++it) {
			Module& mod = *it;

			if (mod.mId == kModuleId_Manual) {
				mmod = &mod;
				break;
			}
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

		p->Init(0, 0x10000);

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

bool ATDebugger::LookupLine(uint32 addr, bool searchUp, uint32& moduleId, ATSourceLineInfo& lineInfo) {
	Modules::const_iterator it(mModules.begin()), itEnd(mModules.end());
	uint32 bestOffset = 0xFFFFFFFFUL;
	bool valid = false;

	for(; it!=itEnd; ++it) {
		const Module& mod = *it;
		uint32 offset = addr - mod.mBase;

		if (offset < mod.mSize && mod.mpSymbols) {
			if (mod.mpSymbols->GetLineForOffset(offset, searchUp, lineInfo)) {
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
		if (mSysBPEEXRun) {
			mpBkptManager->Clear(mSysBPEEXRun);
			mSysBPEEXRun = 0;
		}

		LoadSymbols(L"kernel", false);
		LoadSymbols(L"kerneldb", false);
		LoadSymbols(L"hardware", false);
		return;
	}

	ATCPUEmulator& cpu = g_sim.GetCPU();

	if (ev == kATSimEvent_EXEInitSegment) {
		return;
	} else if (ev == kATSimEvent_EXERunSegment) {
		if (!mbBreakOnEXERunAddr)
			return;

		if (mSysBPEEXRun)
			mpBkptManager->Clear(mSysBPEEXRun);

		mSysBPEEXRun = mpBkptManager->SetAtPC(cpu.GetPC());
		return;
	}

	if (ev == kATSimEvent_CPUPCBreakpointsUpdated)
		NotifyEvent(kATDebugEvent_BreakpointsChanged);

	ATSetFullscreen(false);

	if (!ATIsDebugConsoleActive()) {
		ATDebuggerOpenEvent event;
		event.mbAllowOpen = true;
		event.mInterruptingEvent = ev;

		mEventOpen.Raise(this, &event);

		if (!event.mbAllowOpen)
			return;

		ATOpenConsole();
	}

	switch(ev) {
		case kATSimEvent_CPUSingleStep:
			cpu.DumpStatus();
			break;
		case kATSimEvent_CPUStackBreakpoint:
			cpu.DumpStatus();
			break;
		case kATSimEvent_CPUPCBreakpoint:
			cpu.DumpStatus();
			break;

		case kATSimEvent_CPUIllegalInsn:
			ATConsolePrintf("CPU: Illegal instruction hit: %04X\n", cpu.GetPC());
			cpu.DumpStatus();
			break;

		case kATSimEvent_ReadBreakpoint:
			cpu.DumpStatus();
			break;

		case kATSimEvent_WriteBreakpoint:
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

	mbClientUpdatePending = true;

	if (mbSourceMode)
		ActivateSourceWindow();
}

void ATDebugger::UpdateClientSystemState(IATDebuggerClient *client) {
	mbClientUpdatePending = false;

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

	if (mbClientLastRunState != sysstate.mbRunning) {
		mbClientLastRunState = sysstate.mbRunning;

		mEventRunStateChanged.Raise(this, sysstate.mbRunning);
	}

	if (!sysstate.mbRunning) {
		ATSourceLineInfo lineInfo;
		if (LookupLine(sysstate.mPC, false, sysstate.mPCModuleId, lineInfo)) {
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
	if (!lookup->LookupLine(mFramePC, false, moduleId, lineInfo) || (uint32)mFramePC - lineInfo.mOffset >= 100)
		return;

	if (!lineInfo.mLine)
		return;

	Module *mod = GetModuleById(moduleId);
	if (mod) {
		if (std::binary_search(mod->mSilentlyIgnoredFiles.begin(), mod->mSilentlyIgnoredFiles.end(), lineInfo.mFileId))
			return;
	}

	VDStringW path;
	if (!lookup->GetSourceFilePath(moduleId, lineInfo.mFileId, path))
		return;

	IATSourceWindow *w = ATOpenSourceWindow(path.c_str());
	if (!w) {
		if (mod)
			mod->mSilentlyIgnoredFiles.insert(std::lower_bound(mod->mSilentlyIgnoredFiles.begin(), mod->mSilentlyIgnoredFiles.end(), lineInfo.mFileId), lineInfo.mFileId);

		return;
	}

	w->ActivateLine(lineInfo.mLine - 1);
}

ATDebugger::Module *ATDebugger::GetModuleById(uint32 id) {
	Modules::iterator it(mModules.begin()), itEnd(mModules.end());
	for(; it != itEnd; ++it) {
		Module& mod = *it;

		if (mod.mId == id)
			return &mod;
	}

	return NULL;
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

void ATDebugger::OnBreakpointHit(ATBreakpointManager *sender, ATBreakpointEvent *event) {
	if (event->mIndex == (uint32)mSysBPTraceCIO) {
		DumpCIOParameters();
		return;
	}

	if (event->mIndex == (uint32)mSysBPEEXRun) {
		mpBkptManager->Clear(mSysBPEEXRun);
		mSysBPEEXRun = 0;
		ATConsoleWrite("Breakpoint at EXE run address hit\n");
		event->mbBreak = true;
		return;
	}

	SysBPToUserBPMap::const_iterator it(mSysBPToUserBPMap.find(event->mIndex));

	if (it == mSysBPToUserBPMap.end())
		return;

	const uint32 useridx = it->second;

	UserBP& bp = mUserBPs[useridx];

	if (bp.mpCondition) {
		ATDebugExpEvalContext context;
		context.mpCPU = &g_sim.GetCPU();
		context.mpMemory = &g_sim.GetCPUMemory();
		context.mpAntic = &g_sim.GetAntic();

		sint32 result;
		if (!bp.mpCondition->Evaluate(result, context) || !result)
			return;
	}

	if (bp.mCommand.empty()) {
		ATConsolePrintf("Breakpoint %u hit\n", useridx);
	} else {
		const char *s = bp.mCommand.c_str();

		for(;;) {
			while(*s == ' ')
				++s;

			const char *start = s;

			for(;;) {
				char c = *s;

				if (!c || c == ';')
					break;

				++s;

				if (c == '"') {
					for(;;) {
						c = *s;
						if (!c)
							break;
						++s;

						if (c == '"')
							break;

						if (c == '\\') {
							c = *s;
							if (!c)
								break;

							++s;
						}
					}
				}
			}

			if (start != s) {
				mCommandQueue.push_back(VDStringA());
				mCommandQueue.back().assign(start, s);
			}

			if (!*s)
				break;

			++s;
		}

		event->mbSilentBreak = true;
	}

	event->mbBreak = true;
	mbClientUpdatePending = true;
}

bool ATDebugger::CPUSourceStepIntoCallback(ATCPUEmulator *cpu, uint32 pc, void *data) {
	ATDebugger *thisptr = (ATDebugger *)data;
	uint32 moduleId;
	ATSourceLineInfo lineInfo;

	if (!thisptr->LookupLine(pc, false, moduleId, lineInfo))
		return true;

	Module *mod = thisptr->GetModuleById(moduleId);
	if (mod) {
		if (std::binary_search(mod->mSilentlyIgnoredFiles.begin(), mod->mSilentlyIgnoredFiles.end(), lineInfo.mFileId))
			return true;
	}

	if (pc - lineInfo.mOffset > 100)
		return true;

	return false;
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
		ATDebuggerCmdAddress(bool general, bool required, bool allowStar = false)
			: mAddress(0)
			, mbGeneral(general)
			, mbRequired(required)
			, mbAllowStar(allowStar)
			, mbStar(false)
			, mbValid(false)
		{
		}

		bool IsValid() const { return mbValid; }
		bool IsStar() const { return mbStar; }

		operator uint32() const { return mAddress; }

	protected:
		friend class ATDebuggerCmdParser;

		uint32 mAddress;
		bool mbGeneral;
		bool mbRequired;
		bool mbAllowStar;
		bool mbStar;
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

		const VDStringA& operator*() const { return mName; }
		const VDStringA *operator->() const { return &mName; }

	protected:
		friend class ATDebuggerCmdParser;

		VDStringA mName;
		bool mbRequired;
		bool mbValid;
	};

	class ATDebuggerCmdQuotedString {
	public:
		ATDebuggerCmdQuotedString(bool required)
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
		ATDebuggerCmdParser& operator>>(ATDebuggerCmdQuotedString& nm);
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

		const char *t = s;
		char *end = (char *)s;

		int base = 10;
		if (*t == '$') {
			++t;
			base = 16;
		}

		long v = strtol(t, &end, base);

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
				if (s[0] == '*' && !s[1] && ad.mbAllowStar) {
					ad.mbStar = true;
					ad.mbValid = true;
				} else {
					ad.mAddress = g_debugger.ResolveSymbolThrow(s, ad.mbGeneral);
					ad.mbValid = true;
				}

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

	ATDebuggerCmdParser& ATDebuggerCmdParser::operator>>(ATDebuggerCmdQuotedString& nm) {
		for(Args::iterator it(mArgs.begin()), itEnd(mArgs.end()); it != itEnd; ++it) {
			const char *s = *it;

			if (s[0] == '"') {
				++s;
				const char *t = s + strlen(s);

				if (t != s && t[-1] == '"')
					--t;

				nm.mName.assign(s, t);
				nm.mbValid = true;

				mArgs.erase(it);
				return *this;
			}
		}

		if (nm.mbRequired)
			throw MyError("Quoted string parameter required.");

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
	ATDebuggerCmdAddress addr(false, true);
	ATDebuggerCmdQuotedString command(false);
	ATDebuggerCmdParser(argc, argv) >> addr >> command >> 0;

	ATBreakpointManager *bpm = g_debugger.GetBreakpointManager();
	const uint16 v = (uint16)addr;
	const uint32 sysidx = bpm->SetAtPC((uint16)v);
	const uint32 useridx = g_debugger.RegisterSystemBreakpoint(sysidx, NULL, command.IsValid() ? command->c_str() : NULL);

	g_sim.NotifyEvent(kATSimEvent_CPUPCBreakpointsUpdated);
	ATConsolePrintf("Breakpoint %u set at $%04X.\n", useridx, v);
}

void ATConsoleCmdBreakptClear(int argc, const char *const *argv) {
	if (!argc)
		throw MyError("Usage: bc <index> | *");

	if (argc >= 1) {
		ATBreakpointManager *bpm = g_debugger.GetBreakpointManager();

		const char *arg = argv[0];
		if (arg[0] == '*') {
			g_debugger.ClearAllBreakpoints();
			ATConsoleWrite("All breakpoints cleared.\n");
		} else {
			char *argend = (char *)arg;
			unsigned long v = strtoul(arg, &argend, 16);
			sint32 sysidx = g_debugger.LookupUserBreakpoint(v);

			if (*argend || sysidx < 0)
				ATConsoleWrite("Invalid breakpoint index.\n");
			else {
				bpm->Clear((uint32)sysidx);
				g_debugger.UnregisterSystemBreakpoint(sysidx);
				g_sim.NotifyEvent(kATSimEvent_CPUPCBreakpointsUpdated);
				ATConsolePrintf("Breakpoint %u cleared.\n", v);
			}
		}
	}
}

void ATConsoleCmdBreakptAccess(int argc, const char *const *argv) {
	ATDebuggerCmdName cmdAccessMode(true);
	ATDebuggerCmdAddress cmdAddress(false, true, true);
	ATDebuggerCmdLength cmdLength(1, false);

	ATDebuggerCmdParser(argc, argv) >> cmdAccessMode >> cmdAddress >> cmdLength >> 0;

	bool readMode = true;
	if (*cmdAccessMode == "w")
		readMode = false;
	else if (*cmdAccessMode != "r") {
		ATConsoleWrite("Access mode must be 'r' or 'w'.\n");
		return;
	}

	ATBreakpointManager *bpm = g_debugger.GetBreakpointManager();
	if (cmdAddress.IsStar()) {
		ATBreakpointIndices indices;
		g_debugger.GetBreakpointList(indices);

		uint32 cleared = 0;
		while(!indices.empty()) {
			uint32 sysidx = g_debugger.LookupUserBreakpoint(indices.back());
			indices.pop_back();

			ATBreakpointInfo info;
			bpm->GetInfo(sysidx, info);

			if (readMode ? info.mbBreakOnRead : info.mbBreakOnWrite) {
				bpm->Clear(sysidx);
				g_debugger.UnregisterSystemBreakpoint(sysidx);
				++cleared;
			}
		}

		if (readMode) {
			ATConsolePrintf("%u read breakpoint(s) cleared.\n", cleared);
		} else {
			ATConsolePrintf("%u write breakpoint(s) cleared.\n", cleared);
		}
	} else {
		const uint32 address = cmdAddress;
		const uint32 length = cmdLength;

		if (length == 0) {
			ATConsoleWrite("Invalid breakpoint range length.\n");
			return;
		}

		uint32 sysidx;

		const char *modestr = readMode ? "read" : "write";

		if (length > 1) {
			sysidx = bpm->SetAccessRangeBP(address, length, readMode, !readMode);

			const uint32 useridx = g_debugger.RegisterSystemBreakpoint(sysidx);
			ATConsolePrintf("Breakpoint %u set on %s at %04X-%04X.\n", useridx, modestr, address, address + length - 1);
		} else {
			sysidx = bpm->SetAccessBP(address, readMode, !readMode);

			const uint32 useridx = g_debugger.RegisterSystemBreakpoint(sysidx);
			ATConsolePrintf("Breakpoint %u set on %s at %04X.\n", useridx, modestr, address);
		}
	}
}

void ATConsoleCmdBreakptList() {
	ATBreakpointManager *bpm = g_debugger.GetBreakpointManager();

	ATConsoleWrite("Breakpoints:\n");

	vdfastvector<uint32> indices;
	g_debugger.GetBreakpointList(indices);

	VDStringA line;
	for(vdfastvector<uint32>::const_iterator it(indices.begin()), itEnd(indices.end()); it != itEnd; ++it) {
		const uint32 useridx = *it;
		uint32 sysidx = g_debugger.LookupUserBreakpoint(useridx);

		ATBreakpointInfo info;

		VDVERIFY(bpm->GetInfo(sysidx, info));

		line.sprintf("%3u  ", useridx);

		if (info.mbBreakOnPC)
			line += "PC  ";
		else if (info.mbBreakOnRead) {
			if (info.mbBreakOnWrite)
				line += "RW  ";
			else
				line += "R   ";
		} else if (info.mbBreakOnWrite)
			line += "W   ";

		if (info.mLength > 1)
			line.append_sprintf("%s-%s"
				, g_debugger.GetAddressText(info.mAddress, false, false).c_str()
				, g_debugger.GetAddressText(info.mAddress + info.mLength - 1, false, false).c_str()
				);
		else
			line.append_sprintf("%s", g_debugger.GetAddressText(info.mAddress, false, true).c_str());

		ATDebugExpNode *node = g_debugger.GetBreakpointCondition(useridx);

		if (node) {
			VDStringA expr;
			node->ToString(expr);
			line.append_sprintf(" (when %s)", expr.c_str());
		}

		const char *cmd = g_debugger.GetBreakpointCommand(useridx);
		if (cmd) {
			line.append_sprintf(" (run command: \"%s\")", cmd);
		}

		line += '\n';
		ATConsoleWrite(line.c_str());
	}

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

void ATConsoleCmdBreakptExpr(int argc, const char *const *argv) {
	ATDebuggerCmdName expr(true);
	ATDebuggerCmdQuotedString command(false);
	ATDebuggerCmdParser(argc, argv) >> expr >> command >> 0;

	VDStringA s(expr->c_str());

	if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
		s.erase(0, 1);
		s.pop_back();
	}

	vdautoptr<ATDebugExpNode> node;
	
	try {
		node = ATDebuggerParseExpression(s.c_str(), &g_debugger);
	} catch(ATDebuggerExprParseException& ex) {
		ATConsolePrintf("Unable to parse expression: %s\n", ex.c_str());
		return;
	}

	vdautoptr<ATDebugExpNode> extpc;
	vdautoptr<ATDebugExpNode> extread;
	vdautoptr<ATDebugExpNode> extwrite;
	vdautoptr<ATDebugExpNode> rem;

	if (!node->ExtractEqConst(kATDebugExpNodeType_Read, ~extread, ~rem) &&
		!node->ExtractEqConst(kATDebugExpNodeType_Write, ~extwrite, ~rem) &&
		!node->ExtractEqConst(kATDebugExpNodeType_PC, ~extpc, ~rem)) {
		ATConsoleWrite(
			"Cannot find appropriate anchor for breakpoint expression.\n"
			"A breakpoint expression must contain a top-level clause of the form\n"
			"  pc=<constexpr>, read=<constexpr>, or write=<constexpr>.\n"
			);
		return;
	}

	VDStringA condstr;

	if (rem)
		rem->ToString(condstr);

	sint32 addr;
	VDVERIFY((extpc ? extpc : extread ? extread : extwrite)->Evaluate(addr, ATDebugExpEvalContext()));

	addr &= 0xFFFF;

	ATBreakpointManager *bpm = g_debugger.GetBreakpointManager();
	const uint32 sysidx = extpc ? bpm->SetAtPC((uint16)addr)
						: extread ? bpm->SetAccessBP((uint16)addr, true, false)
						: bpm->SetAccessBP((uint16)addr, false, true);
	const uint32 useridx = g_debugger.RegisterSystemBreakpoint(sysidx, rem, command.IsValid() ? command->c_str() : NULL);

	rem.release();

	g_sim.NotifyEvent(kATSimEvent_CPUPCBreakpointsUpdated);

	if (condstr.empty()) {
		if (extpc)
			ATConsolePrintf("Breakpoint %u set at PC=$%04X.\n", useridx, addr);
		else if (extread)
			ATConsolePrintf("Breakpoint %u set on read from $%04X.\n", useridx, addr);
		else if (extwrite)
			ATConsolePrintf("Breakpoint %u set on write to $%04X.\n", useridx, addr);
	} else {
		if (extpc)
			ATConsolePrintf("Breakpoint %u set at PC=$%04X with condition: %s\n", useridx, addr, condstr.c_str());
		else if (extread)
			ATConsolePrintf("Breakpoint %u set on read from $%04X with condition: %s\n", useridx, addr, condstr.c_str());
		else if (extwrite)
			ATConsolePrintf("Breakpoint %u set on write to $%04X with condition: %s\n", useridx, addr, condstr.c_str());
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

		if (g_debugger.LookupLine(addr, false, moduleId, lineInfo) &&
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

void ATConsoleCmdVerifierTargetAdd(int argc, const char *const *argv) {
	ATDebuggerCmdAddress addr(false, true);
	ATDebuggerCmdParser(argc, argv) >> addr >> 0;

	ATCPUVerifier *verifier = g_sim.GetVerifier();

	if (!verifier) {
		ATConsoleWrite("Verifier is not active.\n");
		return;
	}

	verifier->AddAllowedTarget(addr);
}

void ATConsoleCmdVerifierTargetClear(int argc, const char *const *argv) {
	ATDebuggerCmdAddress addr(false, true, true);
	ATDebuggerCmdParser(argc, argv) >> addr >> 0;

	ATCPUVerifier *verifier = g_sim.GetVerifier();

	if (!verifier) {
		ATConsoleWrite("Verifier is not active.\n");
		return;
	}

	if (addr.IsStar()) {
		verifier->RemoveAllowedTargets();
		ATConsoleWrite("All allowed targets cleared.\n");
	} else
		verifier->RemoveAllowedTarget(addr);
}

void ATConsoleCmdVerifierTargetList(int argc, const char *const *argv) {
	ATDebuggerCmdParser(argc, argv) >> 0;

	ATCPUVerifier *verifier = g_sim.GetVerifier();

	if (!verifier) {
		ATConsoleWrite("Verifier is not active.\n");
		return;
	}

	vdfastvector<uint16> targets;
	verifier->GetAllowedTargets(targets);

	ATConsoleWrite("Allowed kernel entry targets:\n");
	for(vdfastvector<uint16>::const_iterator it(targets.begin()), itEnd(targets.end()); it != itEnd; ++it) {
		ATConsolePrintf("    %s\n", g_debugger.GetAddressText(*it, false, true).c_str());
	}
}

void ATConsoleCmdVerifierTargetReset(int argc, const char *const *argv) {
	ATDebuggerCmdParser(argc, argv) >> 0;

	ATCPUVerifier *verifier = g_sim.GetVerifier();

	if (!verifier) {
		ATConsoleWrite("Verifier is not active.\n");
		return;
	}

	verifier->ResetAllowedTargets();

	ATConsoleWrite("Verifier allowed targets list reset.\n");
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
	ATDebuggerCmdAddress addressArg(true, true);
	ATDebuggerCmdNumber lengthArg(true, 1, 16777216);
	ATDebuggerCmdName filename(true);
	ATDebuggerCmdParser(argc, argv) >> filename >> addressArg >> lengthArg >> 0;

	uint32 addr = addressArg;
	uint32 len = lengthArg.GetValue();

	uint32 limit = 0;
	switch(addr & kATAddressSpaceMask) {
		case kATAddressSpace_CPU:
			limit = kATAddressSpace_CPU + 0x1000000;
			break;
		case kATAddressSpace_ANTIC:
			limit = kATAddressSpace_ANTIC + 0x10000;
			break;
		case kATAddressSpace_VBXE:
			limit = kATAddressSpace_VBXE + 0x80000;
			break;
		case kATAddressSpace_PORTB:
			limit = kATAddressSpace_PORTB + 0x100000;
			break;
	}

	if (addr >= limit)
		throw MyError("Invalid start address: %s\n", g_debugger.GetAddressText(addr, false).c_str());

	if (len > limit - addr)
		len = limit - addr;

	FILE *f = fopen(filename->c_str(), "wb");
	if (!f) {
		ATConsolePrintf("Unable to open file for write: %s\n", filename->c_str());
		return;
	}

	uint32 ptr = addr;
	uint32 i = len;
	while(i--)
		putc(g_sim.DebugGlobalReadByte(ptr++), f);

	fclose(f);

	ATConsolePrintf("Wrote %s-%s to %s\n"
		, g_debugger.GetAddressText(addr, false).c_str()
		, g_debugger.GetAddressText(addr + len - 1, false).c_str()
		, filename->c_str());
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

	ATMMUEmulator *mmu = g_sim.GetMMU();
	ATConsolePrintf("Antic bank: $%06X\n", mmu->GetAnticBankBase());
	ATConsolePrintf("CPU bank:   $%06X\n", mmu->GetCPUBankBase());

	for(uint32 cartUnit = 0; cartUnit < 2; ++cartUnit) {
		int cartBank = g_sim.GetCartBank(cartUnit);

		if (cartBank >= 0)
			ATConsolePrintf("Cartridge %u bank: $%02X ($%06X)\n", cartUnit + 1, cartBank, cartBank << 13);
		else
			ATConsolePrintf("Cartridge %u bank: disabled\n", cartUnit + 1);
	}
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

		uint8 iocb[16];
		for(int j=0; j<16; ++j)
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

void ATConsoleCmdMap(int argc, const char *const *argv) {
	ATMemoryManager& memman = *g_sim.GetMemoryManager();

	memman.DumpStatus();
}

void ATConsoleCmdPrintf(int argc, const char *const *argv) {
	const char *s = *argv++;

	if (!s)
		return;

	VDStringA format;
	if (*s == '"') {
		++s;

		const char *end = s + strlen(s);

		if (end != s && end[-1] == '"')
			--end;

		format.assign(s, end);
		s = format.c_str();
	}

	VDStringA line;
	for(;;) {
		char c = *s++;

		if (!c)
			break;

		if (c != '%') {
			line += c;
			continue;
		}

		// check for escape
		c = *s++;
		if (c == '%') {
			line += c;
			continue;
		}

		// parse flags
		bool zeroPad = false;
		bool useSign = false;
		bool positiveSpace = false;
		bool leftAlign = false;
		bool altForm = false;

		for(;;) {
			if (c == '0')
				zeroPad = true;
			else if (c == '#')
				altForm = true;
			else if (c == '+')
				useSign = true;
			else if (c == ' ')
				positiveSpace = true;
			else if (c == '-')
				leftAlign = true;
			else
				break;

			c = *s++;
		}

		// parse width
		uint32 width = 0;
		if (c >= '1' && c <= '9') {
			do {
				width = (width * 10) + (unsigned)(c - '0');

				if (width >= 100)
					width = 100;

				c = *s++;
			} while(c >= '0' && c <= '9');
		}

		// check for precision
		int precision = -1;
		if (c == '.') {
			precision = 0;

			for(;;) {
				c = *s++;

				if (c < '0' || c > '9')
					break;

				precision = (precision * 10) + (int)(c - '0');
				if (precision >= 100)
					precision = 100;
			}
		}

		int numericPrecision = precision >= 0 ? precision : 0;

		// evaluate value
		sint32 value = 0;

		const char *arg = *argv++;
		if (!arg) {
			line += "<error: value missing>";
			break;
		}

		vdautoptr<ATDebugExpNode> node;

		try {
			node = ATDebuggerParseExpression(arg, &g_debugger);
		} catch(const ATDebuggerExprParseException& ex) {
			line.append_sprintf("<error: %s>", ex.c_str());
			break;
		}

		ATDebugExpEvalContext ctx;
		ctx.mpAntic = &g_sim.GetAntic();
		ctx.mpCPU = &g_sim.GetCPU();
		ctx.mpMemory = &g_sim.GetCPUMemory();
		if (!node->Evaluate(value, ctx)) {
			line.append("<evaluation error>");
			break;
		}

		// check for width modifier and truncate value appropriately
		if (c == 'h') {
			c = *s++;

			if (c == 'h') {
				value &= 0xff;
				c = *s++;
			} else {
				value &= 0xffff;
			}
		} else if (c == 'l') {
			c = *s++;
		}

		// process format character
		if (!c)
			break;

		VDStringA::size_type basePos = line.size();

		switch(c) {
			case 'b':	// binary
				{
					// left align value
					uint32 digits = 32;

					if (!value)
						digits = 1;
					else {
						while(value >= 0) {
							value += value;
							--digits;
						}
					}

					// left-pad if necessary
					uint32 natWidth = digits;
					uint32 precPadWidth = 0;

					if (precision >= 0 && digits < (uint32)precision) {
						precPadWidth = (uint32)precision - digits;
						natWidth = precision;
					}

					char padChar = (zeroPad && precision < 0) ? '0' : ' ';
					uint32 padWidth = (natWidth < width) ? width - natWidth : 0;

					if (padWidth && !leftAlign) {
						do {
							line += padChar;
						} while(--padWidth);
					}

					while(precPadWidth--)
						line += '0';

					// shift out bits
					while(digits) {
						line += (char)('0' - (value >> 31));
						value += value;
						--digits;
					}

					if (padWidth) {
						do {
							line += padChar;
						} while(--padWidth);
					}
				}
				break;

			case 'd':	// signed decimal
			case 'i':	// signed decimal
				{
					// left align value
					uint32 uvalue = (uint32)(value < 0 ? -value : value);
					uint32 digits;

					if (!uvalue)
						digits = 1;
					else if (uvalue >= 1000000000)
						digits = 10;
					else {
						digits = 9;

						while(uvalue < 100000000) {
							uvalue *= 10;
							--digits;
						}
					}

					// left-pad if necessary
					uint32 natWidth = digits;
					uint32 precPadWidth = 0;

					if (precision >= 0 && digits < (uint32)precision) {
						natWidth = precision;
						precPadWidth = (uint32)precision - digits;
					}

					if (useSign || positiveSpace || value < 0)
						++natWidth;

					char padChar = (zeroPad && precision < 0) ? '0' : ' ';
					uint32 padWidth = (natWidth < width) ? width - natWidth : 0;

					if (padWidth && !leftAlign) {
						do {
							line += padChar;
						} while(--padWidth);
					}

					if (value < 0)
						line += '-';
					else if (useSign)
						line += '+';
					else if (positiveSpace)
						line += ' ';

					while(precPadWidth--)
						line += '0';

					// shift out digits
					if (uvalue >= 1000000000) {
						line += uvalue / 1000000000;
						uvalue %= 1000000000;
					}

					while(digits) {
						line += (char)((uvalue / 100000000) + '0');
						uvalue %= 100000000;
						uvalue *= 10;
						--digits;
					}

					if (padWidth) {
						do {
							line += padChar;
						} while(--padWidth);
					}
				}
				break;

			case 'u':	// unsigned decimal
				{
					// left align value
					uint32 uvalue = (uint32)value;
					uint32 digits;

					if (!uvalue)
						digits = 1;
					else if (uvalue >= 1000000000)
						digits = 10;
					else {
						digits = 9;

						while(uvalue < 100000000) {
							uvalue *= 10;
							--digits;
						}
					}

					// left-pad if necessary
					uint32 natWidth = digits;
					uint32 precPadWidth = 0;

					if (precision >= 0 && digits < (uint32)precision) {
						precPadWidth = (uint32)precision - digits;
						natWidth = precision;
					}

					char padChar = (zeroPad && precision < 0) ? '0' : ' ';
					uint32 padWidth = (natWidth < width) ? width - natWidth : 0;

					if (padWidth && !leftAlign) {
						do {
							line += padChar;
						} while(--padWidth);
					}

					while(precPadWidth--)
						line += '0';

					// shift out digits
					if (uvalue >= 1000000000) {
						line += uvalue / 1000000000;
						uvalue %= 1000000000;
					}

					while(digits) {
						line += (char)((uvalue / 100000000) + '0');
						uvalue %= 100000000;
						uvalue *= 10;
						--digits;
					}

					if (padWidth) {
						do {
							line += padChar;
						} while(--padWidth);
					}
				}
				break;

			case 'x':	// hexadecimal lowercase
			case 'X':	// hexadecimal uppercase
				{
					// left align value
					uint32 uvalue = (uint32)value;
					uint32 digits = 8;

					if (!uvalue)
						digits = 1;
					else {
						while(!(uvalue & 0xf0000000)) {
							uvalue <<= 4;

							--digits;
						}
					}

					// left-pad if necessary
					uint32 natWidth = digits;
					uint32 precPadWidth = 0;

					if (precision >= 0 && digits < (uint32)precision) {
						precPadWidth = (uint32)precision - digits;
						natWidth = precision;
					}

					if (altForm)
						natWidth += 2;

					char padChar = (zeroPad && precision < 0) ? '0' : ' ';
					uint32 padWidth = (natWidth < width) ? width - natWidth : 0;

					if (padWidth && !leftAlign) {
						do {
							line += padChar;
						} while(--padWidth);
					}

					if (altForm) {
						line += '0';
						line += c;
					}

					while(precPadWidth--)
						line += '0';

					// shift out digits
					static const char kHexTableLo[16] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
					static const char kHexTableHi[16] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
					const char *hextab = (c == 'X') ? kHexTableHi : kHexTableLo;

					while(digits) {
						line += hextab[uvalue >> 28];
						uvalue <<= 4;
						--digits;
					}

					if (padWidth) {
						do {
							line += padChar;
						} while(--padWidth);
					}
				}
				break;

			default:
				line += "<invalid format mode>";
				continue;
		}
	}

	line += '\n';
	ATConsoleWrite(line.c_str());
}

void ATConsoleCmdDumpSnap(int argc, const char *const *argv) {
	ATDebuggerCmdName name(true);
	ATDebuggerCmdParser(argc, argv) >> name >> 0;

	vdfastvector<uint8> buf;
	ATLoadMiscResource(IDR_DISKLOADER128, buf);

	vdfastvector<uint8> rawMemory(0x10000);
	uint8 *mem = rawMemory.data();
	memcpy(mem, g_sim.GetRawMemory(), 0x10000);

	ATCPUEmulator& cpu = g_sim.GetCPU();
	uint8 *p = buf.data();

	uint8 *gtiabase = p + p[0x214] + 0x100;
	uint8 *pokeybase = p + p[0x214] + 0x11e;
	uint8 *anticbase = p + p[0x214] + 0x127;

	ATGTIARegisterState gtstate;
	g_sim.GetGTIA().GetRegisterState(gtstate);

	memcpy(gtiabase, gtstate.mReg, 30);

	ATAnticRegisterState anstate;
	g_sim.GetAntic().GetRegisterState(anstate);

	anticbase[0] = anstate.mCHACTL;
	anticbase[1] = anstate.mDLISTL;
	anticbase[2] = anstate.mDLISTH;
	anticbase[3] = anstate.mHSCROL;
	anticbase[4] = anstate.mVSCROL;
	anticbase[5] = 0;
	anticbase[6] = anstate.mPMBASE;
	anticbase[7] = 0;
	anticbase[8] = anstate.mCHBASE;

	ATPokeyRegisterState postate;
	g_sim.GetPokey().GetRegisterState(postate);

	memcpy(pokeybase, postate.mReg, 9);

	uint8 regS = cpu.GetS();
	const uint8 stubSize = p[0x0215];
	const uint8 stubOffset = (uint8)(regS - 2) < stubSize ? 0x100 - stubSize : regS - 2 - stubSize;

	// Write return address and flags for RTI. Note that we may possibly wrap around the
	// stack doing this.
	uint32 pc = cpu.GetInsnPC();
	mem[0x100 + regS] = (uint8)(pc >> 8);
	--regS;
	mem[0x100 + regS] = (uint8)pc;
	--regS;
	mem[0x100 + regS] = cpu.GetP();
	--regS;

	const uint8 pbctl = g_sim.GetPortBControl();
	const uint8 pbout = g_sim.GetPortBOutputs();
	const uint8 pbddr = g_sim.GetPortBDirections();

	const uint8 dataToInject[]={
		cpu.GetA(),
		cpu.GetX(),
		cpu.GetY(),
		regS,
		(uint8)stubOffset,
		pbctl ^ 0x04,
		pbctl & 0x04 ? pbddr : pbout,
		pbctl & 0x04 ? pbout : pbddr,
		anstate.mDMACTL,
		anstate.mNMIEN,
	};

	for(uint8 i = 0; i < (uint8)sizeof(dataToInject); ++i)
		buf[VDReadUnalignedLEU16(p + 0x200 + i*2) % buf.size()] = dataToInject[i];

	memcpy(mem + 0x100 + stubOffset, p + 0x0216, stubSize);

	VDFile f(name->c_str(), nsVDFile::kWrite | nsVDFile::kDenyRead | nsVDFile::kCreateAlways | nsVDFile::kSequential);

	const uint8 kATRHeader[]={
		0x96, 0x02,
		0xA0, 0x0F,
		0x80, 0x00,
		0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};

	f.write(kATRHeader, 16);
	f.write(p, 0x200);
	f.write(mem + 0xC000, 0x1000);
	f.write(mem + 0xD800, 0x2800);
	f.write(mem + 0x0A00, 0xB600);
	f.write(mem + 0x0000, 0x0A00);

	ATConsolePrintf("Booter written to: %s\n", name->c_str());
}

void ATConsoleCmdEval(const char *s) {
	if (!s) {
		ATConsoleWrite("Missing expression. (Use .help if you want command help.)\n");
		return;
	}

	ATDebugExpEvalContext ctx;
	ctx.mpCPU = &g_sim.GetCPU();
	ctx.mpMemory = &g_sim.GetCPUMemory();
	ctx.mpAntic = &g_sim.GetAntic();

	vdautoptr<ATDebugExpNode> node;

	try {
		node = ATDebuggerParseExpression(s, &g_debugger);
	} catch(ATDebuggerExprParseException& ex) {
		ATConsolePrintf("Unable to parse expression: %s\n", ex.c_str());
		return;
	}

	ATConsolePrintf("%s = ", s);

	sint32 result;
	if (!node)
		ATConsoleWrite("(parse error)\n");
	else if (!node->Evaluate(result, ctx))
		ATConsoleWrite("(evaluation error)\n");
	else
		ATConsolePrintf("%d ($%0*X)\n", result, (uint32)result >= 0x10000 ? 8 : 4, result);
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

	if (cmd && !anyout) {
		ATConsoleWrite("\n");
		ATConsolePrintf("  No detailed help available for command: %s.\n", cmd);
	}
}

void ATConsoleCmdDumpSIO(int argc, const char *const *argv) {
	ATDebuggerCmdParser(argc, argv) >> 0;

	ATKernelDatabase kdb(&g_sim.GetCPUMemory());
	ATConsolePrintf("DDEVIC    Device ID   = $%02x\n", (int)kdb.DDEVIC);
	ATConsolePrintf("DUNIT     Device unit = $%02x\n", (int)kdb.DUNIT);
	ATConsolePrintf("DCOMND    Command     = $%02x\n", (int)kdb.DCOMND);
	ATConsolePrintf("DSTATS    Status      = $%02x\n", (int)kdb.DSTATS);
	ATConsolePrintf("DBUFHI/LO Buffer      = $%04x\n", kdb.DBUFHI * 256 + kdb.DBUFLO);
	ATConsolePrintf("DTIMLO    Timeout     = $%02x\n", (int)kdb.DTIMLO);
	ATConsolePrintf("DBYTHI/LO Length      = $%04x\n", kdb.DBYTHI * 256 + kdb.DBYTLO);
	ATConsolePrintf("DAUXHI/LO Sector      = $%04x\n", kdb.DAUX2 * 256 + kdb.DAUX1);
}

void ATConsoleCmdDumpPCLink(int argc, const char *const *argv) {
	ATDebuggerCmdParser(argc, argv) >> 0;

	IATPCLinkDevice *pclink = g_sim.GetPCLink();
	if (!pclink)
		ATConsoleWrite("PCLink is not enabled.\n");
	else
		pclink->DumpStatus();
}

void ATConsoleCmdSDXLoadSymbols(int argc, const char *const *argv) {
	ATDebuggerCmdAddress addr(false, false);
	ATDebuggerCmdParser(argc, argv) >> addr >> 0;

	uint16 linkAddr = addr.IsValid() ? addr : 0xd4e;

	linkAddr = g_sim.DebugReadWord(linkAddr);

	uint32 prevSdxModuleId = g_debugger.GetCustomModuleIdByName("sdx");
	if (prevSdxModuleId)
		g_debugger.UnloadSymbols(prevSdxModuleId);

	uint32 sdxModuleId = g_debugger.AddCustomModule("sdx");
	uint32 found = 0;

	vdhashset<uint16> foundAddresses;
	while(linkAddr >= 0x0200 && linkAddr < 0xfffa - 13) {
		if (!foundAddresses.insert(linkAddr).second)
			break;

		uint8 symbol[13];
		for(int i=0; i<13; ++i)
			symbol[i] = g_sim.DebugReadByte(linkAddr + i);

		// validate name
		for(int i=0; i<8; ++i) {
			uint8 c = symbol[2 + i];

			if (c < 0x20 || c >= 0x7f)
				goto stop;
		}

		// parse out name
		const char *s = (const char *)(symbol + 2);
		const char *t = (const char *)(symbol + 10);

		while(t > s && t[-1] == ' ')
			--t;

		if (t == s)
			goto stop;

		// add symbol
		VDStringA name(s, t);
		g_debugger.AddCustomSymbol(VDReadUnalignedLEU16(symbol + 11), 1, name.c_str(), kATSymbol_Any, sdxModuleId);
		++found;

		linkAddr = VDReadUnalignedLEU16(symbol);
	}

stop:
	ATConsolePrintf("%u symbols added.\n", found);
}

void ATConsoleExecuteCommand(const char *s, bool echo) {
	IATDebuggerActiveCommand *cmd = g_debugger.GetActiveCommand();

	if (cmd) {
		g_debugger.ExecuteCommand(s);
		return;
	}

	if (!*s)
		return;

	if (echo) {
		ATConsolePrintf("%s> ", g_debugger.GetPrompt());
		ATConsolePrintf("%s\n", s);
	}

	vdfastvector<char> tempstr;
	vdfastvector<const char *> argptrs;

	int argc = ATDebuggerParseArgv(s, tempstr, argptrs);

	const char **argv = argptrs.data();

	if (argc) {
		const char *cmd = argv[0];
		const char *argstart = argc > 1 ? s + (argv[1] - tempstr.data()) : NULL;

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
		} else if (!strcmp(cmd, "bx")) {
			ATConsoleCmdBreakptExpr(argc-1, argv+1);
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
		} else if (!strcmp(cmd, "vta")) {
			ATConsoleCmdVerifierTargetAdd(argc-1, argv+1);
		} else if (!strcmp(cmd, "vtc")) {
			ATConsoleCmdVerifierTargetClear(argc-1, argv+1);
		} else if (!strcmp(cmd, "vtl")) {
			ATConsoleCmdVerifierTargetList(argc-1, argv+1);
		} else if (!strcmp(cmd, "vtr")) {
			ATConsoleCmdVerifierTargetReset(argc-1, argv+1);
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
		} else if (!strcmp(cmd, ".map")) {
			ATConsoleCmdMap(argc-1, argv+1);
		} else if (!strcmp(cmd, ".printf")) {
			ATConsoleCmdPrintf(argc-1, argv+1);
		} else if (!strcmp(cmd, ".dumpsnap")) {
			ATConsoleCmdDumpSnap(argc-1, argv+1);
		} else if (!strcmp(cmd, ".help")) {
			ATConsoleCmdDumpHelp(argc-1, argv+1);
		} else if (!strcmp(cmd, ".sio")) {
			ATConsoleCmdDumpSIO(argc-1, argv+1);
		} else if (!strcmp(cmd, ".pclink")) {
			ATConsoleCmdDumpPCLink(argc-1, argv+1);
		} else if (!strcmp(cmd, ".sdx_loadsyms")) {
			ATConsoleCmdSDXLoadSymbols(argc-1, argv+1);
		} else if (!strcmp(cmd, "?")) {
			ATConsoleCmdEval(argstart);
		} else {
			ATConsoleWrite("Unrecognized command. \".help\" for help\n");
		}
	}
}
