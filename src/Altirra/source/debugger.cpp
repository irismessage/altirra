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
#include <vd2/system/math.h>
#include <vd2/system/strutil.h>
#include <vd2/system/vdstl_hashset.h>
#include "console.h"
#include "cpu.h"
#include "cpuheatmap.h"
#include "simulator.h"
#include "disasm.h"
#include "debugger.h"
#include "debuggerexp.h"
#include "debuggerlog.h"
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
#include "ide.h"
#include "side.h"
#include "cassette.h"
#include "cassetteimage.h"
#include "slightsid.h"
#include "covox.h"
#include "ultimate1mb.h"
#include "pbi.h"

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

		bool allowEscaping = false;
		if (*t == '\\' && t[1] == '"') {
			++t;
			allowEscaping = true;
		}

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

				if (c == '\\' && allowEscaping) {
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

void ATDebuggerSerializeArgv(VDStringA& dst, int argc, const char *const *argv) {
	for(int i=0; i<argc; ++i) {
		if (i)
			dst += ' ';

		const char *s = argv[i];

		if (*s == '"') {
			++s;
			bool requiresEscaping = false;

			const char *end;
			for(end = s; *end; ++end) {
				unsigned char c = *end;

				if (c == '"' && !end[1])
					break;

				if (c == '"' || c == '\\')
					requiresEscaping = true;
			}

			if (requiresEscaping)
				dst += '\\';

			dst += '"';
			for(const char *t = s; t != end; ++t) {
				unsigned char c = *t;

				if (c == '"' || c == '\\')
					dst += '\\';

				dst += c;
			}
			dst += '"';
		} else {
			dst += s;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////

class ATDebugger : public IATSimulatorCallback, public IATDebugger, public IATDebuggerSymbolLookup {
public:
	ATDebugger();
	~ATDebugger();

	ATBreakpointManager *GetBreakpointManager() { return mpBkptManager; }

	bool IsRunning() const;
	bool AreCommandsQueued() const;
	bool IsSourceModeEnabled() const { return mbSourceMode; }

	const ATDebuggerExprParseOpts& GetExprOpts() const { return mExprOpts; }
	void SetExprOpts(const ATDebuggerExprParseOpts& opts) { mExprOpts = opts; }

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
	bool IsDeferredBreakpointSet(const char *fn, uint32 line);
	bool ClearUserBreakpoint(uint32 useridx);
	void ClearAllBreakpoints();
	void ToggleBreakpoint(uint16 addr);
	void ToggleAccessBreakpoint(uint16 addr, bool write);
	void ToggleSourceBreakpoint(const char *fn, uint32 line);
	sint32 LookupUserBreakpoint(uint32 useridx) const;
	sint32 LookupUserBreakpointByAddr(uint16 address) const;
	uint32 SetSourceBreakpoint(const char *fn, uint32 line, ATDebugExpNode *condexp, const char *command, bool continueExecution = false);
	uint32 SetConditionalBreakpoint(ATDebugExpNode *exp, const char *command = NULL, bool continueExecution = false);
	uint32 RegisterSystemBreakpoint(uint32 sysidx, ATDebugExpNode *condexp = NULL, const char *command = NULL, bool continueExecution = false);
	void UnregisterSystemBreakpoint(uint32 sysidx);
	bool GetBreakpointInfo(uint32 useridx, ATBreakpointInfo& info) const;
	void GetBreakpointList(vdfastvector<uint32>& bps) const;
	ATDebugExpNode *GetBreakpointCondition(uint32 useridx) const;
	const char *GetBreakpointCommand(uint32 useridx) const;
	bool GetBreakpointSourceLocation(uint32 useridx, VDStringA& file, uint32& line) const;

	bool IsBreakOnEXERunAddrEnabled() const { return mbBreakOnEXERunAddr; }
	void SetBreakOnEXERunAddrEnabled(bool en) { mbBreakOnEXERunAddr = en; }

	int AddWatch(uint32 address, int length);
	int AddWatchExpr(ATDebugExpNode *expr);
	bool ClearWatch(int idx);
	void ClearAllWatches();
	bool GetWatchInfo(int idx, ATDebuggerWatchInfo& info);

	void ListModules();
	void ReloadModules();
	void UnloadSymbolsByIndex(uint32 index);

	void DumpCIOParameters();

	bool IsCIOTracingEnabled() const { return mSysBPTraceCIO > 0; }

	void SetCIOTracingEnabled(bool enabled);

	// symbol handling
	uint32 AddModule(uint32 base, uint32 size, IATSymbolStore *symbolStore, const char *name, const wchar_t *path);
	void RemoveModule(uint32 base, uint32 size, IATSymbolStore *symbolStore);

	void AddClient(IATDebuggerClient *client, bool requestUpdate);
	void RemoveClient(IATDebuggerClient *client);
	void RequestClientUpdate(IATDebuggerClient *client);

	uint32 LoadSymbols(const wchar_t *fileName, bool processDirectives);
	void UnloadSymbols(uint32 moduleId);
	void ClearSymbolDirectives(uint32 moduleId);
	void ProcessSymbolDirectives(uint32 id);

	sint32 ResolveSourceLocation(const char *fn, uint32 line);
	sint32 ResolveSymbol(const char *s, bool allowGlobal = false, bool allowShortBase = true);
	uint32 ResolveSymbolThrow(const char *s, bool allowGlobal = false, bool allowShortBase = true);

	uint32 AddCustomModule(const char *name);
	uint32 GetCustomModuleIdByName(const char *name);
	void AddCustomSymbol(uint32 address, uint32 len, const char *name, uint32 rwxmode, uint32 moduleId = 0);
	void RemoveCustomSymbol(uint32 address);
	void LoadCustomSymbols(const wchar_t *filename);
	void SaveCustomSymbols(const wchar_t *filename);

	VDStringA GetAddressText(uint32 globalAddr, bool useHexSpecifier, bool addSymbolInfo = false);

	ATDebugExpEvalContext GetEvalContext() const;

	void GetDirtyStorage(vdfastvector<ATDebuggerStorageId>& ids) const;

	void QueueBatchFile(const wchar_t *s);

	void QueueCommand(const char *s, bool echo) {
		mCommandQueue.push_back(VDStringA());
		VDStringA& t = mCommandQueue.back();
		
		t.push_back(echo ? 'e' : ' ');
		t += s;
	}

	void QueueCommandFront(const char *s, bool echo) {
		mCommandQueue.push_front(VDStringA());
		VDStringA& t = mCommandQueue.front();
		
		t.push_back(echo ? 'e' : ' ');
		t += s;
	}

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
		else {
			cmd = mActiveCommands.back();

			if (cmd->IsBusy())
				SetPrompt("BUSY");
			else
				SetPrompt(cmd->GetPrompt());
		}
	}

	IATDebuggerActiveCommand *GetActiveCommand() {
		return mActiveCommands.empty() ? NULL : mActiveCommands.back();
	}

	void StartActiveCommand(IATDebuggerActiveCommand *cmd) {
		cmd->AddRef();
		mActiveCommands.push_back(cmd);

		cmd->BeginCommand(this);

		if (cmd->IsBusy())
			SetPrompt("BUSY");
		else
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

	const char *GetRepeatCommand() const { return mRepeatCommand.c_str(); }
	void SetRepeatCommand(const char *s) { mRepeatCommand = s; }

	uint32 GetContinuationAddress() const { return mContinuationAddress; }
	void SetContinuationAddress(uint32 addr) { mContinuationAddress = addr; }

	bool IsCommandAliasPresent(const char *alias) const;
	bool MatchCommandAlias(const char *alias, const char *const *argv, int argc, vdfastvector<char>& tempstr, vdfastvector<const char *>& argptrs) const;
	const char *GetCommandAlias(const char *alias, const char *args) const;
	void SetCommandAlias(const char *alias, const char *args, const char *command);
	void ListCommandAliases();
	void ClearCommandAliases();

	void OnExeQueueCmd(bool onrun, const char *s);
	void OnExeClear();
	bool OnExeGetCmd(bool onrun, int index, VDStringA& s);

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
		VDStringW	mPath;
		vdfastvector<uint16> mSilentlyIgnoredFiles;
	};

	void ResolveDeferredBreakpoints();
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
	bool	mbRunning;
	bool	mbBreakOnEXERunAddr;
	bool	mbClientUpdatePending;
	bool	mbClientLastRunState;
	bool	mbSymbolUpdatePending;

	VDStringA	mRepeatCommand;
	uint32	mContinuationAddress;

	uint32	mExprAddress;
	uint8	mExprValue;

	typedef std::list<Module> Modules; 
	Modules		mModules;

	typedef std::vector<IATDebuggerClient *> Clients;
	Clients mClients;
	int mClientsBusy;
	bool mbClientsChanged;

	ATDebuggerExprParseOpts mExprOpts;

	uint32	mWatchAddress[8];
	int		mWatchLength[8];
	vdautoptr<ATDebugExpNode> mpWatchExpr[8];

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
		VDStringA mSource;
		uint32 mSourceLine;
		bool mbContinueExecution;
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

	struct AliasSorter;
	typedef vdvector<std::pair<VDStringA, VDStringA> > AliasList;

	typedef vdhashmap<VDStringA, AliasList, vdhash<VDStringA>, vdstringpred> Aliases;
	Aliases mAliases;

	typedef std::deque<VDStringA> OnExeCmds;
	OnExeCmds mOnExeCmds[2];
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
	, mbRunning(true)
	, mExprAddress(0)
	, mExprValue(0)
	, mbClientUpdatePending(false)
	, mbClientLastRunState(false)
	, mbSymbolUpdatePending(false)
	, mContinuationAddress(0)
	, mClientsBusy(0)
	, mbClientsChanged(false)
	, mpBkptManager(NULL)

{
	SetPromptDefault();

	for(int i=0; i<8; ++i)
		mWatchLength[i] = -1;

	mExprOpts.mbDefaultHex = false;
	mExprOpts.mbAllowUntaggedHex = true;
}

ATDebugger::~ATDebugger() {
	TerminateActiveCommands();
}

bool ATDebugger::IsRunning() const {
	return g_sim.IsRunning();
}

bool ATDebugger::AreCommandsQueued() const {
	return !mActiveCommands.empty() || !mCommandQueue.empty();
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
	mbRunning = true;

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
	if (g_sim.IsRunning())
		return false;

	if (!mActiveCommands.empty()) {
		IATDebuggerActiveCommand *acmd = mActiveCommands.back();

		if (acmd->IsBusy()) {
			if (!acmd->ProcessSubCommand(NULL)) {
				acmd->EndCommand();
				acmd->Release();
				mActiveCommands.pop_back();

				if (mActiveCommands.empty())
					SetPromptDefault();
				else {
					acmd = mActiveCommands.back();

					if (acmd->IsBusy())
						SetPrompt("BUSY");
					else
						SetPrompt(acmd->GetPrompt());
				}
			}

			return true;
		}

		if (mCommandQueue.empty())
			return false;
	} else {
		if (mCommandQueue.empty()) {
			if (mbSymbolUpdatePending) {
				mbSymbolUpdatePending = false;

				NotifyEvent(kATDebugEvent_SymbolsChanged);
			}

			if (mbClientUpdatePending)
				UpdateClientSystemState();

			return false;
		}
	}

	VDStringA s;
	s.swap(mCommandQueue.front());
	mCommandQueue.pop_front();

	try {
		const char *t = s.c_str();
		VDASSERT(*t);
		ATConsoleExecuteCommand(t + 1, t[0] == 'e');
	} catch(const MyError& e) {
		ATConsolePrintf("%s\n", e.gets());
	}

	if (mCommandQueue.empty()) {
		if (mbRunning) {
			g_sim.Resume();
			return true;
		}
	}

	return true;
}

void ATDebugger::Break() {
	if (g_sim.IsRunning()) {
		g_sim.Suspend();
		mbRunning = false;

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
	mbRunning = true;

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
	mbRunning = true;

	if (!mbClientLastRunState)
		UpdateClientSystemState();
}

bool ATDebugger::IsDeferredBreakpointSet(const char *fn, uint32 line) {
	if (!*fn)
		return false;

	UserBPs::iterator it(mUserBPs.begin()), itEnd(mUserBPs.end());
	for(; it != itEnd; ++it) {
		UserBP& ubp = *it;

		if (ubp.mSourceLine == line && ubp.mSource == fn)
			return true;
	}

	return false;
}

bool ATDebugger::ClearUserBreakpoint(uint32 useridx) {
	if (useridx >= mUserBPs.size())
		return false;

	UserBP& bp = mUserBPs[useridx];
	if (bp.mSysBP < 0)
		return false;

	bp.mModuleId = 0;

	if (bp.mpCondition) {
		delete bp.mpCondition;
		bp.mpCondition = NULL;
	}

	bp.mSource.clear();

	if ((sint32)bp.mSysBP > 0) {
		mpBkptManager->Clear(bp.mSysBP);

		SysBPToUserBPMap::iterator it(mSysBPToUserBPMap.find(bp.mSysBP));
		if (it != mSysBPToUserBPMap.end())
			mSysBPToUserBPMap.erase(it);
	}

	bp.mSysBP = (uint32)-1;
	return true;
}

void ATDebugger::ClearAllBreakpoints() {
	ClearAllBreakpoints(false);
}

void ATDebugger::ClearAllBreakpoints(bool notify) {
	uint32 n = mUserBPs.size();

	for(uint32 i=0; i<n; ++i) {
		UserBP& bp = mUserBPs[i];

		if (bp.mSysBP != (uint32)-1)
			ClearUserBreakpoint(i);
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
		ClearUserBreakpoint(useridx);
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
		ClearUserBreakpoint(useridx);
	} else {
		sysidx = mpBkptManager->SetAccessBP(addr, !write, write);
		RegisterSystemBreakpoint(sysidx);
	}

	g_sim.NotifyEvent(kATSimEvent_CPUPCBreakpointsUpdated);
}

void ATDebugger::ToggleSourceBreakpoint(const char *fn, uint32 line) {
	UserBPs::iterator it(mUserBPs.begin()), itEnd(mUserBPs.end());
	for(; it != itEnd; ++it) {
		UserBP& ubp = *it;

		if (ubp.mSourceLine == line && ubp.mSource == fn) {
			ClearUserBreakpoint((uint32)(it - mUserBPs.begin()));
			g_sim.NotifyEvent(kATSimEvent_CPUPCBreakpointsUpdated);
			return;
		}
	}

	sint32 addr = ResolveSourceLocation(fn, line);
	if (addr >= 0) {
		sint32 useridx = LookupUserBreakpointByAddr(addr);
		if (useridx >= 0) {
			ClearUserBreakpoint(useridx);
			g_sim.NotifyEvent(kATSimEvent_CPUPCBreakpointsUpdated);
			return;
		}
	}

	SetSourceBreakpoint(fn, line, NULL, NULL);
	g_sim.NotifyEvent(kATSimEvent_CPUPCBreakpointsUpdated);
}

sint32 ATDebugger::LookupUserBreakpoint(uint32 useridx) const {
	if (useridx >= mUserBPs.size())
		return -1;

	return mUserBPs[useridx].mSysBP;
}

sint32 ATDebugger::LookupUserBreakpointByAddr(uint16 address) const {
	ATBreakpointIndices indices;
	mpBkptManager->GetAtPC(address, indices);

	while(!indices.empty()) {
		SysBPToUserBPMap::const_iterator it(mSysBPToUserBPMap.find(indices.back()));

		if (it != mSysBPToUserBPMap.end())
			return it->second;

		indices.pop_back();
	}

	return -1;
}

uint32 ATDebugger::SetSourceBreakpoint(const char *fn, uint32 line, ATDebugExpNode *condexp, const char *command, bool continueExecution) {
	sint32 address = ResolveSourceLocation(fn, line);
	uint32 sysidx = 0;

	if (address >= 0)
		sysidx = mpBkptManager->SetAtPC(address);

	UserBPs::iterator it(std::find_if(mUserBPs.begin(), mUserBPs.end(), UserBPFreePred()));
	uint32 useridx = it - mUserBPs.begin();

	if (it == mUserBPs.end())
		mUserBPs.push_back();

	UserBP& ubp = mUserBPs[useridx];
	ubp.mSysBP = sysidx;
	ubp.mpCondition = condexp;
	ubp.mCommand = command ? command : "";
	ubp.mModuleId = 0;
	ubp.mSource = fn;
	ubp.mSourceLine = line;
	ubp.mbContinueExecution = continueExecution;

	if (sysidx)
		mSysBPToUserBPMap[sysidx] = useridx;

	return useridx;
}

uint32 ATDebugger::SetConditionalBreakpoint(ATDebugExpNode *exp0, const char *command, bool continueExecution) {
	vdautoptr<ATDebugExpNode> exp(exp0);

	if (exp->mType == kATDebugExpNodeType_Const) {
		sint32 v;

		if (exp->Evaluate(v, ATDebugExpEvalContext())) {
			VDString s;

			exp->ToString(s);

			if (v)
				throw MyError("Error: Condition '%s' is always true.", s.c_str());
			else
				throw MyError("Error: Condition '%s' is always false.", s.c_str());
		}
	}

	vdautoptr<ATDebugExpNode> extpc;
	vdautoptr<ATDebugExpNode> extread;
	vdautoptr<ATDebugExpNode> extwrite;
	vdautoptr<ATDebugExpNode> rem;
	vdautoptr<ATDebugExpNode> rangelo;
	vdautoptr<ATDebugExpNode> rangehi;
	bool isrange = false;
	bool israngewrite;
	sint32 rangeloaddr;
	sint32 rangehiaddr;
	sint32 addr;

	if (!exp->ExtractEqConst(kATDebugExpNodeType_Read, ~extread, ~rem) &&
		!exp->ExtractEqConst(kATDebugExpNodeType_Write, ~extwrite, ~rem) &&
		!exp->ExtractEqConst(kATDebugExpNodeType_PC, ~extpc, ~rem))
	{
		// Hmm. Okay, let's see if we can extract a range breakpoint.
		vdautoptr<ATDebugExpNode> temprem;

		ATDebugExpNodeType oplo;
		ATDebugExpNodeType ophi;

		if (exp->ExtractRelConst(kATDebugExpNodeType_Read, ~rangelo, ~temprem, &oplo) &&
			temprem->ExtractRelConst(kATDebugExpNodeType_Read, ~rangehi, ~rem, &ophi))
		{
			isrange = true;
			israngewrite = false;
		}
		else if (exp->ExtractRelConst(kATDebugExpNodeType_Write, ~rangelo, ~temprem, &oplo) &&
			temprem->ExtractRelConst(kATDebugExpNodeType_Write, ~rangehi, ~rem, &ophi))
		{
			isrange = true;
			israngewrite = true;
		}
		else
		{
			throw MyError(
				"Cannot find appropriate anchor for breakpoint expression.\n"
				"A breakpoint expression must contain a top-level clause of the form\n"
				"  pc=<constexpr>, read=<constexpr>, or write=<constexpr>.\n"
				);
		}

		// One of the ranges should be LT/LE and the other one GT/GE; validate this and swap around
		// the terms if needed.
		bool validRange = true;

		if (oplo == kATDebugExpNodeType_LT || oplo == kATDebugExpNodeType_LE) {
			rangelo.swap(rangehi);
			std::swap(oplo, ophi);
		}

		VDVERIFY(rangelo->Evaluate(rangeloaddr, ATDebugExpEvalContext()));
		VDVERIFY(rangehi->Evaluate(rangehiaddr, ATDebugExpEvalContext()));

		if (oplo == kATDebugExpNodeType_GT)
			++rangeloaddr;
		else if (oplo != kATDebugExpNodeType_GE)
			validRange = false;

		if (ophi == kATDebugExpNodeType_LT)
			--rangehiaddr;
		else if (ophi != kATDebugExpNodeType_LE)
			validRange = false;

		if (!validRange) {
			throw MyError("Unable to parse access range: relative checks for read or write accesses were found, but a range could not be determined. "
				"An access range must be specified with the READ or WRITE operators using a </<= and >/>= pair.");
		}

		if (rangeloaddr < 0 || rangehiaddr > 0xFFFF || rangeloaddr > rangehiaddr)
			throw MyError("Invalid access range: $%04X-%04X.\n", rangeloaddr, rangehiaddr);

		// Check if we're only doing exactly one byte and demote to a single address breakpoint
		// if so.
		if (rangeloaddr == rangehiaddr) {
			if (israngewrite)
				rangelo.swap(extwrite);
			else
				rangelo.swap(extread);

			addr = rangeloaddr;
			isrange = false;
		}
	} else {
		VDVERIFY((extpc ? extpc : extread ? extread : extwrite)->Evaluate(addr, ATDebugExpEvalContext()));

		if (addr < 0 || addr > 0xFFFF)
			throw MyError("Invalid breakpoint address: $%x. Addresses must be in the 64K address space.", addr);
	}

	if (rem) {
		// check if the remainder is always true, and if so, drop it
		if (rem->mType == kATDebugExpNodeType_Const) {
			sint32 v;

			if (rem->Evaluate(v, ATDebugExpEvalContext()) && v)
				rem.reset();
		}
	}

	ATBreakpointManager *bpm = GetBreakpointManager();
	const uint32 sysidx = isrange ? bpm->SetAccessRangeBP(rangeloaddr, rangehiaddr - rangeloaddr + 1, !israngewrite, israngewrite)
						: extpc ? bpm->SetAtPC((uint16)addr)
						: extread ? bpm->SetAccessBP((uint16)addr, true, false)
						: extwrite ? bpm->SetAccessBP((uint16)addr, false, true)
						: bpm->SetAccessBP((uint16)addr, false, true);

	uint32 useridx = RegisterSystemBreakpoint(sysidx, rem, command, false);
	rem.release();

	return useridx;
}

uint32 ATDebugger::RegisterSystemBreakpoint(uint32 sysidx, ATDebugExpNode *condexp, const char *command, bool continueExecution) {
	UserBPs::iterator it(std::find_if(mUserBPs.begin(), mUserBPs.end(), UserBPFreePred()));
	uint32 useridx = it - mUserBPs.begin();

	if (it == mUserBPs.end())
		mUserBPs.push_back();

	UserBP& ubp = mUserBPs[useridx];
	ubp.mSysBP = sysidx;
	ubp.mpCondition = condexp;
	ubp.mCommand = command ? command : "";
	ubp.mModuleId = 0;
	ubp.mbContinueExecution = continueExecution;

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
	bp.mSource.clear();

	if (bp.mpCondition) {
		delete bp.mpCondition;
		bp.mpCondition = NULL;
	}
}

bool ATDebugger::GetBreakpointInfo(uint32 useridx, ATBreakpointInfo& info) const {
	if (useridx >= mUserBPs.size())
		return false;

	const UserBP& bp = mUserBPs[useridx];

	if (bp.mSysBP == (uint32)-1)
		return false;

	mpBkptManager->GetInfo(bp.mSysBP, info);
	return true;
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

bool ATDebugger::GetBreakpointSourceLocation(uint32 useridx, VDStringA& file, uint32& line) const {
	if (useridx >= mUserBPs.size())
		return NULL;

	const UserBP& ubp = mUserBPs[useridx];
	if (ubp.mSource.empty())
		return false;

	file = ubp.mSource;
	line = ubp.mSourceLine;
	return true;
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
	mbRunning = true;

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
	mbRunning = true;

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
	mbRunning = true;

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
		if (mWatchLength[i] < 0) {
			mWatchAddress[i] = address;
			mWatchLength[i] = length;
			return i;
		}
	}

	return -1;
}

int ATDebugger::AddWatchExpr(ATDebugExpNode *expr) {
	for(int i=0; i<8; ++i) {
		if (mWatchLength[i] < 0) {
			mWatchAddress[i] = 0;
			mWatchLength[i] = 0;
			mpWatchExpr[i] = expr;
			return i;
		}
	}

	return -1;
}

bool ATDebugger::ClearWatch(int idx) {
	if (idx < 0 || idx > 7)
		return false;

	mWatchLength[idx] = -1;
	mpWatchExpr[idx].reset();
	return true;
}

void ATDebugger::ClearAllWatches() {
	for(int i=0; i<8; ++i)
		mWatchLength[i] = -1;
}

bool ATDebugger::GetWatchInfo(int idx, ATDebuggerWatchInfo& winfo) {
	if ((unsigned)idx >= 8 || mWatchLength[idx] < 0)
		return false;

	winfo.mAddress = mWatchAddress[idx];
	winfo.mLen = mWatchLength[idx];
	winfo.mpExpr = mpWatchExpr[idx];
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

void ATDebugger::ReloadModules() {
	Modules::iterator it(mModules.begin()), itEnd(mModules.end());
	for(; it!=itEnd; ++it) {
		Module& mod = *it;

		if (!mod.mPath.empty()) {
			vdrefptr<IATSymbolStore> symStore;

			if (ATLoadSymbols(mod.mPath.c_str(), ~symStore)) {
				ClearSymbolDirectives(mod.mId);
				mod.mpSymbols = symStore;
				ProcessSymbolDirectives(mod.mId);
				ATConsolePrintf("Reloaded symbols: %ls\n", mod.mPath.c_str());
			} else {
				ATConsolePrintf("Unable to reload symbols: %ls\n", mod.mPath.c_str());
			}
		}
	}

	ResolveDeferredBreakpoints();
	NotifyEvent(kATDebugEvent_SymbolsChanged);
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
	char devName[3];

	if (cmd != 0x03) {
		uint8 dev = g_sim.DebugReadByte(iocb + ATKernelSymbols::ICHID);

		if (dev == 0xFF) {
			devName[0] = '-';
			devName[1] = 0;
		} else {
			dev = g_sim.DebugReadByte(dev + ATKernelSymbols::HATABS);

			if (dev > 0x20 && dev < 0x7F)
				devName[0] = (char)dev;
			else
				devName[0] = '?';

			devName[1] = ':';
			devName[2] = 0;
		}
	}

	char fn[128];
	fn[0] = 0;

	if (cmd == 0x03 || cmd >= 0x0D) {
		int idx = 0;
		uint16 bufadr = g_sim.DebugReadWord(iocb + ATKernelSymbols::ICBAL);

		while(idx < 127) {
			uint8 c = g_sim.DebugReadByte(bufadr + idx);

			if (c < 0x20 || c >= 0x7f)
				break;

			fn[idx++] = c;
		}

		fn[idx] = 0;
	}

	switch(cmd) {
		case 0x03:
			{
				const uint8 aux1 = g_sim.DebugReadByte(iocb + ATKernelSymbols::ICAX1);

				ATConsolePrintf("CIO: IOCB=%u, CMD=$03 (open), AUX1=$%02x, filename=\"%s\"\n", iocbIdx, aux1, fn);
			}
			break;

		case 0x05:
			ATConsolePrintf("CIO: IOCB=%u (%s), CMD=$05 (get record), buffer=$%04x, length=$%04x\n"
				, iocbIdx
				, devName
				, g_sim.DebugReadWord(iocb + ATKernelSymbols::ICBAL)
				, g_sim.DebugReadWord(iocb + ATKernelSymbols::ICBLL)
				);
			break;

		case 0x07:
			ATConsolePrintf("CIO: IOCB=%u (%s), CMD=$07 (get characters), buffer=$%04x, length=$%04x\n"
				, iocbIdx
				, devName
				, g_sim.DebugReadWord(iocb + ATKernelSymbols::ICBAL)
				, g_sim.DebugReadWord(iocb + ATKernelSymbols::ICBLL)
				);
			break;

		case 0x09:
			ATConsolePrintf("CIO: IOCB=%u (%s), CMD=$09 (put record)\n", iocbIdx, devName);
			break;

		case 0x0A:
			{
				const uint8 c = g_sim.GetCPU().GetA();
				ATConsolePrintf("CIO: IOCB=%u (%s), CMD=$0A (put byte): char=$%02X (%c)\n"
					, iocbIdx
					, devName
					, c
					, c >= 0x20 && c < 0x7F ? (char)c : '.');
			}
			break;
		case 0x0B:
			{
				uint16 len = g_sim.DebugReadWord(iocb + ATKernelSymbols::ICBLL);

				// Length=0 is a special case that uses the A register instead.
				if (len) {
					ATConsolePrintf("CIO: IOCB=%u (%s), CMD=$07 (put characters): buf=$%04X, len=$%04X\n"
						, iocbIdx
						, devName
						, g_sim.DebugReadWord(iocb + ATKernelSymbols::ICBAL)
						, len
						);
				} else {
					ATConsolePrintf("CIO: IOCB=%u (%s), CMD=$07 (put character): ch=$%02X\n"
						, iocbIdx
						, devName
						, g_sim.GetCPU().GetA()
						);
				}
			}
			break;

		case 0x0C:
			ATConsolePrintf("CIO: IOCB=%u (%s), CMD=$0C (close)\n", iocbIdx, devName);
			break;

		case 0x0D:
			ATConsolePrintf("CIO: IOCB=%u (%s), CMD=$0D (get status): filename=\"%s\"\n", iocbIdx, devName, fn);
			break;

		default:
			if (cmd >= 0x0E) {
				ATConsolePrintf("CIO: IOCB=%u (%s), CMD=$%02x (special): filename=\"%s\"\n", iocbIdx, devName, cmd, fn);
			} else {
				ATConsolePrintf("CIO: IOCB=%u (%s), CMD=$%02x (unknown)\n", iocbIdx, devName, cmd);
			}
			break;
	}
}

void ATDebugger::SetCIOTracingEnabled(bool enabled) {
	if (enabled) {
		if (mSysBPTraceCIO)
			return;

		mSysBPTraceCIO = mpBkptManager->SetAtPC(ATKernelSymbols::CIOV);
	} else {
		if (!mSysBPTraceCIO)
			return;

		mpBkptManager->Clear(mSysBPTraceCIO);
		mSysBPTraceCIO = 0;
	}
}

uint32 ATDebugger::AddModule(uint32 base, uint32 size, IATSymbolStore *symbolStore, const char *name, const wchar_t *path) {
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

	if (path)
		newmod.mPath = path;

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
		mModules.insert(mModules.begin(), Module());
		Module& hwmod = mModules.front();

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

	uint32 moduleId = AddModule(symStore->GetDefaultBase(), symStore->GetDefaultSize(), symStore, VDTextWToA(fileName).c_str(), VDGetFullPath(fileName).c_str());

	if (moduleId && processDirectives)
		ProcessSymbolDirectives(moduleId);

	ResolveDeferredBreakpoints();
	return moduleId;
}

void ATDebugger::UnloadSymbols(uint32 moduleId) {
	Modules::iterator it(mModules.begin()), itEnd(mModules.end());
	for(; it!=itEnd; ++it) {
		const Module& mod = *it;

		if (mod.mId == moduleId) {
			ClearSymbolDirectives(mod.mId);

			mModules.erase(it);
			mbSymbolUpdatePending = true;
			return;
		}
	}
}

void ATDebugger::ClearSymbolDirectives(uint32 moduleId) {
	// scan all breakpoints and clear those from this module
	uint32 n = mUserBPs.size();

	for(uint32 i=0; i<n; ++i) {
		UserBP& ubp = mUserBPs[i];

		if (ubp.mModuleId == moduleId && ubp.mSysBP != (uint32)-1) {
			mpBkptManager->Clear(ubp.mSysBP);
			UnregisterSystemBreakpoint(ubp.mSysBP);
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
						expr = ATDebuggerParseExpression(dirInfo.mpArguments, this, mExprOpts);
						
						vdautoptr<ATDebugExpNode> expr2(ATDebuggerInvertExpression(expr));
						expr.release();
						expr.swap(expr2);

						if (expr->mType == kATDebugExpNodeType_Const)
							ATConsolePrintf("Warning: ##ASSERT expression is a constant: %s\n", dirInfo.mpArguments);

						VDStringA cmd;

						cmd.sprintf(".printf \\\"Assert failed at $%04X: ", dirInfo.mOffset);

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
						uint32 useridx = RegisterSystemBreakpoint(sysidx, expr, cmd.c_str(), false);
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

					cmd = "`.printf ";

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
							cmd += "\\\"";

						cmd.append(arg, argEnd - arg);

						if (useQuotes)
							cmd += '"';

						cmd += ' ';
					}

					uint32 sysidx = mpBkptManager->SetAtPC(dirInfo.mOffset);
					uint32 useridx = RegisterSystemBreakpoint(sysidx, NULL, cmd.c_str(), true);

					mUserBPs[useridx].mModuleId = id;
				}
				break;
		}
	}
}

sint32 ATDebugger::ResolveSourceLocation(const char *fn, uint32 line) {
	const VDStringW fnw(VDTextAToW(fn));

	uint32 moduleId;
	uint16 fileId;

	if (!LookupFile(fnw.c_str(), moduleId, fileId))
		return -1;

	Module *mod = GetModuleById(moduleId);

	ATSourceLineInfo lineInfo = {};
	lineInfo.mFileId = fileId;
	lineInfo.mLine = line;
	lineInfo.mOffset = 0;

	uint32 modOffset;
	if (mod->mpSymbols->GetOffsetForLine(lineInfo, modOffset))
		return mod->mBase + modOffset;

	return -1;
}

sint32 ATDebugger::ResolveSymbol(const char *s, bool allowGlobal, bool allowShortBase) {
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
		} else if (!strncmp(s, "r:", 2)) {
			addressSpace = kATAddressSpace_RAM;
			addressLimit = 0xffff;
			s += 2;
		}
	}

	if (!vdstricmp(s, "pc"))
		return g_sim.GetCPU().GetInsnPC();

	bool forceHex = false;
	if (s[0] == '$') {
		++s;
		forceHex = true;
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
	int base = forceHex || (allowShortBase ? mExprOpts.mbAllowUntaggedHex : mExprOpts.mbDefaultHex) ? 16 : 10;
	unsigned long result = strtoul(s, &t, base);

	// check for bank
	if (*t == ':' && addressSpace == kATAddressSpace_CPU) {
		if (result > 0xff)
			return -1;

		s = t+1;
		uint32 offset = strtoul(s, &t, base);
		if (offset > 0xffff || *t)
			return -1;

		return (result << 16) + offset;
	} else {
		if (result > addressLimit || *t)
			return -1;

		return result + addressSpace;
	}
}

uint32 ATDebugger::ResolveSymbolThrow(const char *s, bool allowGlobal, bool allowShortBase) {
	sint32 v = ResolveSymbol(s, allowGlobal, allowShortBase);

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

	mbSymbolUpdatePending = true;
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

	mbSymbolUpdatePending = true;
}

void ATDebugger::LoadCustomSymbols(const wchar_t *filename) {
	UnloadSymbols(kModuleId_Manual);

	vdrefptr<IATSymbolStore> css;
	if (!ATLoadSymbols(filename, ~css))
		return;

	mModules.push_back(Module());
	Module& mod = mModules.back();

	mod.mName = "Manual";
	mod.mId = kModuleId_Manual;
	mod.mBase = css->GetDefaultBase();
	mod.mSize = css->GetDefaultSize();
	mod.mpSymbols = css;
	mod.mbDirty = false;

	ATConsolePrintf("%d symbol(s) loaded.\n", css->GetSymbolCount());

	mbSymbolUpdatePending = true;
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
		case kATAddressSpace_RAM:
			s.sprintf("r:%s%04X", prefix, globalAddr & 0xffff);
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

ATDebugExpEvalContext ATDebugger::GetEvalContext() const {
	ATDebugExpEvalContext ctx;
	ctx.mpAntic = &g_sim.GetAntic();
	ctx.mpCPU = &g_sim.GetCPU();
	ctx.mpMemory = &g_sim.GetCPUMemory();
	ctx.mpMMU = g_sim.GetMMU();
	ctx.mbAccessValid = true;
	ctx.mbAccessReadValid = false;
	ctx.mbAccessWriteValid = false;
	ctx.mAccessAddress = mExprAddress;
	ctx.mAccessValue = mExprValue;

	return ctx;
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

void ATDebugger::QueueBatchFile(const wchar_t *path) {
	VDTextInputFile tif(path);

	std::deque<VDStringA> commands;

	while(const char *s = tif.GetNextLine()) {
		commands.push_back(VDStringA());
		commands.back() = s;
	}

	while(!commands.empty()) {
		g_debugger.QueueCommandFront(commands.back().c_str(), false);
		commands.pop_back();
	}
}

bool ATDebugger::IsCommandAliasPresent(const char *alias) const {
	return mAliases.find_as(alias) != mAliases.end();
}

bool ATDebugger::MatchCommandAlias(const char *alias, const char *const *argv, int argc, vdfastvector<char>& tempstr, vdfastvector<const char *>& argptrs) const {
	Aliases::const_iterator it = mAliases.find_as(alias);

	if (it == mAliases.end())
		return false;

	AliasList::const_iterator it2(it->second.begin()), it2End(it->second.end());

	VDStringRefA patargs[10];

	for(; it2 != it2End; ++it2) {
		VDStringRefA argpat(it2->first.c_str());
		VDStringRefA pattoken;

		uint32 patargsvalid = 0;
		const char *const *wildargs = NULL;

		int patidx = 0;
		bool valid = true;

		while(!argpat.empty()) {
			if (!argpat.split(' ', pattoken)) {
				pattoken = argpat;
				argpat.clear();
			}

			// check for wild pattern
			if (pattoken == "%*") {
				wildargs = argv + patidx;
				patidx = argc;
				break;
			}

			// check for insufficient arguments
			if (patidx >= argc)
				break;

			// match pattern
			VDStringRefA::const_iterator itPatToken(pattoken.begin()), itPatTokenEnd(pattoken.end());
			const char *s = argv[patidx];

			bool percent = false;
			while(itPatToken != itPatTokenEnd) {
				char c = *itPatToken++;

				if (c == '%') {
					percent = !percent;

					if (percent)
						continue;
				} else if (percent) {
					if (c >= '0' && c <= '9') {
						patargsvalid |= 1 << (c - '0');

						patargs[c - '0'] = s;
						percent = false;
						break;
					} else {
						valid = false;
						break;
					}
				}

				if (!*s || *s != c) {
					valid = false;
					break;
				}

				++s;
			}

			if (percent)
				valid = false;

			if (!valid)
				break;

			++patidx;
		}

		// check for extra args
		if (argc != patidx)
			continue;

		if (!valid)
			continue;

		// We have a match -- apply result template.
		VDStringRefA tmpl(it2->second.c_str());
		VDStringRefA tmpltoken;

		vdfastvector<int> argoffsets;

		while(!tmpl.empty()) {
			if (!tmpl.split(' ', tmpltoken)) {
				tmpltoken = tmpl;
				tmpl.clear();
			}

			if (tmpltoken == "%*") {
				while(const char *arg = *wildargs++) {
					argoffsets.push_back(tempstr.size());
					tempstr.insert(tempstr.end(), arg, arg + strlen(arg) + 1);
				}

				continue;
			}

			argoffsets.push_back(tempstr.size());

			VDStringRefA::const_iterator itTmplToken(tmpltoken.begin()), itTmplTokenEnd(tmpltoken.end());

			bool percent = false;
			for(; itTmplToken != itTmplTokenEnd; ++itTmplToken) {
				char c = *itTmplToken;

				if (c == '%') {
					percent = !percent;

					if (percent)
						continue;
				}

				if (percent) {
					if (c >= '0' && c <= '9') {
						int idx = c - '0';

						if (patargsvalid & (1 << idx)) {
							const VDStringRefA& insarg = patargs[idx];
							tempstr.insert(tempstr.end(), insarg.begin(), insarg.end());
						}
					}

					continue;
				}

				tempstr.push_back(c);
			}

			tempstr.push_back(0);
		}

		argptrs.reserve(argoffsets.size() + 1);

		for(vdfastvector<int>::const_iterator itOff(argoffsets.begin()), itOffEnd(argoffsets.end());
			itOff != itOffEnd;
			++itOff)
		{
			argptrs.push_back(tempstr.data() + *itOff);
		}

		argptrs.push_back(NULL);

		return true;
	}

	argptrs.clear();
	return true;
}

const char *ATDebugger::GetCommandAlias(const char *alias, const char *args) const {
	Aliases::const_iterator it = mAliases.find_as(alias);

	if (it == mAliases.end())
		return NULL;

	AliasList::const_iterator it2(it->second.begin()), it2End(it->second.end());

	for(; it2 != it2End; ++it2) {
		if (it2->first == args)
			return it2->second.c_str();
	}

	return NULL;
}

void ATDebugger::SetCommandAlias(const char *alias, const char *args, const char *command) {
	if (command) {
		Aliases::iterator it(mAliases.insert_as(alias).first);

		if (args) {
			AliasList::iterator it2(it->second.begin()), it2End(it->second.end());

			for(; it2 != it2End; ++it2) {
				if (it2->first == args)
					return;
			}

			it->second.push_back(AliasList::value_type(VDStringA(args), VDStringA(command)));
		} else {
			it->second.resize(1);
			it->second.back().first = "%*";
			it->second.back().second = command;
		}
	} else {
		Aliases::iterator it(mAliases.find_as(alias));

		if (it != mAliases.end()) {
			if (args) {
				AliasList::iterator it2(it->second.begin()), it2End(it->second.end());

				for(; it2 != it2End; ++it2) {
					if (it2->first == args) {
						it->second.erase(it2);

						if (it->second.empty())
							mAliases.erase(it);
					}
				}
			} else {
				mAliases.erase(it);
			}
		}
	}
}

struct ATDebugger::AliasSorter {
	typedef std::pair<const char *, const AliasList *> value_type;

	bool operator()(const value_type& x, const value_type& y) const {
		return strcmp(x.first, y.first) < 0;
	}
};

void ATDebugger::ListCommandAliases() {
	if (mAliases.empty()) {
		ATConsoleWrite("No command aliases defined.\n");
		return;
	}

	typedef vdfastvector<std::pair<const char *, const AliasList *> > SortedAliases;
	SortedAliases sortedAliases;
	sortedAliases.reserve(mAliases.size());

	for(Aliases::const_iterator it(mAliases.begin()), itEnd(mAliases.end());
		it != itEnd;
		++it)
	{
		sortedAliases.push_back(std::make_pair(it->first.c_str(), &it->second));
	}

	std::sort(sortedAliases.begin(), sortedAliases.end(), AliasSorter());

	ATConsoleWrite("Current command aliases:\n");

	VDStringA s;
	for(SortedAliases::const_iterator it(sortedAliases.begin()), itEnd(sortedAliases.end());
		it != itEnd;
		++it)
	{
		const AliasList& al = *it->second;

		for(AliasList::const_iterator it2(al.begin()), it2End(al.end()); it2 != it2End; ++it2) {
			s = it->first;
			s += ' ';
			s += it2->first;

			if (s.size() < 10)
				s.resize(10, ' ');

			s += " -> ";
			s += it2->second;
			s += '\n';

			ATConsoleWrite(s.c_str());
		}
	}
}

void ATDebugger::ClearCommandAliases() {
	mAliases.clear();
}

void ATDebugger::OnExeQueueCmd(bool onrun, const char *s) {
	mOnExeCmds[onrun].push_back(VDStringA());
	mOnExeCmds[onrun].back() = s;
}

void ATDebugger::OnExeClear() {
	mOnExeCmds[0].clear();
	mOnExeCmds[1].clear();
}

bool ATDebugger::OnExeGetCmd(bool onrun, int index, VDStringA& s) {
	OnExeCmds& cmds = mOnExeCmds[onrun];

	if (index < 0 || (unsigned)index >= cmds.size())
		return false;

	s = cmds[index];
	return true;
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
	int bestQuality = 0;

	Modules::const_iterator it(mModules.begin()), itEnd(mModules.end());
	for(; it!=itEnd; ++it) {
		const Module& mod = *it;
		int q;

		uint16 fid = mod.mpSymbols->GetFileId(fileName, &q);

		if (fid && q > bestQuality) {
			bestQuality = q;
			moduleId = mod.mId;
			fileId = fid;
		}
	}

	return bestQuality > 0;
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
					case 0:
						{
							const ATDebugExpEvalContext& ctx = g_debugger.GetEvalContext();

							sint32 result;
							r->SetWatchedValue(i, mpWatchExpr[i]->Evaluate(result, ctx) ? result : 0, 0);
						}
						break;

					default:
						r->ClearWatchedValue(i);
						break;
				}
			}
		}

		return;
	}

	if (ev == kATSimEvent_WarmReset)
		return;

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

	if (ev == kATSimEvent_StateLoaded) {
		ATConsoleWrite("Save state loaded.\n");

		if (mbRunning)
			return;

		cpu.DumpStatus();
	} else if (ev == kATSimEvent_EXELoad) {
		if (!mOnExeCmds[0].empty()) {
			g_sim.Suspend();

			for(OnExeCmds::const_iterator it(mOnExeCmds[0].begin()), itEnd(mOnExeCmds[0].end());
				it != itEnd;
				++it)
			{
				QueueCommand(it->c_str(), false);
			}
		}
		return;
	} else if (ev == kATSimEvent_EXEInitSegment) {
		return;
	} else if (ev == kATSimEvent_AbnormalDMA) {
		return;
	} else if (ev == kATSimEvent_EXERunSegment) {
		if (!mOnExeCmds[1].empty()) {
			g_sim.Suspend();

			for(OnExeCmds::const_iterator it(mOnExeCmds[1].begin()), itEnd(mOnExeCmds[1].end());
				it != itEnd;
				++it)
			{
				QueueCommand(it->c_str(), false);
			}
		}

		if (!mbBreakOnEXERunAddr)
			return;

		if (mSysBPEEXRun)
			mpBkptManager->Clear(mSysBPEEXRun);

		mSysBPEEXRun = mpBkptManager->SetAtPC(cpu.GetPC());
		return;
	}

	if (ev == kATSimEvent_CPUPCBreakpointsUpdated) {
		NotifyEvent(kATDebugEvent_BreakpointsChanged);
		return;
	}

	if (!ATIsDebugConsoleActive()) {
		ATDebuggerOpenEvent event;
		event.mbAllowOpen = true;

		mEventOpen.Raise(this, &event);

		if (!event.mbAllowOpen)
			return;

		ATSetFullscreen(false);
		ATOpenConsole();
	}

	switch(ev) {
		case kATSimEvent_CPUSingleStep:
		case kATSimEvent_CPUStackBreakpoint:
		case kATSimEvent_CPUPCBreakpoint:
			cpu.DumpStatus();
			mbRunning = false;
			break;

		case kATSimEvent_CPUIllegalInsn:
			ATConsolePrintf("CPU: Illegal instruction hit: %04X\n", cpu.GetPC());
			cpu.DumpStatus();
			mbRunning = false;
			break;

		case kATSimEvent_CPUNewPath:
			ATConsoleWrite("CPU: New path encountered with path break enabled.\n");
			cpu.DumpStatus();
			mbRunning = false;
			break;

		case kATSimEvent_ReadBreakpoint:
		case kATSimEvent_WriteBreakpoint:
			cpu.DumpStatus();
			mbRunning = false;
			break;

		case kATSimEvent_DiskSectorBreakpoint:
			ATConsolePrintf("DISK: Sector breakpoint hit: %d\n", g_sim.GetDiskDrive(0).GetSectorBreakpoint());
			mbRunning = false;
			break;

		case kATSimEvent_EndOfFrame:
			ATConsoleWrite("End of frame reached.\n");
			mbRunning = false;
			break;

		case kATSimEvent_ScanlineBreakpoint:
			ATConsoleWrite("Scanline breakpoint reached.\n");
			mbRunning = false;
			break;

		case kATSimEvent_VerifierFailure:
			cpu.DumpStatus();
			g_sim.Suspend();
			mbRunning = false;
			break;
	}

	cpu.SetRTSBreak();

	if (ev != kATSimEvent_CPUPCBreakpointsUpdated)
		mFramePC = cpu.GetInsnPC();

	mbClientUpdatePending = true;

	if (mbSourceMode && !mbRunning)
		ActivateSourceWindow();
}

void ATDebugger::ResolveDeferredBreakpoints() {
	bool breakpointsChanged = false;

	UserBPs::iterator it(mUserBPs.begin()), itEnd(mUserBPs.end());
	for(; it != itEnd; ++it) {
		UserBP& ubp = *it;

		if (ubp.mSysBP || ubp.mSource.empty())
			continue;

		sint32 addr = ResolveSourceLocation(ubp.mSource.c_str(), ubp.mSourceLine);
		if (addr < 0)
			continue;

		ubp.mSysBP = mpBkptManager->SetAtPC((uint16)addr);
		mSysBPToUserBPMap[ubp.mSysBP] = (uint32)(it - mUserBPs.begin());
		breakpointsChanged = true;
	}

	NotifyEvent(kATDebugEvent_BreakpointsChanged);
}

void ATDebugger::UpdateClientSystemState(IATDebuggerClient *client) {
	if (!client)
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

		mContinuationAddress = sysstate.mInsnPC + ((uint32)sysstate.mK << 16);

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
		ATDebugExpEvalContext context(GetEvalContext());
		context.mbAccessValid = true;
		context.mbAccessReadValid = true;
		context.mbAccessWriteValid = true;
		context.mAccessAddress = event->mAddress;
		context.mAccessValue = event->mValue;

		sint32 result;
		if (!bp.mpCondition->Evaluate(result, context) || !result)
			return;
	}

	mExprAddress = event->mAddress;
	mExprValue = event->mValue;

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

				bool allowEscapes = false;
				if (c == '\\' && *s == '"') {
					allowEscapes = true;
					c = '"';
					++s;
				}

				if (c == '"') {
					for(;;) {
						c = *s;
						if (!c)
							break;
						++s;

						if (c == '"')
							break;

						if (c == '\\' && allowEscapes) {
							c = *s;
							if (!c)
								break;

							++s;
						}
					}
				}
			}

			if (start != s) {
				QueueCommand(VDStringA(start, s).c_str(), false);
				event->mbBreak = true;
			}

			if (!*s)
				break;

			++s;
		}

		event->mbSilentBreak = true;

		// Because responding to the breakpoint causes a stop, we need to manually
		// handle a step event.
		ATCPUEmulator& cpu = g_sim.GetCPU();
		if (cpu.GetStep()) {
			cpu.SetStep(false);

			mbClientUpdatePending = true;
			mbRunning = false;
			cpu.SetRTSBreak();
			mFramePC = cpu.GetInsnPC();
		}
	}

	if (!bp.mbContinueExecution) {
		event->mbBreak = true;
		mbClientUpdatePending = true;
		mbRunning = false;
	}
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

	class ATDebuggerCmdExprAddr;

	class ATDebuggerCmdLength {
	public:
		ATDebuggerCmdLength(uint32 defaultLen, bool required, ATDebuggerCmdExprAddr *anchor)
			: mLength(defaultLen)
			, mbRequired(required)
			, mbValid(false)
			, mpAnchor(anchor)
		{
		}

		bool IsValid() const { return mbValid; }

		operator uint32() const { return mLength; }

	protected:
		friend class ATDebuggerCmdParser;

		uint32 mLength;
		bool mbRequired;
		bool mbValid;
		ATDebuggerCmdExprAddr *mpAnchor;
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

	class ATDebuggerCmdPath {
	public:
		ATDebuggerCmdPath(bool required)
			: mbValid(false)
			, mbRequired(required)
		{
		}

		bool IsValid() const { return mbValid; }

		const VDStringA *operator->() const { return &mPath; }

	protected:
		friend class ATDebuggerCmdParser;

		VDStringA mPath;
		bool mbRequired;
		bool mbValid;
	};

	class ATDebuggerCmdString {
	public:
		ATDebuggerCmdString(bool required)
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

	class ATDebuggerCmdExpr {
	public:
		ATDebuggerCmdExpr(bool required)
			: mbRequired(required)
			, mpExpr(NULL)
		{
		}

		ATDebugExpNode *GetValue() const { return mpExpr; }

	protected:
		friend class ATDebuggerCmdParser;

		bool mbRequired;
		vdautoptr<ATDebugExpNode> mpExpr;
	};

	class ATDebuggerCmdExprNum {
	public:
		ATDebuggerCmdExprNum(bool required, bool hex = true, sint32 minVal = -0x7FFFFFFF-1, sint32 maxVal = 0x7FFFFFFF, sint32 defaultValue = 0, bool allowStar = false)
			: mbRequired(required)
			, mbValid(false)
			, mbAllowStar(allowStar)
			, mbStar(false)
			, mbHexDefault(hex)
			, mValue(defaultValue)
			, mMinVal(minVal)
			, mMaxVal(maxVal)
		{
		}

		bool IsValid() const { return mbValid; }
		bool IsStar() const { return mbStar; }
		sint32 GetValue() const { return mValue; }

	protected:
		friend class ATDebuggerCmdParser;

		bool mbRequired;
		bool mbValid;
		bool mbStar;
		bool mbAllowStar;
		bool mbHexDefault;
		sint32 mValue;
		sint32 mMinVal;
		sint32 mMaxVal;
	};

	class ATDebuggerCmdExprAddr {
	public:
		ATDebuggerCmdExprAddr(bool general, bool required, bool allowStar = false, uint32 defaultValue = 0)
			: mbRequired(required)
			, mbAllowStar(allowStar)
			, mbValid(false)
			, mbStar(false)
			, mValue(defaultValue)
		{
		}

		bool IsValid() const { return mbValid; }
		bool IsStar() const { return mbStar; }
		uint32 GetValue() const { return mValue; }

	protected:
		friend class ATDebuggerCmdParser;

		bool mbRequired;
		bool mbAllowStar;
		bool mbValid;
		bool mbStar;
		uint32 mValue;
	};

	class ATDebuggerCmdParser {
	public:
		ATDebuggerCmdParser(int argc, const char *const *argv);

		const char *GetNextArgument();

		ATDebuggerCmdParser& operator>>(ATDebuggerCmdSwitch& sw);
		ATDebuggerCmdParser& operator>>(ATDebuggerCmdSwitchNumArg& sw);
		ATDebuggerCmdParser& operator>>(ATDebuggerCmdBool& bo);
		ATDebuggerCmdParser& operator>>(ATDebuggerCmdNumber& nu);
		ATDebuggerCmdParser& operator>>(ATDebuggerCmdAddress& ad);
		ATDebuggerCmdParser& operator>>(ATDebuggerCmdLength& ln);
		ATDebuggerCmdParser& operator>>(ATDebuggerCmdName& nm);
		ATDebuggerCmdParser& operator>>(ATDebuggerCmdPath& nm);
		ATDebuggerCmdParser& operator>>(ATDebuggerCmdString& nm);
		ATDebuggerCmdParser& operator>>(ATDebuggerCmdQuotedString& nm);
		ATDebuggerCmdParser& operator>>(ATDebuggerCmdExpr& nu);
		ATDebuggerCmdParser& operator>>(ATDebuggerCmdExprNum& nu);
		ATDebuggerCmdParser& operator>>(ATDebuggerCmdExprAddr& nu);
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

	const char *ATDebuggerCmdParser::GetNextArgument() {
		if (mArgs.empty())
			return false;

		const char *s = mArgs.front();
		mArgs.erase(mArgs.begin());

		return s;
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
		VDStringA quoteStripBuf;

		for(Args::iterator it(mArgs.begin()), itEnd(mArgs.end()); it != itEnd; ++it) {
			const char *s = *it;
			bool quoted = false;

			if (s[0] == '"') {
				quoted = true;
				++s;
			}

			if (s[0] == 'L' || s[0] == 'l') {
				++s;

				if (quoted) {
					size_t len = strlen(s);

					if (len && s[len - 1] == '"') {
						quoteStripBuf.assign(s, s + len - 1);
						s = quoteStripBuf.c_str();
					}
				}

				char *t = (char *)s;

				bool end_addr = false;

				if (*s == '>') {
					++s;

					if (!ln.mpAnchor || !ln.mpAnchor->mbValid || ln.mpAnchor->mbStar)
						throw MyError("Address end syntax cannot be used in this context.");

					end_addr = true;
				}

				sint32 v;
				vdautoptr<ATDebugExpNode> node;
				try {
					node = ATDebuggerParseExpression(s, ATGetDebugger(), ATGetDebugger()->GetExprOpts());
				} catch(const ATDebuggerExprParseException& ex) {
					throw MyError("Unable to parse expression '%s': %s\n", s, ex.c_str());
				}

				if (!node->Evaluate(v, g_debugger.GetEvalContext()))
					throw MyError("Cannot evaluate '%s' in this context.", s);

				if (end_addr) {
					if (v < 0 || (uint32)v < ln.mpAnchor->mValue)
						throw MyError("End address is prior to start address.");

					ln.mLength = v - ln.mpAnchor->mValue + 1;
				} else {
					if (v < 0)
						throw MyError("Invalid length: %s", s);

					ln.mLength = (uint32)v;
				}

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

	ATDebuggerCmdParser& ATDebuggerCmdParser::operator>>(ATDebuggerCmdPath& nm) {
		for(Args::iterator it(mArgs.begin()), itEnd(mArgs.end()); it != itEnd; ++it) {
			const char *s = *it;

			bool quoted = false;
			if (s[0] == '"') {
				quoted = true;
				++s;
			}

			const char *t = s + strlen(s);

			if (quoted && t != s && t[-1] == '"')
				--t;

			nm.mPath.assign(s, t);
			nm.mbValid = true;

			mArgs.erase(it);
			return *this;
		}

		if (nm.mbRequired)
			throw MyError("Path parameter required.");

		return *this;
	}

	ATDebuggerCmdParser& ATDebuggerCmdParser::operator>>(ATDebuggerCmdString& nm) {
		for(Args::iterator it(mArgs.begin()), itEnd(mArgs.end()); it != itEnd; ++it) {
			const char *s = *it;

			if (s[0] == '-')
				continue;

			if (s[0] == '"') {
				++s;
				const char *t = s + strlen(s);

				if (t != s && t[-1] == '"')
					--t;

				nm.mName.assign(s, t);
			} else {
				nm.mName = s;
			}

			nm.mbValid = true;
			mArgs.erase(it);
			return *this;
		}

		if (nm.mbRequired)
			throw MyError("String parameter required.");

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

	ATDebuggerCmdParser& ATDebuggerCmdParser::operator>>(ATDebuggerCmdExpr& xp) {
		if (mArgs.empty()) {
			if (xp.mbRequired)
				throw MyError("Missing expression argument.");

			return *this;
		}

		const char *s = mArgs.front();
		mArgs.erase(mArgs.begin());

		VDStringA quoteStripBuf;

		if (*s == '"') {
			++s;

			size_t len = strlen(s);

			if (len && s[len - 1] == '"') {
				quoteStripBuf.assign(s, s + len - 1);
				s = quoteStripBuf.c_str();
			}
		}

		try {
			xp.mpExpr = ATDebuggerParseExpression(s, ATGetDebugger(), ATGetDebugger()->GetExprOpts());
		} catch(const ATDebuggerExprParseException& ex) {
			throw MyError("Unable to parse expression '%s': %s\n", s, ex.c_str());
		}

		return *this;
	}
	ATDebuggerCmdParser& ATDebuggerCmdParser::operator>>(ATDebuggerCmdExprNum& nu) {
		if (mArgs.empty()) {
			if (nu.mbRequired)
				throw MyError("Missing numeric argument.");

			return *this;
		}

		const char *s = mArgs.front();
		mArgs.erase(mArgs.begin());

		if (nu.mbAllowStar && s[0] == '*' && !s[1]) {
			nu.mbValid = true;
			nu.mbStar = true;
			nu.mValue = 0;
			return *this;
		}

		VDStringA quoteStripBuf;
		sint32 v;

		char dummy;
		long lval;
		if (1 == sscanf(s, nu.mbHexDefault ? "%lx%c" : "%ld%c", &lval, &dummy)) {
			v = lval;
		} else {
			if (*s == '"') {
				++s;

				size_t len = strlen(s);

				if (len && s[len - 1] == '"') {
					quoteStripBuf.assign(s, s + len - 1);
					s = quoteStripBuf.c_str();
				}
			}

			vdautoptr<ATDebugExpNode> node;
			try {
				node = ATDebuggerParseExpression(s, ATGetDebugger(), ATGetDebugger()->GetExprOpts());
			} catch(const ATDebuggerExprParseException& ex) {
				throw MyError("Unable to parse expression '%s': %s\n", s, ex.c_str());
			}

			if (!node->Evaluate(v, g_debugger.GetEvalContext()))
				throw MyError("Cannot evaluate '%s' in this context.", s);
		}

		if (v < nu.mMinVal || v > nu.mMaxVal)
			throw MyError("Numeric argument out of range: %d", v);

		nu.mbValid = true;
		nu.mValue = v;

		return *this;
	}

	ATDebuggerCmdParser& ATDebuggerCmdParser::operator>>(ATDebuggerCmdExprAddr& nu) {
		if (mArgs.empty()) {
			if (nu.mbRequired)
				throw MyError("Missing numeric argument.");

			return *this;
		}

		const char *s = mArgs.front();
		mArgs.erase(mArgs.begin());

		if (s[0] == '*' && !s[1] && nu.mbAllowStar) {
			nu.mbStar = true;
			nu.mbValid = true;
			return *this;
		}

		VDStringA quoteStripBuf;
		sint32 v;

		if (*s == '"') {
			++s;

			size_t len = strlen(s);

			if (len && s[len - 1] == '"') {
				quoteStripBuf.assign(s, s + len - 1);
				s = quoteStripBuf.c_str();
			}
		}

		vdautoptr<ATDebugExpNode> node;
		try {
			node = ATDebuggerParseExpression(s, ATGetDebugger(), ATGetDebugger()->GetExprOpts());
		} catch(const ATDebuggerExprParseException& ex) {
			throw MyError("Unable to parse expression '%s': %s\n", s, ex.c_str());
		}

		if (!node->Evaluate(v, g_debugger.GetEvalContext()))
			throw MyError("Cannot evaluate '%s' in this context.", s);

		nu.mbValid = true;
		nu.mValue = v;

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
	ATDebuggerCmdExprAddr addr(false, true);

	ATDebuggerCmdParser(argc, argv) >> addr;

	vdrefptr<IATDebuggerActiveCommand> cmd;

	ATCreateDebuggerCmdAssemble(addr.GetValue(), ~cmd);
	g_debugger.StartActiveCommand(cmd);
}

void ATConsoleCmdTrace() {
	ATGetDebugger()->StepInto(kATDebugSrcMode_Disasm);
}

void ATConsoleCmdGo(int argc, const char *const *argv) {
	ATDebuggerCmdSwitch swSource("s", false);
	ATDebuggerCmdSwitch swNoChange("n", false);
	ATDebuggerCmdParser(argc, argv) >> swSource >> swNoChange >> 0;

	ATGetDebugger()->Run(swNoChange ? kATDebugSrcMode_Same : swSource ? kATDebugSrcMode_Source : kATDebugSrcMode_Disasm);
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

////////////////////////////////////////////////////////////////////////////

namespace {
	bool ParseBreakpointSourceLocation(const char *s0, VDStringA& filename, uint32& line) {
		const char *s = s0;

		if (s[0] != '`')
			return false;

		++s;

		bool valid = false;

		const char *fnstart = s;
		const char *split = NULL;

		while(*s && *s != '`') {
			if (*s == ':')
				split = s;
			++s;
		}

		const char *fnend = split;

		line = 0;
		if (split) {
			s = split + 1;

			for(;;) {
				uint8 c = *s - '0';

				if (c >= 10)
					break;

				line = line * 10 + c;
				++s;
			}

			if (line)
				valid = true;
		}

		if (!valid)
			throw MyError("Invalid source location: %s", s0);

		filename.assign(fnstart, fnend);
		return true;
	}

	void MakeTracepointCommand(VDStringA& cmd, ATDebuggerCmdParser& parser) {
		cmd = "`.printf";

		const char *s = parser.GetNextArgument();
		if (!s)
			throw MyError("Trace format argument required.");

		do {
			cmd += ' ';

			ATDebuggerSerializeArgv(cmd, 1, &s);

			s = parser.GetNextArgument();
		} while(s);
	}
}

void ATConsoleCmdBreakpt(int argc, const char *const *argv) {
	ATDebuggerCmdString addrStr(true);
	ATDebuggerCmdQuotedString command(false);
	ATDebuggerCmdParser(argc, argv) >> addrStr >> command >> 0;

	ATBreakpointManager *bpm = g_debugger.GetBreakpointManager();

	const char *s = addrStr->c_str();
	uint32 useridx;
	VDStringA filename;
	uint32 line;

	if (ParseBreakpointSourceLocation(s, filename, line)) {
		useridx = g_debugger.SetSourceBreakpoint(filename.c_str(), line, NULL, command.IsValid() ? command->c_str() : NULL);

		sint32 sysidx = g_debugger.LookupUserBreakpoint(useridx);

		if (!sysidx)
			ATConsolePrintf("Deferred breakpoint %u set at %s:%u.\n", useridx, filename.c_str(), line);
		else {
			ATBreakpointInfo bpinfo;
			g_debugger.GetBreakpointManager()->GetInfo(sysidx, bpinfo);

			ATConsolePrintf("Breakpoint %u set at `%s:%u` ($%04X)\n", useridx, filename.c_str(), line, bpinfo.mAddress);
		}
	} else {
		const uint16 addr = (uint16)g_debugger.ResolveSymbolThrow(s, false);
		const uint32 sysidx = bpm->SetAtPC(addr);
		useridx = g_debugger.RegisterSystemBreakpoint(sysidx, NULL, command.IsValid() ? command->c_str() : NULL, false);
		ATConsolePrintf("Breakpoint %u set at $%04X.\n", useridx, addr);
	}

	g_sim.NotifyEvent(kATSimEvent_CPUPCBreakpointsUpdated);
}

void ATConsoleCmdBreakptTrace(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdString addrStr(true);
	parser >> addrStr;

	VDStringA tracecmd;
	MakeTracepointCommand(tracecmd, parser);

	ATBreakpointManager *bpm = g_debugger.GetBreakpointManager();

	const char *s = addrStr->c_str();
	uint32 useridx;
	VDStringA filename;
	uint32 line;

	if (ParseBreakpointSourceLocation(s, filename, line)) {
		useridx = g_debugger.SetSourceBreakpoint(filename.c_str(), line, NULL, tracecmd.c_str(), true);

		sint32 sysidx = g_debugger.LookupUserBreakpoint(useridx);

		if (!sysidx)
			ATConsolePrintf("Deferred tracepoint %u set at %s:%u.\n", useridx, filename.c_str(), line);
		else {
			ATBreakpointInfo bpinfo;
			g_debugger.GetBreakpointManager()->GetInfo(sysidx, bpinfo);

			ATConsolePrintf("Tracepoint %u set at `%s:%u` ($%04X)\n", useridx, filename.c_str(), line, bpinfo.mAddress);
		}
	} else {
		const uint16 addr = (uint16)g_debugger.ResolveSymbolThrow(s, false);
		const uint32 sysidx = bpm->SetAtPC(addr);
		useridx = g_debugger.RegisterSystemBreakpoint(sysidx, NULL, tracecmd.c_str(), true);
		ATConsolePrintf("Tracepoint %u set at $%04X.\n", useridx, addr);
	}

	g_sim.NotifyEvent(kATSimEvent_CPUPCBreakpointsUpdated);
}

void ATConsoleCmdBreakptClear(int argc, const char *const *argv) {
	if (!argc)
		throw MyError("Usage: bc <index> | *");

	if (argc >= 1) {
		ATBreakpointManager *bpm = g_debugger.GetBreakpointManager();

		const char *arg = argv[0];
		if (arg[0] == '*') {
			g_debugger.ClearAllBreakpoints();
			g_sim.NotifyEvent(kATSimEvent_CPUPCBreakpointsUpdated);
			ATConsoleWrite("All breakpoints cleared.\n");
		} else {
			char *argend = (char *)arg;
			unsigned long useridx = strtoul(arg, &argend, 10);
			sint32 sysidx = g_debugger.LookupUserBreakpoint(useridx);

			if (*argend || !g_debugger.ClearUserBreakpoint(useridx))
				ATConsoleWrite("Invalid breakpoint index.\n");
			else {
				g_sim.NotifyEvent(kATSimEvent_CPUPCBreakpointsUpdated);
				ATConsolePrintf("Breakpoint %u cleared.\n", useridx);
			}
		}
	}
}

void ATConsoleCmdBreakptAccess(int argc, const char *const *argv) {
	ATDebuggerCmdName cmdAccessMode(true);
	ATDebuggerCmdExprAddr cmdAddress(false, true, true);
	ATDebuggerCmdLength cmdLength(1, false, &cmdAddress);
	ATDebuggerCmdQuotedString command(false);

	ATDebuggerCmdParser(argc, argv) >> cmdAccessMode >> cmdAddress >> cmdLength >> command >> 0;

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
		const uint32 address = cmdAddress.GetValue();
		const uint32 length = cmdLength;

		if (length == 0) {
			ATConsoleWrite("Invalid breakpoint range length.\n");
			return;
		}

		uint32 sysidx;

		const char *modestr = readMode ? "read" : "write";

		if (length > 1) {
			sysidx = bpm->SetAccessRangeBP(address, length, readMode, !readMode);

			const uint32 useridx = g_debugger.RegisterSystemBreakpoint(sysidx, NULL, command.IsValid() ? command->c_str() : NULL, false);
			ATConsolePrintf("Breakpoint %u set on %s at %04X-%04X.\n", useridx, modestr, address, address + length - 1);
		} else {
			sysidx = bpm->SetAccessBP(address, readMode, !readMode);

			const uint32 useridx = g_debugger.RegisterSystemBreakpoint(sysidx, NULL, command.IsValid() ? command->c_str() : NULL, false);
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
	VDStringA file;
	uint32 lineno;
	for(vdfastvector<uint32>::const_iterator it(indices.begin()), itEnd(indices.end()); it != itEnd; ++it) {
		const uint32 useridx = *it;
		uint32 sysidx = g_debugger.LookupUserBreakpoint(useridx);

		ATBreakpointInfo info;

		line.sprintf("%3u  ", useridx);

		if (sysidx) {
			VDVERIFY(bpm->GetInfo(sysidx, info));
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
				line.append_sprintf("%s     ", g_debugger.GetAddressText(info.mAddress, false, true).c_str());
		} else
			line += "deferred     ";

		if (g_debugger.GetBreakpointSourceLocation(useridx, file, lineno))
			line.append_sprintf("  `%s:%u`", file.c_str(), lineno);

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

void ATConsoleCmdBreakptSector(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdExprNum num(true, false, 0, 65535);
	parser >> num >> 0;

	ATDiskEmulator& disk = g_sim.GetDiskDrive(0);
	if (num.IsStar()) {
		disk.SetSectorBreakpoint(-1);
		ATConsolePrintf("Disk sector breakpoint is disabled.\n");
	} else {
		int v = num.GetValue();

		disk.SetSectorBreakpoint(v);
		ATConsolePrintf("Disk sector breakpoint is now %d.\n", v);
	}
}

void ATConsoleCmdBreakptExpr(int argc, const char *const *argv) {
	ATDebuggerCmdString expr(true);
	ATDebuggerCmdQuotedString command(false);
	ATDebuggerCmdParser(argc, argv) >> expr >> command >> 0;

	VDStringA s(expr->c_str());

	vdautoptr<ATDebugExpNode> node;
	
	try {
		node = ATDebuggerParseExpression(s.c_str(), &g_debugger, ATGetDebugger()->GetExprOpts());
	} catch(ATDebuggerExprParseException& ex) {
		ATConsolePrintf("Unable to parse expression: %s\n", ex.c_str());
		return;
	}

	const uint32 useridx = g_debugger.SetConditionalBreakpoint(node.release(), command.IsValid() ? command->c_str() : NULL, false);
	g_sim.NotifyEvent(kATSimEvent_CPUPCBreakpointsUpdated);
	
	VDStringA condstr;

	ATDebugExpNode *cond = g_debugger.GetBreakpointCondition(useridx);

	if (cond)
		cond->ToString(condstr);

	ATBreakpointInfo info = {};

	VDVERIFY(g_debugger.GetBreakpointInfo(useridx, info));

	VDStringA msg;
	msg.sprintf("Breakpoint %u set ", useridx);

	if (info.mbBreakOnRead || info.mbBreakOnWrite) {
		if (info.mLength > 1)
			msg.append_sprintf("on %s to range $%04X-$%04X", info.mbBreakOnWrite ? "write" : "read", info.mAddress, info.mAddress + info.mLength - 1);
		else if (info.mbBreakOnRead)
			msg.append_sprintf("on read from $%04X", info.mAddress);
		else if (info.mbBreakOnWrite)
			msg.append_sprintf("on write to $%04X", info.mAddress);
	} else {
		msg.append_sprintf("at PC=$%04X", info.mAddress);
	}

	if (cond) {
		VDStringA condstr;

		cond->ToString(condstr);
		msg.append_sprintf(" with condition: %s\n", condstr.c_str());
	} else
		msg += ".\n";

	ATConsoleWrite(msg.c_str());
}

void ATConsoleCmdUnassemble(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdExprAddr address(false, false);
	ATDebuggerCmdLength length(20, false, &address);

	parser >> address >> length >> 0;

	ATCPUEmulator& cpu = g_sim.GetCPU();
	uint32 addr;
	
	if (address.IsValid())
		addr = address.GetValue();
	else
		addr = g_debugger.GetContinuationAddress();

	uint8 bank = (uint8)(addr >> 16);
	uint32 n = length;
	for(uint32 i=0; i<n; ++i) {
		addr = ATDisassembleInsn(addr, bank);

		if ((i & 15) == 15 && ATConsoleCheckBreak())
			break;
	}

	g_debugger.SetContinuationAddress(((uint32)bank << 16) + (addr & 0xffff));
}

void ATConsoleCmdRegisters(int argc, const char *const *argv) {
	ATCPUEmulator& cpu = g_sim.GetCPU();

	if (!argc) {
		cpu.DumpStatus(true);
		return;
	}

	if (argc < 2) {
		ATConsoleWrite("Syntax: r reg value\n");
		return;
	}

	uint16 v = (uint16)g_debugger.ResolveSymbolThrow(argv[1]);

	VDStringSpanA regName(argv[0]);
	bool resetContinuationAddr = false;
	bool is816 = cpu.GetCPUMode() == kATCPUMode_65C816;
	bool isM16 = !(cpu.GetP() & AT6502::kFlagM);
	bool isX16 = !(cpu.GetP() & AT6502::kFlagX);

	if (regName == "pc") {
		g_debugger.SetPC(v);
		resetContinuationAddr = true;
	} else if (regName == "x") {
		cpu.SetX((uint8)v);

		if (isX16)
			cpu.SetXH((uint8)(v >> 8));
	} else if (regName == "y") {
		cpu.SetY((uint8)v);

		if (isX16)
			cpu.SetYH((uint8)(v >> 8));
	} else if (regName == "s") {
		cpu.SetS((uint8)v);
	} else if (regName == "p") {
		cpu.SetP((uint8)v);
	} else if (regName == "a") {
		cpu.SetA((uint8)v);
	} else if (regName == "c") {
		cpu.SetA((uint8)v);
		if (isM16)
			cpu.SetAH((uint8)(v >> 8));
	} else if (regName == "d") {
		cpu.SetD(v);
	} else if (regName == "k" || regName == "pbr") {
		cpu.SetK((uint8)v);
	} else if (regName == "b" || regName == "dbr") {
		cpu.SetB((uint8)v);
	} else if (regName.size() == 3 && regName[0] == 'p' && regName[1] == '.') {
		uint8 p = cpu.GetP();
		uint8 mask;

		switch(regName[2]) {
			case 'n': mask = 0x80; break;
			case 'v': mask = 0x40; break;
			case 'm': mask = 0x20; break;
			case 'x': mask = 0x10; break;
			case 'd': mask = 0x08; break;
			case 'i': mask = 0x04; break;
			case 'z': mask = 0x02; break;
			case 'c': mask = 0x01; break;
			default:
				goto unknown;
		}

		if (v)
			p |= mask;
		else
			p &= ~mask;

		cpu.SetP(p);
	} else {
		goto unknown;
	}

	if (resetContinuationAddr)
		g_debugger.SetContinuationAddress(((uint32)cpu.GetK() << 16) + cpu.GetInsnPC());

	g_debugger.SendRegisterUpdate();
	return;

unknown:
	ATConsolePrintf("Unknown register '%s'\n", argv[0]);
}

void ATConsoleCmdDumpATASCII(int argc, const char *const *argv) {
	ATDebuggerCmdExprAddr address(true, true);
	ATDebuggerCmdLength length(128, false, &address);

	ATDebuggerCmdParser(argc, argv) >> address >> length >> 0;

	uint32 addr = (uint32)address.GetValue();
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
	ATDebuggerCmdExprAddr address(true, true);
	ATDebuggerCmdLength length(128, false, &address);

	ATDebuggerCmdParser(argc, argv) >> address >> length >> 0;

	uint32 addr = address.GetValue();
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
	ATDebuggerCmdExprAddr address(true, false);
	ATDebuggerCmdLength length(128, false, &address);

	ATDebuggerCmdParser(argc, argv) >> address >> length >> 0;

	uint32 addr = address.IsValid() ? address.GetValue() : g_debugger.GetContinuationAddress();
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

	g_debugger.SetContinuationAddress(atype + (addr & kATAddressOffsetMask));
}

void ATConsoleCmdDumpWords(int argc, const char *const *argv) {
	ATDebuggerCmdExprAddr address(true, false);
	ATDebuggerCmdLength length(64, false, &address);

	ATDebuggerCmdParser(argc, argv) >> address >> length >> 0;

	uint32 addr = address.IsValid() ? address.GetValue() : g_debugger.GetContinuationAddress();
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

	g_debugger.SetContinuationAddress(atype + (addr & kATAddressOffsetMask));
}

void ATConsoleCmdDumpDwords(int argc, const char *const *argv) {
	ATDebuggerCmdExprAddr address(true, true);
	ATDebuggerCmdLength length(64, false, &address);

	ATDebuggerCmdParser(argc, argv) >> address >> length >> 0;

	uint32 addr = address.GetValue();
	uint32 atype = addr & kATAddressSpaceMask;

	uint32 rows = (length + 3) >> 2;

	uint8 buf[16];

	while(rows--) {
		if (15 == (rows & 15) && ATConsoleCheckBreak())
			break;

		for(int i=0; i<16; ++i) {
			uint8 v = g_sim.DebugGlobalReadByte(atype + ((addr + i) & kATAddressOffsetMask));
			buf[i] = v;
		}

		ATConsolePrintf("%s: %08X %08X %08X %08X\n"
			, g_debugger.GetAddressText(addr, false).c_str()
			, VDReadUnalignedLEU32(buf + 0)
			, VDReadUnalignedLEU32(buf + 4)
			, VDReadUnalignedLEU32(buf + 8)
			, VDReadUnalignedLEU32(buf + 12)
			);

		addr += 16;
	}

	g_debugger.SetContinuationAddress(atype + (addr & kATAddressOffsetMask));
}

void ATConsoleCmdDumpFloats(int argc, const char *const *argv) {
	ATDebuggerCmdExprAddr address(true, false);
	ATDebuggerCmdLength length(1, false, &address);

	ATDebuggerCmdParser(argc, argv) >> address >> length >> 0;

	uint32 addr = address.IsValid() ? address.GetValue() : g_debugger.GetContinuationAddress();
	uint32 atype = addr & kATAddressSpaceMask;
	uint8 data[6];

	uint32 rows = length;
	while(rows--) {
		if (15 == (rows & 15) && ATConsoleCheckBreak())
			break;

		for(int i=0; i<6; ++i) {
			data[i] = g_sim.DebugGlobalReadByte(atype + ((addr + i) & kATAddressOffsetMask));
		}

		ATConsolePrintf("%s: %02X %02X %02X %02X %02X %02X  %.10g\n"
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

	g_debugger.SetContinuationAddress(atype + (addr & kATAddressOffsetMask));
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

void ATConsoleCmdLogFilterEnable(int argc, const char *const *argv) {
	ATDebuggerCmdName name(true);
	ATDebuggerCmdParser(argc, argv) >> name >> 0;

	bool star = (*name) == "*";

	for(ATDebuggerLogChannel *p = ATDebuggerLogChannel::GetFirstChannel();
		p;
		p = ATDebuggerLogChannel::GetNextChannel(p))
	{
		if (star || !vdstricmp(p->GetName(), name->c_str())) {
			if (!p->IsEnabled() || p->GetTagFlags()) {
				p->SetEnabled(true);
				p->SetTagFlags(0);
				ATConsolePrintf("Enabled logging channel: %s\n", p->GetName());
			}

			if (!star)
				return;
		}
	}

	if (!star)
		ATConsolePrintf("Unknown logging channel: %s\n", name->c_str());
}

void ATConsoleCmdLogFilterDisable(int argc, const char *const *argv) {
	ATDebuggerCmdName name(true);
	ATDebuggerCmdParser(argc, argv) >> name >> 0;

	bool star = (*name) == "*";

	for(ATDebuggerLogChannel *p = ATDebuggerLogChannel::GetFirstChannel();
		p;
		p = ATDebuggerLogChannel::GetNextChannel(p))
	{
		if (star || !vdstricmp(p->GetName(), name->c_str())) {
			if (p->IsEnabled()) {
				p->SetEnabled(false);
				ATConsolePrintf("Disabled logging channel: %s\n", p->GetName());
			}

			if (!star)
				return;
		}
	}

	if (star)
		return;

	ATConsolePrintf("Unknown logging channel: %s\n", name->c_str());
}

void ATConsoleCmdLogFilterTag(int argc, const char *const *argv) {
	ATDebuggerCmdSwitch swTimestamp("t", false);
	ATDebuggerCmdSwitch swCassettePos("c", false);
	ATDebuggerCmdName name(true);
	ATDebuggerCmdParser(argc, argv) >> swTimestamp >> swCassettePos >> name >> 0;

	uint32 flags = 0;

	if (swTimestamp)
		flags |= kATTagFlags_Timestamp;

	if (swCassettePos)
		flags |= kATTagFlags_CassettePos;

	if (!flags)
		flags = kATTagFlags_Timestamp;

	bool star = (*name) == "*";

	for(ATDebuggerLogChannel *p = ATDebuggerLogChannel::GetFirstChannel();
		p;
		p = ATDebuggerLogChannel::GetNextChannel(p))
	{
		if (star || !vdstricmp(p->GetName(), name->c_str())) {
			if (!p->IsEnabled() || p->GetTagFlags() != flags) {
				p->SetEnabled(true);
				p->SetTagFlags(flags);
				ATConsolePrintf("Enabled logging channel with tagging: %s\n", p->GetName());
			}

			if (!star)
				return;
		}
	}

	if (star)
		return;

	ATConsolePrintf("Unknown logging channel: %s\n", name->c_str());
}

namespace {
	struct ChannelSortByName {
		bool operator()(const ATDebuggerLogChannel *x, const ATDebuggerLogChannel *y) const {
			return vdstricmp(x->GetName(), y->GetName()) < 0;
		}
	};
}

void ATConsoleCmdLogFilterList(int argc, const char *const *argv) {
	ATDebuggerCmdParser(argc, argv) >> 0;

	typedef vdfastvector<ATDebuggerLogChannel *> Channels;
	Channels channels;

	for(ATDebuggerLogChannel *p = ATDebuggerLogChannel::GetFirstChannel();
		p;
		p = ATDebuggerLogChannel::GetNextChannel(p))
	{
		channels.push_back(p);
	}

	std::sort(channels.begin(), channels.end(), ChannelSortByName());

	for(Channels::const_iterator it(channels.begin()), itEnd(channels.end());
		it != itEnd;
		++it)
	{
		ATDebuggerLogChannel *p = *it;
		ATConsolePrintf("%-10s  %-3s  %s\n", p->GetName(), p->IsEnabled() ? "on" : "off", p->GetDesc());
	}
}

void ATConsoleCmdVerifierTargetAdd(int argc, const char *const *argv) {
	ATDebuggerCmdExprAddr addr(false, true);
	ATDebuggerCmdParser(argc, argv) >> addr >> 0;

	ATCPUVerifier *verifier = g_sim.GetVerifier();

	if (!verifier) {
		ATConsoleWrite("Verifier is not active.\n");
		return;
	}

	verifier->AddAllowedTarget(addr.GetValue());
}

void ATConsoleCmdVerifierTargetClear(int argc, const char *const *argv) {
	ATDebuggerCmdExprAddr addr(false, true, true);
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
		verifier->RemoveAllowedTarget(addr.GetValue());
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

void ATConsoleCmdWatchByte(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdExprAddr addr(false, true);
	parser >> addr >> 0;

	int idx = g_debugger.AddWatch(addr.GetValue(), 1);

	if (idx >= 0)
		ATConsolePrintf("Watch entry %d set.\n", idx);
	else
		ATConsoleWrite("No free watch slots available.\n");
}

void ATConsoleCmdWatchWord(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdExprAddr addr(false, true);
	parser >> addr >> 0;

	int idx = g_debugger.AddWatch(addr.GetValue(), 2);

	if (idx >= 0)
		ATConsolePrintf("Watch entry %d set.\n", idx);
	else
		ATConsoleWrite("No free watch slots available.\n");
}

void ATConsoleCmdWatchExpr(int argc, const char *const *argv) {
	if (!argc)
		return;

	vdautoptr<ATDebugExpNode> node;

	try {
		node = ATDebuggerParseExpression(argv[0], &g_debugger, ATGetDebugger()->GetExprOpts());
	} catch(ATDebuggerExprParseException& ex) {
		ATConsolePrintf("Unable to parse expression: %s\n", ex.c_str());
		return;
	}

	int idx = g_debugger.AddWatchExpr(node);

	if (idx >= 0) {
		node.release();

		ATConsolePrintf("Watch entry %d set.\n", idx);
	} else
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
		if (g_debugger.GetWatchInfo(i, winfo)) {
			if (winfo.mpExpr) {
				VDStringA s;
				winfo.mpExpr->ToString(s);
				ATConsolePrintf("%d  %s\n", i, s.c_str());
			} else {
				ATConsolePrintf("%d  %2d  %s\n", i, winfo.mLen, g_debugger.GetAddressText(winfo.mAddress, false, true).c_str());
			}
		}
	}
}

void ATConsoleCmdSymbolAdd(int argc, const char *const *argv) {
	ATDebuggerCmdName name(true);
	ATDebuggerCmdExprAddr addr(false, true);
	ATDebuggerCmdLength len(1, false, &addr);

	ATDebuggerCmdParser(argc, argv) >> name, addr, len;

	VDStringA s(name->c_str());

	for(VDStringA::iterator it(s.begin()), itEnd(s.end()); it != itEnd; ++it)
		*it = toupper((unsigned char)*it);

	g_debugger.AddCustomSymbol(addr.GetValue(), len, s.c_str(), kATSymbol_Any);
}

void ATConsoleCmdSymbolClear(int argc, const char *const *argv) {
	g_debugger.UnloadSymbols(ATDebugger::kModuleId_Manual);
	ATConsoleWrite("Custom symbols cleared.\n");
}

void ATConsoleCmdSymbolRemove(int argc, const char *const *argv) {
	ATDebuggerCmdExprAddr addr(false, true);

	ATDebuggerCmdParser(argc, argv) >> addr;

	g_debugger.RemoveCustomSymbol(addr.GetValue());
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
	try {
		if (argc < 2)
			goto usage;

		vdautoptr<ATDebugExpNode> node(ATDebuggerParseExpression(argv[0], ATGetDebugger(), ATGetDebugger()->GetExprOpts()));

		sint32 v;
		if (!node->Evaluate(v, g_debugger.GetEvalContext())) {
			ATConsolePrintf("Cannot evaluate '%s' in this context.\n", argv[0]);
			return;
		}

		uint32 addr = (uint32)v;

		vdfastvector<uint8> data(argc - 1);
		for(int i=1; i<argc; ++i) {
			vdautoptr<ATDebugExpNode> node(ATDebuggerParseExpression(argv[i], ATGetDebugger(), ATGetDebugger()->GetExprOpts()));

			sint32 result;
			if (!node->Evaluate(result, g_debugger.GetEvalContext()))
				throw MyError("Cannot evaluate '%s' in this context.", argv[i]);

			if (result < 0 || result > 255)
				throw MyError("Value out of range: %s = %d", argv[i], result);

			data[i-1] = (uint8)result;
		}

		ATCPUEmulatorMemory& mem = g_sim.GetCPUMemory();
		for(int i=1; i<argc; ++i) {
			uint32 ea = addr + (i - 1);

			if (addr & kATAddressSpaceMask)
				g_sim.DebugGlobalWriteByte(ea, data[i - 1]);
			else
				mem.ExtWriteByte((uint16)ea, (uint8)(ea >> 16), data[i - 1]);
		}
	} catch(const ATDebuggerExprParseException& ex) {
		ATConsolePrintf("Unable to parse expression: %s\n", ex.c_str());
		return;
	}
	return;

usage:
	ATConsoleWrite("Syntax: e <address> <byte> [<byte>...]\n");
}

void ATConsoleCmdFill(int argc, const char *const *argv) {
	ATDebuggerCmdExprAddr addrarg(true, true);
	ATDebuggerCmdLength lenarg(0, true, &addrarg);
	ATDebuggerCmdExprNum val(true, true, 0, 255);
	ATDebuggerCmdParser parser(argc, argv);
	
	parser >> addrarg >> lenarg >> val;

	vdfastvector<uint8> buf;
	uint8 c = val.GetValue();

	for(;;) {
		buf.push_back(c);

		ATDebuggerCmdExprNum val(false, true, 0, 255);
		parser >> val;

		if (!val.IsValid())
			break;

		c = val.GetValue();
	}

	parser >> 0;

	ATCPUEmulatorMemory& mem = g_sim.GetCPUMemory();
	uint32 len = lenarg;
	uint32 addrspace = addrarg.GetValue() & kATAddressSpaceMask;
	uint32 addroffset = addrarg.GetValue() & kATAddressOffsetMask;

	const uint8 *patstart = buf.data();
	const uint8 *patend = patstart + buf.size();
	const uint8 *patsrc = patstart;
	for(uint32 len = lenarg; len; --len) {
		g_sim.DebugGlobalWriteByte(addrspace + addroffset, *patsrc);

		if (++patsrc == patend)
			patsrc = patstart;

		addroffset = (addroffset + 1) & kATAddressOffsetMask;
	}

	if (lenarg) {
		ATConsolePrintf("Filled %s-%s.\n"
		, g_debugger.GetAddressText(addrarg.GetValue(), false).c_str()
		, g_debugger.GetAddressText(addrspace + ((addroffset - 1) & kATAddressOffsetMask), false).c_str());
	}
}

void ATConsoleCmdFillExp(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdExprAddr addrarg(true, true);
	ATDebuggerCmdLength lenarg(0, true, &addrarg);
	ATDebuggerCmdExpr valexpr(true);
	
	parser >> addrarg >> lenarg >> valexpr >> 0;

	ATCPUEmulatorMemory& mem = g_sim.GetCPUMemory();
	uint32 len = lenarg;
	uint32 addrspace = addrarg.GetValue() & kATAddressSpaceMask;
	uint32 addroffset = addrarg.GetValue() & kATAddressOffsetMask;

	ATDebugExpEvalContext ctx = g_debugger.GetEvalContext();
	ctx.mbAccessValid = true;
	ctx.mbAccessWriteValid = true;
	ctx.mAccessValue = 0;

	ATDebugExpNode *xpn = valexpr.GetValue();

	for(uint32 len = lenarg; len; --len) {
		sint32 v;
		uint32 addr = addrspace + addroffset;

		ctx.mAccessAddress = addr;

		if (!xpn->Evaluate(v, ctx))
			throw MyError("Evaluation error at %s.", g_debugger.GetAddressText(addr, true).c_str());

		++ctx.mAccessValue;

		g_sim.DebugGlobalWriteByte(addr, (uint8)v);

		addroffset = (addroffset + 1) & kATAddressOffsetMask;
	}

	if (lenarg) {
		ATConsolePrintf("Filled %s-%s.\n"
		, g_debugger.GetAddressText(addrarg.GetValue(), false).c_str()
		, g_debugger.GetAddressText(addrspace + ((addroffset - 1) & kATAddressOffsetMask), false).c_str());
	}
}

void ATConsoleCmdMove(int argc, const char *const *argv) {
	ATDebuggerCmdExprAddr srcaddrarg(true, true);
	ATDebuggerCmdLength lenarg(0, true, &srcaddrarg);
	ATDebuggerCmdExprAddr dstaddrarg(true, true);
	ATDebuggerCmdParser parser(argc, argv);
	
	parser >> srcaddrarg >> lenarg >> dstaddrarg >> 0;

	ATCPUEmulatorMemory& mem = g_sim.GetCPUMemory();
	uint32 len = lenarg;
	uint32 srcspace = srcaddrarg.GetValue() & kATAddressSpaceMask;
	uint32 srcoffset = srcaddrarg.GetValue() & kATAddressOffsetMask;
	uint32 dstspace = dstaddrarg.GetValue() & kATAddressSpaceMask;
	uint32 dstoffset = dstaddrarg.GetValue() & kATAddressOffsetMask;

	if (srcspace == dstspace && dstoffset >= srcoffset && dstoffset < srcoffset + len) {
		srcoffset = (srcoffset + len) & kATAddressOffsetMask;
		dstoffset = (dstoffset + len) & kATAddressOffsetMask;

		while(len--) {
			srcoffset = (srcoffset - 1) & kATAddressOffsetMask;
			dstoffset = (dstoffset - 1) & kATAddressOffsetMask;

			const uint8 c = g_sim.DebugGlobalReadByte(srcspace + srcoffset);
			g_sim.DebugGlobalWriteByte(dstspace + dstoffset, c);
		}
	} else {
		while(len--) {
			const uint8 c = g_sim.DebugGlobalReadByte(srcspace + srcoffset);
			g_sim.DebugGlobalWriteByte(dstspace + dstoffset, c);

			srcoffset = (srcoffset + 1) & kATAddressOffsetMask;
			dstoffset = (dstoffset + 1) & kATAddressOffsetMask;
		}
	}
}

void ATConsoleCmdHeatMapDumpAccesses(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdExprAddr addrarg(false, false);
	ATDebuggerCmdLength lenarg(0, false, &addrarg);
	parser >> addrarg >> lenarg >> 0;

	if (!g_sim.IsHeatMapEnabled())
		throw MyError("Heat map is not enabled.\n");

	ATCPUHeatMap& heatmap = *g_sim.GetHeatMap();

	uint32 addr = addrarg.IsValid() ? 0 : addrarg.GetValue() & 0xffff;
	uint32 len = lenarg.IsValid() ? lenarg : 0x10000 - addrarg.GetValue();

	uint8 prevflags = 0;
	uint32 rangeaddr = 0;
	if (len) {
		for(;;) {
			uint8 flags = 0;

			if (len)
				flags = heatmap.GetMemoryAccesses(addr);

			if (flags != prevflags) {
				if (prevflags) {
					uint32 end = addr;

					if (end <= rangeaddr)
						end += 0x10000;

					ATConsolePrintf("$%04X-%04X (%4.0fK) %s %s\n"
						, rangeaddr
						, (addr - 1) & 0xffff
						, (float)(end - rangeaddr) / 1024.0f
						, prevflags & ATCPUHeatMap::kAccessRead ? "read" : "    "
						, prevflags & ATCPUHeatMap::kAccessWrite ? " write" : "     ");
				}

				rangeaddr = addr;
				prevflags = flags;
			}

			if (!len)
				break;

			addr = (addr + 1) & 0xffff;
			--len;
		}
	}
}

void ATConsoleCmdHeatMapClear(ATDebuggerCmdParser& parser) {
	parser >> 0;

	if (!g_sim.IsHeatMapEnabled())
		throw MyError("Heat map is not enabled.\n");

	ATCPUHeatMap& heatmap = *g_sim.GetHeatMap();
	heatmap.Reset();

	ATConsoleWrite("Heat map reset.\n");
}

void ATDebuggerPrintHeatMapState(VDStringA& s, uint32 code) {
	switch(code & ATCPUHeatMap::kTypeMask) {
		case ATCPUHeatMap::kTypeUnknown:
		default:
			s = "Unknown";
			break;

		case ATCPUHeatMap::kTypePreset:
			s.sprintf("Preset from $%04X", code & 0xFFFF);
			break;

		case ATCPUHeatMap::kTypeImm:
			s.sprintf("Immediate from insn at $%04X", code & 0xFFFF);
			break;

		case ATCPUHeatMap::kTypeComputed:
			s.sprintf("Computed by insn at $%04X", code & 0xFFFF);
			break;
	}
}

void ATConsoleCmdHeatMapDumpMemory(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdExprAddr addrarg(false, true);
	ATDebuggerCmdLength lenarg(8, false, &addrarg);
	parser >> addrarg >> lenarg >> 0;

	if (!g_sim.IsHeatMapEnabled())
		throw MyError("Heat map is not enabled.\n");

	ATCPUHeatMap& heatmap = *g_sim.GetHeatMap();

	uint32 addr = addrarg.GetValue();
	uint32 len = lenarg;
	VDStringA s;
	for(uint32 i=0; i<len; ++i) {
		uint32 addr2 = (addr + i) & 0xffff;

		ATDebuggerPrintHeatMapState(s, heatmap.GetMemoryStatus(addr2));
		uint8 flags = heatmap.GetMemoryAccesses(addr2);
		ATConsolePrintf("$%04X: %c%c | %s\n"
			, addr2
			, flags & ATCPUHeatMap::kAccessRead  ? 'R' : ' '
			, flags & ATCPUHeatMap::kAccessWrite ? 'W' : ' '
			, s.c_str());
	}
}

void ATConsoleCmdHeatMapEnable(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdBool enable(true);
	parser >> enable >> 0;

	if (g_sim.IsHeatMapEnabled() != enable) {
		g_sim.SetHeatMapEnabled(enable);

		ATConsolePrintf("Heat map is now %s.\n", enable ? "enabled" : "disabled");
	}
}

void ATConsoleCmdHeatMapRegisters(ATDebuggerCmdParser& parser) {
	parser >> 0;

	if (!g_sim.IsHeatMapEnabled())
		throw MyError("Heat map is not enabled.\n");

	ATCPUHeatMap& heatmap = *g_sim.GetHeatMap();
	ATCPUEmulator& cpu = g_sim.GetCPU();

	VDStringA s;

	ATDebuggerPrintHeatMapState(s, heatmap.GetAStatus());
	ATConsolePrintf("A = $%02X (%s)\n", cpu.GetA(), s.c_str());

	ATDebuggerPrintHeatMapState(s, heatmap.GetXStatus());
	ATConsolePrintf("X = $%02X (%s)\n", cpu.GetX(), s.c_str());

	ATDebuggerPrintHeatMapState(s, heatmap.GetYStatus());
	ATConsolePrintf("Y = $%02X (%s)\n", cpu.GetY(), s.c_str());
}

void ATConsoleCmdSearch(int argc, const char *const *argv) {
	ATDebuggerCmdExprAddr addrarg(true, true);
	ATDebuggerCmdLength lenarg(0, true, &addrarg);
	ATDebuggerCmdExprNum val(true, true, 0, 255);
	ATDebuggerCmdParser parser(argc, argv);
	
	parser >> addrarg >> lenarg >> val;

	vdfastvector<uint8> buf;
	uint8 c = val.GetValue();

	for(;;) {
		buf.push_back(c);

		ATDebuggerCmdExprNum val(false, true, 0, 255);
		parser >> val;

		if (!val.IsValid())
			break;

		c = val.GetValue();
	}

	parser >> 0;

	ATCPUEmulatorMemory& mem = g_sim.GetCPUMemory();
	uint32 len = lenarg;
	uint32 addrspace = addrarg.GetValue() & kATAddressSpaceMask;
	uint32 addroffset = addrarg.GetValue() & kATAddressOffsetMask;

	const uint8 *const pat = buf.data();
	const uint32 patlen = buf.size();
	uint32 patoff = 0;

	if (len < patlen)
		return;

	len -= (patlen - 1);

	for(uint32 len = lenarg; len && !ATConsoleCheckBreak(); --len) {
		uint8 v = g_sim.DebugGlobalReadByte(addrspace + ((addroffset + patoff) & kATAddressOffsetMask));

		if (v == pat[patoff]) {
			bool validMatch = true;

			for(uint32 i = patoff + 1; i < patlen; ++i) {
				uint8 v2 = g_sim.DebugGlobalReadByte(addrspace + ((addroffset + i) & kATAddressOffsetMask));

				if (v2 != pat[i]) {
					validMatch = false;
					patoff = i;
					break;
				}
			}

			if (validMatch) {
				for(uint32 i = 0; i < patoff; ++i) {
					uint8 v3 = g_sim.DebugGlobalReadByte(addrspace + ((addroffset + i) & kATAddressOffsetMask));

					if (v3 != pat[i]) {
						validMatch = false;
						patoff = i;
						break;
					}
				}

				if (validMatch)
					ATConsolePrintf("Match found at: %s\n", g_debugger.GetAddressText(addrspace + (addroffset & kATAddressOffsetMask), false).c_str());
			}
		}

		++addroffset;
	}
}

class ATDebuggerCmdStaticTrace : public vdrefcounted<IATDebuggerActiveCommand> {
public:
	ATDebuggerCmdStaticTrace(uint32 initialpc, uint32 rangelo, uint32 rangehi);
	virtual bool IsBusy() const { return true; }
	virtual const char *GetPrompt() { return NULL; }

	virtual void BeginCommand(IATDebugger *debugger);
	virtual void EndCommand();
	virtual bool ProcessSubCommand(const char *s);

protected:
	enum {
		kFlagTraced = 0x01,
		kFlagLabeled = 0x02
	};

	uint32 mRangeLo;
	uint32 mRangeHi;

	vdfastdeque<uint32> mPCQueue;
	VDStringA mSymName;

	uint8 mSeenFlags[65536];
};

ATDebuggerCmdStaticTrace::ATDebuggerCmdStaticTrace(uint32 initialpc, uint32 rangelo, uint32 rangehi)
	: mRangeLo(rangelo)
	, mRangeHi(rangehi)
{
	memset(mSeenFlags, 0, sizeof mSeenFlags);
	mPCQueue.push_back(initialpc);
}

void ATDebuggerCmdStaticTrace::BeginCommand(IATDebugger *debugger) {
}

void ATDebuggerCmdStaticTrace::EndCommand() {
}

bool ATDebuggerCmdStaticTrace::ProcessSubCommand(const char *) {
	if (mPCQueue.empty())
		return false;

	uint32 pc = mPCQueue.front();
	mPCQueue.pop_front();

	ATConsolePrintf("Tracing $%04X\n", pc);

	while(!(mSeenFlags[pc] & kFlagTraced)) {
		mSeenFlags[pc] |= kFlagTraced;

		uint8 opcode = g_sim.DebugReadByte(pc);
		uint32 len = ATGetOpcodeLength(opcode);

		pc += len;
		pc &= 0xffff;

		// check for interesting opcodes
		sint32 target = -1;

		switch(opcode) {
			case 0x00:		// BRK
			case 0x40:		// RTI
			case 0x60:		// RTS
			case 0x6C:		// JMP (abs)
				goto stop_trace;

			case 0x4C:		// JMP abs
				pc = g_sim.DebugReadWord((pc - 2) & 0xffff);
				break;

			case 0x20:		// JSR abs
				target = g_sim.DebugReadWord((pc - 2) & 0xffff);
				break;

			case 0x10:		// branches
			case 0x30:
			case 0x50:
			case 0x70:
			case 0x90:
			case 0xb0:
			case 0xd0:
			case 0xf0:
				target = (pc + (sint8)g_sim.DebugReadByte((pc - 1) & 0xffff)) & 0xffff;
				break;
		}

		if (target >= 0 && !(mSeenFlags[target] & kFlagLabeled)) {
			mSeenFlags[target] |= kFlagLabeled;

			ATSymbol sym;
			if (!g_debugger.LookupSymbol(target, kATSymbol_Any, sym)) {
				mSymName.sprintf("L%04X", target);
				g_debugger.AddCustomSymbol(target, 1, mSymName.c_str(), kATSymbol_Execute);

				if (!(mSeenFlags[target] & kFlagTraced))
					mPCQueue.push_back(target);
			}
		}
	}

stop_trace:
	return true;
}

void ATConsoleCmdStaticTrace(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdExprAddr baseAddr(false, true);
	ATDebuggerCmdExprAddr restrictBase(false, false);
	ATDebuggerCmdLength restrictLength(false, true, &restrictBase);

	parser >> baseAddr;

	parser >> restrictBase;
	if (restrictBase.IsValid())
		parser >> restrictLength;

	parser >> 0;

	uint32 rangeLo = 0;
	uint32 rangeHi = 0xFFFF;
	
	if (restrictBase.IsValid()) {
		rangeLo = restrictBase.GetValue();
		rangeHi = rangeLo + restrictLength;
	}

	vdrefptr<IATDebuggerActiveCommand> acmd(new ATDebuggerCmdStaticTrace(baseAddr.GetValue(), rangeLo, rangeHi));

	g_debugger.StartActiveCommand(acmd);
}

void ATConsoleCmdDumpDisplayList(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdExprAddr addrArg(false, false);
	ATDebuggerCmdSwitch noCollapseArg("n", false);

	parser >> noCollapseArg >> addrArg >> 0;

	uint16 addr = addrArg.IsValid() ? addrArg.GetValue() : g_sim.GetAntic().GetDisplayListPointer();

	VDStringA line;

	uint16 regionBase = (addr & 0xfc00);
	for(int i=0; i<500; ++i) {
		uint16 baseaddr = regionBase + (addr & 0x3ff);
		uint8 b = g_sim.DebugAnticReadByte(regionBase + (addr++ & 0x3ff));

		int count = 1;
		uint32 jumpAddr;

		if (((b & 0x40) && (b & 0x0f)) || (b & 15) == 1) {
			uint8 arg0 = g_sim.DebugAnticReadByte(regionBase + (addr++ & 0x3ff));
			uint8 arg1 = g_sim.DebugAnticReadByte(regionBase + (addr++ & 0x3ff));

			jumpAddr = arg0 + ((uint32)arg1 << 8);

			if ((b & 15) != 1 && !noCollapseArg) {
				while(i < 500) {
					if (g_sim.DebugAnticReadByte(regionBase + (addr & 0x3ff)) != b)
						break;

					if (g_sim.DebugAnticReadByte(regionBase + ((addr + 1) & 0x3ff)) != arg0)
						break;

					if (g_sim.DebugAnticReadByte(regionBase + ((addr + 2) & 0x3ff)) != arg1)
						break;

					++count;
					++i;
					addr += 3;
				}
			}
		} else if (!noCollapseArg) {
			while(i < 500 && g_sim.DebugAnticReadByte(regionBase + (addr & 0x3ff)) == b) {
				++count;
				++i;
				++addr;
			}
		}

		line.sprintf("  %04X: ", baseaddr);

		if (!noCollapseArg) {
			if (count > 1)
				line.append_sprintf("x%-3u ", count);
			else
				line += "     ";
		}

		switch(b & 15) {
			case 0:
				line.append_sprintf("blank%s %d", b&128 ? ".i" : "", ((b >> 4) & 7) + 1);
				break;
			case 1:
				if (b & 64) {
					line.append_sprintf("waitvbl%s %04X", b&128 ? ".i" : "", jumpAddr);
					line += '\n';
					ATConsoleWrite(line.c_str());
					return;
				} else {
					line.append_sprintf("jump%s%s %04X"
						, b&128 ? ".i" : ""
						, b&32 ? ".v" : ""
						, jumpAddr);

					regionBase = jumpAddr & 0xfc00;
					addr = jumpAddr;
				}
				break;
			default:
				line.append_sprintf("mode%s%s%s %X"
					, b&128 ? ".i" : ""
					, b&32 ? ".v" : ""
					, b&16 ? ".h" : ""
					, b&15);

				if (b & 64)
					line.append_sprintf(" @ %04X", jumpAddr);
				break;
		}

		line += '\n';
		ATConsoleWrite(line.c_str());
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
	ATDebuggerCmdSwitch switchJ("j", false);
	ATDebuggerCmdSwitchNumArg switchS("s", 0, 0x7FFFFFFF);
	ATDebuggerCmdNumber histLenArg(false, 0, 0x7FFFFFFF);
	ATDebuggerCmdName wildArg(false);

	ATDebuggerCmdParser(argc, argv) >> switchI >> switchC >> switchJ >> switchS >> histLenArg >> wildArg;

	int histlen = 32;
	const char *wild = NULL;
	bool compressed = switchC;
	bool interruptsOnly = switchI;
	const bool jumpsOnly = switchJ;
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
	const ATCPUMode cpuMode = cpu.GetCPUMode();
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

		if (jumpsOnly) {
			bool branch = false;

			switch(he.mOpcode[0]) {
				case 0x10:	// BPL
				case 0x30:	// BMI
				case 0x50:	// BVC
				case 0x70:	// BVS
				case 0x90:	// BCC
				case 0xB0:	// BCS
				case 0xD0:	// BNE
				case 0xF0:	// BEQ
				case 0x20:	// JSR abs
				case 0x4C:	// JMP abs
				case 0x6C:	// JMP (abs)
					branch = true;
					break;

				// 65C02/65C816
				case 0x7C:	// JMP (abs,X)
				case 0x80:	// BRA rel
					if (cpuMode != kATCPUMode_6502)
						branch = true;
					break;

				// 65C02 only
				case 0x07:	// RMBn
				case 0x17:
				case 0x27:
				case 0x37:
				case 0x47:
				case 0x57:
				case 0x67:
				case 0x77:
				case 0x87:	// SMBn
				case 0x97:
				case 0xA7:
				case 0xB7:
				case 0xC7:
				case 0xD7:
				case 0xE7:
				case 0xF7:
				case 0x0F:	// BBRn
				case 0x1F:
				case 0x2F:
				case 0x3F:
				case 0x4F:
				case 0x5F:
				case 0x6F:
				case 0x7F:
				case 0x8F:	// BBSn
				case 0x9F:
				case 0xAF:
				case 0xBF:
				case 0xCF:
				case 0xDF:
				case 0xEF:
				case 0xFF:
					if (cpuMode == kATCPUMode_65C02)
						branch = true;
					break;

				// 65C816 only
				case 0x22:	// JSL long
				case 0x5C:	// JML long
				case 0x82:	// BRL rel16
				case 0xDC:	// JML [abs]
				case 0xFC:	// JSR (abs,X)
					if (cpuMode == kATCPUMode_65C816)
						branch = true;
					break;
			}

			if (!branch)
				continue;
		}

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

void ATConsoleCmdDumpDsm(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdString filename(true);
	ATDebuggerCmdExprAddr addrArg(false, true);
	ATDebuggerCmdLength lenArg(0, true, &addrArg);
	ATDebuggerCmdSwitch codeBytesArg("c", false);
	ATDebuggerCmdSwitch pcAddrArg("p", false);
	ATDebuggerCmdSwitch noLabelsArg("n", false);
	ATDebuggerCmdSwitch lcopsArg("l", false);
	ATDebuggerCmdSwitch sepArg("s", false);
	ATDebuggerCmdSwitch tabsArg("t", false);

	parser >> codeBytesArg >> pcAddrArg >> noLabelsArg >> filename >> addrArg >> lenArg >> lcopsArg >> sepArg >> tabsArg >> 0;

	uint32 addr = addrArg.GetValue();
	uint32 addrEnd = addr + lenArg;

	if (addrEnd > 0x10000)
		addrEnd = addr;

	FILE *f = fopen(filename->c_str(), "w");
	if (!f) {
		ATConsolePrintf("Unable to open file for write: %s\n", filename->c_str());
		return;
	}

	const bool showLabels = !noLabelsArg;
	const bool showCodeBytes = codeBytesArg;
	const bool showPCAddress = pcAddrArg;
	const bool lowercaseOps = lcopsArg;
	const bool separateRoutines = sepArg;
	const bool useTabs = tabsArg;

	VDStringA s;
	VDStringA t;
	ATCPUHistoryEntry hent;
	ATDisassembleCaptureRegisterContext(hent);

	uint32 pc = addr;
	while(pc < addrEnd) {	
		ATDisassembleCaptureInsnContext(pc, hent.mK, hent);

		s.clear();
		pc = ATDisassembleInsn(s, hent, false, false, showPCAddress, showCodeBytes, showLabels, lowercaseOps, useTabs);

		while(!s.empty() && s.back() == ' ')
			s.pop_back();

		s += '\n';

		if (useTabs) {
			// detabify
			t.clear();

			int pos = 0;
			int spaces = 0;
			for(VDStringA::const_iterator it(s.begin()), itEnd(s.end()); it != itEnd; ++it, ++pos) {
				const char c = *it;

				if (c == ' ') {
					++spaces;
					continue;
				}

				if (spaces) {
					if (spaces > 1) {
						int basepos = pos - spaces;
						int prespaces = -basepos & 3;

						if (prespaces > spaces)
							prespaces = spaces;

						int postspaces = spaces - prespaces;

						while(prespaces-- > 0)
							t += ' ';

						while(postspaces >= 4) {
							postspaces -= 4;
							t += '\t';
						}

						while(postspaces-- > 0)
							t += ' ';
					} else
						t += ' ';

					spaces = 0;
				}

				t += c;
			}

			s.swap(t);
		}

		fwrite(s.data(), s.size(), 1, f);

		if (separateRoutines) {
			switch(hent.mOpcode[0]) {
				case 0x40:	// RTI
				case 0x60:	// RTS
				case 0x4C:	// JMP abs
				case 0x6C:	// JMP (abs)
					fputc('\n', f);
					break;
			}
		}
	}

	fclose(f);

	ATConsolePrintf("Disassembled %04X-%04X to %s\n", addr, addrEnd-1, filename->c_str());
}

void ATConsoleCmdReadMem(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdExprAddr addressArg(true, true);
	ATDebuggerCmdLength lengthArg(1, false, &addressArg);
	ATDebuggerCmdName filename(true);
	parser >> filename >> addressArg >> lengthArg >> 0;

	uint32 addr = addressArg.GetValue();
	uint32 len = lengthArg.IsValid() ? (uint32)lengthArg : 0x0FFFFFFFU;

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
		case kATAddressSpace_RAM:
			limit = kATAddressSpace_RAM + 0x10000;
			break;
	}

	if (addr >= limit)
		throw MyError("Invalid start address: %s\n", g_debugger.GetAddressText(addr, false).c_str());

	if (len > limit - addr)
		len = limit - addr;

	FILE *f = fopen(filename->c_str(), "rb");
	if (!f) {
		ATConsolePrintf("Unable to open file for read: %s\n", filename->c_str());
		return;
	}

	uint32 ptr = addr;
	uint32 i = len;
	uint32 actual = 0;
	while(i--) {
		int ch = getc(f);
		if (ch < 0)
			break;

		g_sim.DebugGlobalWriteByte(ptr++, (uint8)ch);
		++actual;
	}

	fclose(f);

	ATConsolePrintf("Read %s-%s from %s\n"
		, g_debugger.GetAddressText(addr, false).c_str()
		, g_debugger.GetAddressText(addr + actual - 1, false).c_str()
		, filename->c_str());
}
void ATConsoleCmdWriteMem(int argc, const char *const *argv) {
	ATDebuggerCmdExprAddr addressArg(true, true);
	ATDebuggerCmdNumber lengthArg(true, 1, 16777216);
	ATDebuggerCmdName filename(true);
	ATDebuggerCmdParser(argc, argv) >> filename >> addressArg >> lengthArg >> 0;

	uint32 addr = addressArg.GetValue();
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
		case kATAddressSpace_RAM:
			limit = kATAddressSpace_RAM + 0x10000;
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

		if (*t || !index)
			throw MyError("Invalid index: %s\n", s);

		g_debugger.UnloadSymbolsByIndex(index - 1);
	}
}

void ATConsoleCmdAntic() {
	g_sim.GetAntic().DumpStatus();
}

void ATConsoleCmdBank() {
	uint8 portb = g_sim.GetBankRegister();
	ATConsolePrintf("Bank state: %02X\n", portb);

	ATMemoryMapState state;
	g_sim.GetMMU()->GetMemoryMapState(state);

	ATConsolePrintf("  Kernel ROM:    %s\n", state.mbKernelEnabled ? "enabled" : "disabled");
	ATConsolePrintf("  BASIC ROM:     %s\n", state.mbBASICEnabled ? "enabled" : "disabled");
	ATConsolePrintf("  CPU bank:      %s\n", state.mbExtendedCPU ? "enabled" : "disabled");
	ATConsolePrintf("  Antic bank:    %s\n", state.mbExtendedANTIC ? "enabled" : "disabled");
	ATConsolePrintf("  Self test ROM: %s\n", state.mbSelfTestEnabled ? "enabled" : "disabled");

	ATMMUEmulator *mmu = g_sim.GetMMU();
	ATConsolePrintf("Antic bank: $%06X\n", mmu->GetAnticBankBase());
	ATConsolePrintf("CPU bank:   $%06X\n", mmu->GetCPUBankBase());

	if (state.mAxlonBankMask)
		ATConsolePrintf("Axlon bank: $%02X ($%05X)\n", state.mAxlonBank, (uint32)state.mAxlonBank << 14);

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
		ATConsolePrintf("VTIMR4  POKEY timer 4                 %04X\n", (uint16)kdb5.VTIMR4);
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
		ATConsolePrintf("VTIMR4  POKEY timer 4                 %04X\n", (uint16)kdb.VTIMR4);
		ATConsolePrintf("VPIRQ   PBI device interrupt          %04X\n", (uint16)kdb.VPIRQ);
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

	g_debugger.AddModule(0, 0x10000, pSymbolStore, NULL, NULL);

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

	g_debugger.AddModule(0xD800, 0x2800, symbols, "Kernel", NULL);
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

	uint32 vsecBase = track * 18 + 1;

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

void ATConsoleCmdDiskDumpSec(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdSwitchNumArg driveSw("d", 1, 15, 1);
	ATDebuggerCmdExprNum sectorArg(true, false, 1, 65535);

	parser >> driveSw >> sectorArg >> 0;

	ATDiskEmulator& disk = g_sim.GetDiskDrive(driveSw.GetValue() - 1);
	IATDiskImage *image = disk.GetDiskImage();
	
	if (!image)
		throw MyError("No disk image is mounted for drive D%u:.", driveSw.GetValue());

	uint32 sector = sectorArg.GetValue();
	if (sector > image->GetVirtualSectorCount())
		throw MyError("Invalid sector count for disk image: %u.", sector);

	uint8 buf[512];
	uint32 len = image->ReadVirtualSector(sector - 1, buf, 512);

	ATConsolePrintf("Sector %d / $%X (%u bytes):\n", sector, sector, len);

	VDStringA line;
	for(uint32 i=0; i<len; i+=16) {
		line.sprintf("%03X:", i);

		uint32 count = std::min<uint32>(len - i, 16);
		for(uint32 j=0; j<count; ++j)
			line.append_sprintf("%c%02X", j==8 ? '-' : ' ', buf[i+j]);

		line.resize(4 + 3*16 + 1, ' ');
		line += '|';

		for(uint32 j=0; j<count; ++j) {
			uint8 c = buf[i+j];

			if (c < 0x20 || c >= 0x7F)
				c = '.';

			line += (char)c;
		}

		line.resize(4 + 3*16 + 2 + 16, ' ');
		line += '|';
		line += '\n';

		ATConsoleWrite(line.c_str());
	}
}

void ATConsoleCmdCasLogData(int argc, const char *const *argv) {
	ATCassetteEmulator& cas = g_sim.GetCassette();

	bool newSetting = !cas.IsLogDataEnabled();
	cas.SetLogDataEnable(newSetting);

	ATConsolePrintf("Verbose cassette read data logging is now %s.\n", newSetting ? "enabled" : "disabled");
}

void ATConsoleCmdDumpPIAState() {
	g_sim.GetPIA().DumpState();
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
	ATConsoleWrite(" #  Dev      Cd St Bufr PutR BfLn X1 X2 X3 X4 X5 X6\n");

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
		if (iocb[0] == 0x7F) {
			// provisional open - ICAX3 contains the device name, ICAX4 the SIO address
			s.append_sprintf("$%02X~%c", iocb[13], iocb[12]);

			if (iocb[1] > 1)
				s.append_sprintf("%u", iocb[1]);

			s += ':';
		} else if (iocb[0] != 0xFF) {
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
		while(pad < 13) {
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

void ATConsoleCmdBasicDumpLine(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdExprAddr addrArg(false, false);

	parser >> addrArg >> 0;

	VDStringA line;
	uint16 addr = addrArg.IsValid() ? addrArg.GetValue() : g_debugger.GetContinuationAddress();

	const uint16 lineNumber = g_sim.DebugReadWord(addr);
	const uint16 vntp = g_sim.DebugReadWord(0x82);
	const uint16 vvtp = g_sim.DebugReadWord(0x86);

	line.sprintf("%u ", lineNumber);

	if (lineNumber > 32768) {
		line += "[invalid line number]";
	} else {
		int lineLen = g_sim.DebugReadByte(addr+2);
		int offset = 3;

		g_debugger.SetContinuationAddress(addr + lineLen);

		while(offset < lineLen) {
			// read statement offset
			const uint8 statementOffset = g_sim.DebugReadByte(addr + offset++);

			if (statementOffset > lineLen || statementOffset < offset)
				break;

			const uint8 statementToken = g_sim.DebugReadByte(addr + offset++);

			if (statementToken > 0x36) {
				line.append_sprintf("[invalid statement token: $%02X]", statementToken);
				break;
			}

			static const char* const kStatements[]={
				/* 0x00 */ "REM",
				/* 0x01 */ "DATA",
				/* 0x02 */ "INPUT",
				/* 0x03 */ "COLOR",
				/* 0x04 */ "LIST",
				/* 0x05 */ "ENTER",
				/* 0x06 */ "LET",
				/* 0x07 */ "IF",
				/* 0x08 */ "FOR",
				/* 0x09 */ "NEXT",
				/* 0x0A */ "GOTO",
				/* 0x0B */ "GO TO",
				/* 0x0C */ "GOSUB",
				/* 0x0D */ "TRAP",
				/* 0x0E */ "BYE",
				/* 0x0F */ "CONT",
				/* 0x10 */ "COM",
				/* 0x11 */ "CLOSE",
				/* 0x12 */ "CLR",
				/* 0x13 */ "DEG",
				/* 0x14 */ "DIM",
				/* 0x15 */ "END",
				/* 0x16 */ "NEW",
				/* 0x17 */ "OPEN",
				/* 0x18 */ "LOAD",
				/* 0x19 */ "SAVE",
				/* 0x1A */ "STATUS",
				/* 0x1B */ "NOTE",
				/* 0x1C */ "POINT",
				/* 0x1D */ "XIO",
				/* 0x1E */ "ON",
				/* 0x1F */ "POKE",
				/* 0x20 */ "PRINT",
				/* 0x21 */ "RAD",
				/* 0x22 */ "READ",
				/* 0x23 */ "RESTORE",
				/* 0x24 */ "RETURN",
				/* 0x25 */ "RUN",
				/* 0x26 */ "STOP",
				/* 0x27 */ "POP",
				/* 0x28 */ "?",
				/* 0x29 */ "GET",
				/* 0x2A */ "PUT",
				/* 0x2B */ "GRAPHICS",
				/* 0x2C */ "PLOT",
				/* 0x2D */ "POSITION",
				/* 0x2E */ "DOS",
				/* 0x2F */ "DRAWTO",
				/* 0x30 */ "SETCOLOR",
				/* 0x31 */ "LOCATE",
				/* 0x32 */ "SOUND",
				/* 0x33 */ "LPRINT",
				/* 0x34 */ "CSAVE",
				/* 0x35 */ "CLOAD",
				/* 0x36 */ "",
				/* 0x37 */ "ERROR -",
			};

			if (statementToken != 0x36) {
				line += kStatements[statementToken];
				line += ' ';
			}

			// check for REM, DATA, and syntax error cases
			if (statementToken == 0x37 || statementToken == 0x01 || statementToken == 0) {
				while(offset < lineLen) {
					uint8 c = g_sim.DebugReadByte(addr + offset++);

					if (c < 0x20 || c >= 0x7F)
						line.append_sprintf("{%02X}", c);
					else
						line += (char)c;
				}

				line.append_sprintf(" {end $%04X}", (addr + offset) & 0xffff);
				break;
			}

			// process operator/function/variable tokens
			uint8 buf[6];

			while(offset < lineLen) {
				const uint8 token = g_sim.DebugReadByte(addr + offset++);

				if (token == 0x14) {
					line += ": ";
					break;
				}

				if (token == 0x16) {
					line.append_sprintf(" {end $%04X}", (addr + offset) & 0xffff);
					goto line_end;
				}

				switch(token) {
					case 0x0E:
						for(int i=0; i<6; ++i)
							buf[i] = g_sim.DebugReadByte(addr + offset++);

						line.append_sprintf("%g", ATReadDecFloatAsBinary(buf));
						break;

					case 0x0F:
						line += '"';
						{
							uint8 len = g_sim.DebugReadByte(addr + offset++);

							while(len--) {
								uint8 c = g_sim.DebugReadByte(addr + offset++);

								if (c < 0x20 || c >= 0x7F)
									line.append_sprintf("{%02X}", c);
								else
									line += (char)c;
							}
						}
						line += '"';
						break;

					case 0x12:	line += ','; break;
					case 0x13:	line += '$'; break;
					case 0x15:	line += ';'; break;
					case 0x17:	line += " GOTO "; break;
					case 0x18:	line += " GOSUB "; break;
					case 0x19:	line += " TO "; break;
					case 0x1A:	line += " STEP "; break;
					case 0x1B:	line += " THEN "; break;
					case 0x1C:	line += '#'; break;
					case 0x1D:	line += "<="; break;
					case 0x1E:	line += "<>"; break;
					case 0x1F:	line += ">="; break;
					case 0x20:	line += '<'; break;
					case 0x21:	line += '>'; break;
					case 0x22:	line += '='; break;
					case 0x24:	line += '*'; break;
					case 0x25:	line += '+'; break;
					case 0x26:	line += '-'; break;
					case 0x27:	line += '/'; break;
					case 0x28:	line += " NOT "; break;
					case 0x29:	line += " OR "; break;
					case 0x2A:	line += " AND "; break;
					case 0x2B:	line += '('; break;
					case 0x2C:	line += ')'; break;
					case 0x2D:	line += '='; break;
					case 0x2E:	line += '='; break;
					case 0x2F:	line += "<="; break;
					case 0x30:	line += "<>"; break;
					case 0x31:	line += ">="; break;
					case 0x32:	line += '<'; break;
					case 0x33:	line += '>'; break;
					case 0x34:	line += '='; break;
					case 0x35:	line += '+'; break;
					case 0x36:	line += '-'; break;
					case 0x37:
					case 0x38:
					case 0x39:
					case 0x3A:
					case 0x3B:
						line += '(';
						break;
					case 0x3C:	line += ','; break;
					case 0x3D:	line += "STR$"; break;
					case 0x3E:	line += "CHR$"; break;
					case 0x3F:	line += "USR"; break;
					case 0x40:	line += "ASC"; break;
					case 0x41:	line += "VAL"; break;
					case 0x42:	line += "LEN"; break;
					case 0x43:	line += "ADR"; break;
					case 0x44:	line += "ATN"; break;
					case 0x45:	line += "COS"; break;
					case 0x46:	line += "PEEK"; break;
					case 0x47:	line += "SIN"; break;
					case 0x48:	line += "RND"; break;
					case 0x49:	line += "FRE"; break;
					case 0x4A:	line += "EXP"; break;
					case 0x4B:	line += "LOG"; break;
					case 0x4C:	line += "CLOG"; break;
					case 0x4D:	line += "SQR"; break;
					case 0x4E:	line += "SGN"; break;
					case 0x4F:	line += "ABS"; break;
					case 0x50:	line += "INT"; break;
					case 0x51:	line += "PADDLE"; break;
					case 0x52:	line += "STICK"; break;
					case 0x53:	line += "PTRIG"; break;
					case 0x54:	line += "STRIG"; break;
					default:
						if (token < 0x80) {
							line.append_sprintf(" [invalid token $%02X]", token);
							goto line_end;
						}

						{
							bool varValid = false;

							if (vntp < vvtp) {
								uint16 vaddr = vntp;
								uint8 vidx = token;

								while(vidx-- > 0x80) {
									while(vaddr < vvtp && !(g_sim.DebugReadByte(vaddr++) & 0x80))
										;
								}

								if (vaddr < vvtp) {
									size_t curLen = line.size();

									varValid = true;

									for(i=0; i<16; ++i) {
										if (vaddr >= vvtp) {
											varValid = false;
											break;
										}

										uint8 b = g_sim.DebugReadByte(vaddr++);
										uint8 c = b & 0x7f;

										if (c < 0x20 || c >= 0x7f) {
											varValid = false;
											break;
										}

										line += (char)c;

										if (b & 0x80)
											break;
									}

									if (!varValid)
										line.resize(curLen);
								}
							}

							if (!varValid)
								line.append_sprintf("[V%02X]", token);
						}
						break;
				}
			}
		}
	}

line_end:
	line += '\n';
	ATConsoleWrite(line.c_str());
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

		uint16 valptr = vvtp + i*8;
		uint8 dat[8];

		for(int j=0; j<8; ++j)
			dat[j] = g_sim.DebugReadByte(valptr+j);

		while(s.length() < 8)
			s += ' ';

		if (dat[0] & 0x80) {
			if (!(dat[0] & 0x01)) {
				s += "undimensioned";
			} else {
				s.append_sprintf("len=%u, capacity=%u, address=$%04x", VDReadUnalignedLEU16(dat+4), VDReadUnalignedLEU16(dat+6), VDReadUnalignedLEU16(dat+2));
			}
		} else if (dat[0] & 0x40) {
			if (!(dat[0] & 0x01)) {
				s += "undimensioned";
			} else {
				s.append_sprintf("size=%ux%u, address=$%04x", VDReadUnalignedLEU16(dat+4), VDReadUnalignedLEU16(dat+6), VDReadUnalignedLEU16(dat+2));
			}
		} else {
			s.append_sprintf(" = %g", ATReadDecFloatAsBinary(dat+2));
		}

		s += '\n';

		ATConsoleWrite(s.c_str());
	}
}

void ATConsoleCmdMap(int argc, const char *const *argv) {
	ATMemoryManager& memman = *g_sim.GetMemoryManager();

	memman.DumpStatus();
}

void ATConsoleCmdEcho(int argc, const char *const *argv) {
	VDStringA line;

	for(int i=0; i<argc; ++i) {
		if (i)
			line += ' ';

		const char *s = argv[i];
		const char *t = s + strlen(s);

		if (*s == '"') {
			++s;

			if (t != s && t[-1] == '"')
				--t;
		}

		line.append(s, t);
	}

	line += '\n';
	ATConsoleWrite(line.c_str());
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
			node = ATDebuggerParseExpression(arg, &g_debugger, ATGetDebugger()->GetExprOpts());
		} catch(const ATDebuggerExprParseException& ex) {
			line.append_sprintf("<error: %s>", ex.c_str());
			break;
		}

		const ATDebugExpEvalContext& ctx = g_debugger.GetEvalContext();
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

			case 'c':	// ASCII character
				{
					// left-pad if necessary
					uint32 padWidth = (width > 1) ? width - 1 : 0;

					if (padWidth && !leftAlign) {
						do {
							line += ' ';
						} while(--padWidth);
					}

					value &= 0xff;
					if (value < 0x20 || value >= 0x7f)
						value = '.';

					line += (char)value;

					if (padWidth) {
						do {
							line += ' ';
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

			case 'e':	// float (exponential)
			case 'f':	// float (natural)
			case 'g':	// float (general)
				{
					double d = ATDebugReadDecFloatAsBinary(g_sim.GetCPUMemory(), value);
					const char format[]={'%', '*', '.', '*', c, 0};

					line.append_sprintf(format, width, precision >= 0 ? precision : 10, d);
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

			case 'y':
				{
					// decode symbol
					ATDebuggerSymbol sym;
					VDStringA temp;
					if (g_debugger.LookupSymbol(value, kATSymbol_Any, sym)) {
						sint32 disp = (sint32)value - sym.mSymbol.mOffset;
						if (abs(disp) > 10)
							temp.sprintf("%s%c%X", sym.mSymbol.mpName, disp < 0 ? '-' : '+', abs(disp));
						else if (disp)
							temp.sprintf("%s%+d", sym.mSymbol.mpName, disp);
						else
							temp = sym.mSymbol.mpName;
					} else
						temp.sprintf("$%04X", value);

					// left-pad if necessary
					uint32 len = (uint32)temp.size();
					uint32 precPadWidth = 0;

					if (precision >= 0 && len > (uint32)precision)
						len = (uint32)precision;

					uint32 padWidth = (len < width) ? width - len : 0;

					if (padWidth && !leftAlign) {
						do {
							line += ' ';
						} while(--padWidth);
					}

					line.append(temp, 0, len);

					if (padWidth) {
						do {
							line += ' ';
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

	ATPIAState piastate;
	g_sim.GetPIA().GetState(piastate);

	const uint8 dataToInject[]={
		cpu.GetA(),
		cpu.GetX(),
		cpu.GetY(),
		regS,
		(uint8)stubOffset,
		piastate.mCRB ^ 0x04,
		piastate.mCRB & 0x04 ? piastate.mDDRB : piastate.mORB,
		piastate.mCRB & 0x04 ? piastate.mORB : piastate.mDDRB,
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

	const ATDebugExpEvalContext& ctx = g_debugger.GetEvalContext();

	vdautoptr<ATDebugExpNode> node;

	try {
		ATDebuggerExprParseOpts opts = ATGetDebugger()->GetExprOpts();

		// We always assume an expression here.
		opts.mbAllowUntaggedHex = false;

		node = ATDebuggerParseExpression(s, &g_debugger, opts);
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
	ATDebuggerCmdSwitch brief("b", false);
	ATDebuggerCmdParser(argc, argv) >> brief >> 0;

	uint8 dcb[12];

	for(int i=0; i<12; ++i)
		dcb[i] = g_sim.DebugReadByte(ATKernelSymbols::DDEVIC + i);

	if (brief) {
		VDStringA s;
		s.sprintf("SIO: Device $%02X:%02X, Command $%02X:$%04X"
			, dcb[0], dcb[1], dcb[2]
			, VDReadUnalignedLEU16(&dcb[10])
		);

		if (dcb[3] & 0xc0) {
			switch(dcb[3] & 0xc0) {
				case 0x40:
					s += ", Read ";
					break;
				case 0x80:
					s += ", Write";
					break;
				case 0xc0:
					s += ", R/W  ";
					break;
			}

			s.append_sprintf(" len $%04X -> $%04X"
				, VDReadUnalignedLEU16(&dcb[8])
				, VDReadUnalignedLEU16(&dcb[4]));
		}

		s += '\n';
		ATConsoleWrite(s.c_str());
	} else {
		ATConsolePrintf("DDEVIC    Device ID   = $%02x\n", dcb[0]);
		ATConsolePrintf("DUNIT     Device unit = $%02x\n", dcb[1]);
		ATConsolePrintf("DCOMND    Command     = $%02x\n", dcb[2]);
		ATConsolePrintf("DSTATS    Status      = $%02x\n", dcb[3]);
		ATConsolePrintf("DBUFHI/LO Buffer      = $%04x\n", dcb[4] + 256 * dcb[5]);
		ATConsolePrintf("DTIMLO    Timeout     = $%02x\n", dcb[6]);
		ATConsolePrintf("DBYTHI/LO Length      = $%04x\n", dcb[8] + 256 * dcb[9]);
		ATConsolePrintf("DAUXHI/LO Sector      = $%04x\n", dcb[10] + 256 * dcb[11]);
	}
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
	ATDebuggerCmdExprAddr addr(false, false);
	ATDebuggerCmdParser(argc, argv) >> addr >> 0;

	uint16 linkAddr = addr.IsValid() ? addr.GetValue() : 0xd4e;

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

void ATConsoleCmdIDE(int argc, const char *const *argv) {
	ATDebuggerCmdParser(argc, argv) >> 0;

	ATIDEEmulator *ide = g_sim.GetIDEEmulator();
	if (!ide) {
		ATConsoleWrite("IDE not active.\n");
		return;
	}

	ide->DumpStatus();
}

void ATConsoleCmdIDEDumpSec(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdExprNum num(true, false, 0);
	ATDebuggerCmdSwitch swL("l", false);
	parser >> swL >> num >> 0;

	ATIDEEmulator *ide = g_sim.GetIDEEmulator();
	if (!ide) {
		ATConsoleWrite("IDE not active.\n");
		return;
	}

	uint8 buf[512];
	ide->DebugReadSector(num.GetValue(), buf, 512);

	VDStringA s;
	int step = swL ? 2 : 1;
	for(int i=0; i<512; i += 32) {
		s.sprintf("%03x:", swL ? i >> 1 : i);
		
		for(int j=0; j<32; j += step)
			s.append_sprintf(" %02x", buf[i+j]);

		s += " |";
		for(int j=0; j<32; j += step) {
			uint8 c = buf[i+j];

			if ((uint32)(c - 0x20) >= 0x5f)
				c = '.';

			s += c;
		}

		s += "|\n";

		ATConsoleWrite(s.c_str());
	}
}

void ATConsoleCmdIDEReadSec(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdExprNum num(true, false, 0);
	ATDebuggerCmdExprAddr addr(true, true, false);
	ATDebuggerCmdSwitch swL("l", false);
	parser >> swL >> num >> addr >> 0;

	ATIDEEmulator *ide = g_sim.GetIDEEmulator();
	if (!ide) {
		ATConsoleWrite("IDE not active.\n");
		return;
	}

	uint8 buf[512];
	ide->DebugReadSector(num.GetValue(), buf, 512);

	uint32 addrhi = addr.GetValue() & kATAddressSpaceMask;
	uint32 addrlo = addr.GetValue();

	int step = swL ? 2 : 1;
	for(int i=0; i<512; i += step) {
		g_sim.DebugGlobalWriteByte(addrhi + (addrlo & kATAddressOffsetMask), buf[i]);
		++addrlo;
	}
}

void ATConsoleCmdIDEWriteSec(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdExprNum num(true, false, 0);
	ATDebuggerCmdExprAddr addr(true, true, false);
	ATDebuggerCmdSwitch swL("l", false);
	parser >> swL >> num >> addr >> 0;

	ATIDEEmulator *ide = g_sim.GetIDEEmulator();
	if (!ide) {
		ATConsoleWrite("IDE not active.\n");
		return;
	}

	uint32 addrhi = addr.GetValue() & kATAddressSpaceMask;
	uint32 addrlo = addr.GetValue();

	uint8 buf[512];
	if (swL) {
		for(int i=0; i<512; i += 2) {
			buf[i] = g_sim.DebugGlobalReadByte(addrhi + (addrlo & kATAddressOffsetMask));
			buf[i+1] = 0xFF;

			++addrlo;
		}
	} else {
		for(int i=0; i<512; ++i) {
			buf[i] = g_sim.DebugGlobalReadByte(addrhi + (addrlo & kATAddressOffsetMask));

			++addrlo;
		}
	}

	ide->DebugWriteSector(num.GetValue(), buf, 512);
}

void ATConsoleCmdRunBatchFile(int argc, const char *const *argv) {
	ATDebuggerCmdPath path(true);
	ATDebuggerCmdParser(argc, argv) >> path >> 0;

	g_debugger.QueueBatchFile(VDTextAToW(path->c_str()).c_str());
}

void ATConsoleCmdOnExeLoad(int argc, const char *const *argv) {
	VDStringA cmd;
	ATDebuggerSerializeArgv(cmd, argc, argv);

	g_debugger.OnExeQueueCmd(false, cmd.c_str());
}

void ATConsoleCmdOnExeRun(int argc, const char *const *argv) {
	VDStringA cmd;
	ATDebuggerSerializeArgv(cmd, argc, argv);

	g_debugger.OnExeQueueCmd(true, cmd.c_str());
}

void ATConsoleCmdOnExeClear(int argc, const char *const *argv) {
	VDStringA cmd;
	ATDebuggerSerializeArgv(cmd, argc, argv);

	g_debugger.OnExeClear();
	ATConsoleWrite("On-EXE commands cleared.\n");
}

void ATConsoleCmdOnExeList(int argc, const char *const *argv) {
	VDStringA s;

	for(int i=0; i<2; ++i) {
		if (i)
			ATConsoleWrite("Executed prior to EXE run:\n");
		else
			ATConsoleWrite("Executed prior to EXE load:\n");

		for(int j=0; g_debugger.OnExeGetCmd(i != 0, j, s); ++j)
			ATConsolePrintf("    %s\n", s.c_str());

		ATConsoleWrite("\n");
	}
}

void ATConsoleCmdSourceMode(int argc, const char *const *argv) {
	ATDebuggerCmdName name(false);
	ATDebuggerCmdParser(argc, argv) >> name >> 0;

	if (name.IsValid()) {
		if (*name == "on") {
			g_debugger.SetSourceMode(kATDebugSrcMode_Source);
		} else if (*name == "off") {
			g_debugger.SetSourceMode(kATDebugSrcMode_Disasm);
		} else
			throw MyError("Unknown source mode: %s\n", name->c_str());
	}

	ATConsolePrintf("Source debugging mode is now %s.\n", g_debugger.IsSourceModeEnabled() ? "on" : "off");
}

void ATConsoleCmdDS1305(int argc, const char *const *argv) {
	ATDebuggerCmdParser(argc, argv) >> 0;

	ATSIDEEmulator *side = g_sim.GetSIDE();
	ATUltimate1MBEmulator *ult = g_sim.GetUltimate1MB();

	if (!side && !ult)
		throw MyError("Neither SIDE nor Ultimate1MB are enabled.");

	if (side) {
		ATConsoleWrite("\nSIDE:\n");
		side->DumpRTCStatus();
	}

	if (ult) {
		ATConsoleWrite("\nUltimate1MB:\n");
		ult->DumpRTCStatus();
	}
}

void ATConsoleCmdTape(ATDebuggerCmdParser& parser) {
	parser >> 0;

	ATCassetteEmulator& tape = g_sim.GetCassette();
	if (!tape.IsLoaded())
		throw MyError("No cassette tape mounted.");

	ATConsolePrintf("Current position:  %u/%u (%.3fs / %.3fs)\n"
		, tape.GetSamplePos()
		, tape.GetSampleLen()
		, tape.GetPosition()
		, tape.GetLength());

	ATConsolePrintf("Motor state:       %s / %s / %s\n"
		, tape.IsPlayEnabled() ? "play" : "stop"
		, tape.IsMotorEnabled() ? "enabled" : "disabled"
		, tape.IsMotorRunning() ? "running" : "stopped");
}

void ATConsoleCmdTapeData(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdSwitch swD("d", false);
	ATDebuggerCmdSwitch swT("t", false);
	ATDebuggerCmdSwitchNumArg swB("b", 1, 6000, 600);
	ATDebuggerCmdSwitchNumArg swR("r", -10000000, +10000000);
	ATDebuggerCmdSwitchNumArg swP("p", 0, +10000000);
	parser >> swD >> swT >> swB >> swP;
	if (!swP.IsValid())
		parser >> swR;
	parser >> 0;

	ATCassetteEmulator& tape = g_sim.GetCassette();
	IATCassetteImage *pImage = tape.GetImage();
	if (!tape.IsLoaded() || !pImage)
		throw MyError("No cassette tape mounted.");

	uint32 pos = tape.GetSamplePos();
	uint32 len = tape.GetSampleLen();

	if (swR.IsValid()) {
		sint32 deltapos = VDRoundToInt32((float)swR.GetValue() * kDataFrequency / 1000.0f);

		if (deltapos < 0) {
			if (pos <= (uint32)-deltapos)
				pos = 0;
			else
				pos += deltapos;
		} else
			pos += deltapos;
	} else if (swP.IsValid()) {
		pos = VDRoundToInt32((float)swP.GetValue() * (kDataFrequency / 1000.0f));
	}

	if (swT || swB.IsValid()) {
		uint32 poslimit = pos + (int)(kDataFrequency * 30);
		if (poslimit > len)
			poslimit = len;

		uint32 replimit = 50;
		bool first = true;
		bool prevBit = true;

		uint32 pos2 = pos;

		if (swB.IsValid()) {
			const int bitsPerSampleFP8 = (int)((float)swB.GetValue() / kDataFrequency * 256 + 0.5f);
			int stepAccum = 0;
			uint32 bitIdx = 0;
			bool bitPhase = false;
			bool dataOnly = swD;
			uint8 data = 0;

			uint32 lastDataPos = pos2;

			const int kMinGapReportTime = (int)(kDataFrequency / 10);

			VDStringA s;
			while(pos2 < poslimit) {
				bool nextBit = pImage->GetBit(pos2, 2, 1, prevBit);

				stepAccum += bitsPerSampleFP8;

				if (nextBit != prevBit) {
					bitPhase = false;
					stepAccum = 0;
				}

				if (stepAccum >= 0x80) {
					stepAccum -= 0x80;

					bitPhase = !bitPhase;

					if (bitPhase) {
						if (!dataOnly || bitIdx == 9) {
							if ((int)(pos2 - lastDataPos) >= kMinGapReportTime) {
								ATConsolePrintf("-- gap of %u samples (%.1fms) --\n"
									, pos2 - lastDataPos
									, (float)(pos2 - lastDataPos) * 1000.0f / kDataFrequency);
							}

							lastDataPos = pos2;

							s.sprintf("%u (%.6fs / +%.6fs): bit[%u] = %c"
								, pos2
								, (float)pos2 / kDataFrequency
								, (float)(pos2 - pos) / kDataFrequency
								, bitIdx
								, nextBit ? '1' : '0');
						}

						bool dataByte = false;

						if (bitIdx == 0) {
							// start bit -- must be space
							if (!nextBit)
								++bitIdx;
						} else if (bitIdx < 9) {
							// data bit -- can be zero or one
							++bitIdx;
							data = (data >> 1) + (nextBit ? 0x80 : 0x00);
						} else if (bitIdx == 9) {
							// stop bit -- must be mark bit
							if (nextBit) {
								s.append_sprintf(" | data = $%02x (ok)", data);
								bitIdx = 0;
							} else {
								s.append_sprintf(" | data = $%02x (framing error)", data);
								++bitIdx;
							}

							dataByte = true;
						} else {
							if (nextBit)
								bitIdx = 0;
						}

						if (!dataOnly || dataByte) {
							s += '\n';
							ATConsoleWrite(s.c_str());

							if (!--replimit)
								break;
						}
					}
				}

				prevBit = nextBit;
				first = false;
				++pos2;
			}
		} else {
			while(pos2 < poslimit) {
				bool nextBit = pImage->GetBit(pos2, 2, 1, prevBit);

				if (first || nextBit != prevBit) {

					ATConsolePrintf("%u (%.6fs) | +%u (+%.6fs): %c\n"
						, pos2
						, (float)pos2 / kDataFrequency
						, pos2 - pos
						, (float)(pos2 - pos) / kDataFrequency
						, nextBit ? '1' : '0');

					first = false;
					prevBit = nextBit;

					if (!--replimit)
						break;
				}

				++pos2;
			}
		}

		if (pos2 >= len)
			ATConsoleWrite("End of tape reached.\n");
	} else {
		uint32 avgPeriod = 1;
		uint32 threshold = 1;

		char buf[62];
		bool bit = true;

		for(uint32 i = 0; i < 61; ++i) {
			uint32 pos2 = pos + i;

			if (pos2 < 30)
				buf[i] = '.';
			else {
				pos2 -= 30;

				if (pos2 >= len) {
					buf[i] = '.';
				} else {
					bit = pImage->GetBit(pos2, 2, 1, bit);

					buf[i] = bit ? '1' : '0';
				}
			}
		}

		buf[61] = 0;

		ATConsolePrintf("%s\n", buf);
		ATConsolePrintf("%30s^ %u/%u\n", "", pos, len);
	}
}

void ATConsoleCmdSID(ATDebuggerCmdParser& parser) {
	parser >> 0;

	ATSlightSIDEmulator *sid = g_sim.GetSlightSID();

	if (!sid) {
		ATConsoleWrite("SlightSID is not active.\n");
		return;
	}

	sid->DumpStatus();
}

void ATConsoleCmdCovox(ATDebuggerCmdParser& parser) {
	parser >> 0;

	ATCovoxEmulator *covox = g_sim.GetCovox();

	if (!covox) {
		ATConsoleWrite("Covox is not active.\n");
		return;
	}

	covox->DumpStatus();
}

void ATConsoleCmdUltimate(ATDebuggerCmdParser& parser) {
	parser >> 0;

	ATUltimate1MBEmulator *ult = g_sim.GetUltimate1MB();

	if (!ult) {
		ATConsoleWrite("Ultimate1MB is not active.\n");
		return;
	}

	ult->DumpStatus();
}

void ATConsoleCmdPBI(ATDebuggerCmdParser& parser) {
	ATPBIManager& pbi = g_sim.GetPBIManager();

	ATConsolePrintf("PBI select register:   $%02x\n", pbi.GetSelectRegister());
	ATConsolePrintf("PBI math pack overlay: %s\n", pbi.IsROMOverlayActive() ? "enabled" : "disabled");
}

void ATConsoleCmdBase(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdName name(false);

	parser >> name >> 0;

	ATDebuggerExprParseOpts opts(g_debugger.GetExprOpts());

	if (name.IsValid()) {
		if (*name == "dec") {
			opts.mbAllowUntaggedHex = false;
			opts.mbDefaultHex = false;

		} else if (*name == "hex") {
			opts.mbAllowUntaggedHex = true;
			opts.mbDefaultHex = true;

		} else if (*name == "mixed") {
			opts.mbAllowUntaggedHex = true;
			opts.mbDefaultHex = false;

		} else {
			throw MyError("Unrecognized number base mode: %s.", name->c_str());
		}
	}

	g_debugger.SetExprOpts(opts);

	if (opts.mbDefaultHex)
		ATConsoleWrite("Numeric base is set to hex.\n");
	else if (opts.mbAllowUntaggedHex)
		ATConsoleWrite("Numeric base is set to mixed.\n");
	else
		ATConsoleWrite("Numeric base is set to decimal.\n");
}

void ATConsoleCmdReload(ATDebuggerCmdParser& parser) {
	parser >> 0;

	g_debugger.ReloadModules();
}

void ATConsoleCmdCIODevs(ATDebuggerCmdParser& parser) {
	parser >> 0;

	ATConsolePrintf("Device  Handler table address\n");
	for(int i=0; i<15*3; i += 3) {
		uint8 c = g_sim.DebugReadByte(ATKernelSymbols::HATABS + i);

		if (c < 0x20 || c >= 0x7F)
			break;

		uint16 addr = g_sim.DebugReadWord(ATKernelSymbols::HATABS + 1 + i);

		ATConsolePrintf("  %c:    %s\n", c, g_debugger.GetAddressText(addr, true, true).c_str());
	}
}

void ATConsoleCmdSum(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdExprAddr addrarg(true, true);
	ATDebuggerCmdLength lenarg(0, true, &addrarg);
	
	parser >> addrarg >> lenarg >> 0;

	ATCPUEmulatorMemory& mem = g_sim.GetCPUMemory();
	uint32 len = lenarg;
	uint32 addrspace = addrarg.GetValue() & kATAddressSpaceMask;
	uint32 addroffset = addrarg.GetValue() & kATAddressOffsetMask;

	uint32 sum = 0;
	uint32 wrapsum = 0;

	for(uint32 len = lenarg; len; --len) {
		uint8 c = g_sim.DebugGlobalReadByte(addrspace + addroffset);
		
		sum += c;
		wrapsum += c;
		wrapsum += (wrapsum >> 8);
		wrapsum &= 0xff;

		addroffset = (addroffset + 1) & kATAddressOffsetMask;
	}

	ATConsolePrintf("Sum[%s + L%x] = $%02x (checksum = $%02x)\n", g_debugger.GetAddressText(addrarg.GetValue(), true).c_str(), (uint32)lenarg, sum, wrapsum);
}

void ATConsoleCmdAliasA8(int argc, const char *const *argv) {
	ATDebuggerCmdParser(argc, argv) >> 0;

	static const char *kA8Aliases[][3]={
		{ "cont", "", "g" },
		{ "show", "", "r" },
		{ "stack", "", "k" },
		{ "setpc", "%1", "r pc %1" },
		{ "seta", "%1", "r a %1" },
		{ "sets", "%1", "r s %1" },
		{ "setx", "%1", "r x %1" },
		{ "sety", "%1", "r y %1" },
		{ "setn", "%1", "r p.n %1" },
		{ "setv", "%1", "r p.v %1" },
		{ "setd", "%1", "r p.d %1" },
		{ "seti", "%1", "r p.i %1" },
		{ "setz", "%1", "r p.z %1" },
		{ "setc", "%1", "r p.c %1" },
		{ "setn", "", "r p.n 1" },
		{ "setv", "", "r p.v 1" },
		{ "setd", "", "r p.d 1" },
		{ "seti", "", "r p.i 1" },
		{ "setz", "", "r p.z 1" },
		{ "setc", "", "r p.c 1" },
		{ "clrn", "", "r p.n 0" },
		{ "clrv", "", "r p.v 0" },
		{ "clrd", "", "r p.d 0" },
		{ "clri", "", "r p.i 0" },
		{ "clrz", "", "r p.z 0" },
		{ "clrc", "", "r p.c 0" },
		{ "c", "%1 %*", "e %1 %*" },
		{ "d", "", "u" },
		{ "d", "%1", "u %1" },
		{ "f", "%1 %2 %3 %*", "f %1 L>%2 %3 %*" },
		{ "m", "", "db" },
		{ "m", "%1", "db %1" },
		{ "m", "%1 %2", "db %1 L>%2" },
		{ "s", "%1 %2 %3 %*", "s %1 L>%2 %3 %*" },
		{ "sum", "%1 %2", ".sum %1 L>%2" },
		{ "bpc", "%1", "bp %1" },
		{ "history", "", "h" },
		{ "g", "", "t" },
		{ "r", "", "gr" },
		{ "b", "", "bl" },
		{ "b", "?", ".help bp" },
		{ "b", "c", "bc *" },
		{ "b", "d %1", "bc %1" },
		{ "b", "pc=%1", "bp %1" },
		{ "bpc", "%1", "bp %1" },
		{ "antic", "", ".antic" },
		{ "gtia", "", ".gtia" },
		{ "pia", "", ".pia" },
		{ "pokey", "", ".pokey" },
		{ "dlist", "", ".dumpdlist" },
		{ "dlist", "%1", ".dumpdlist %1" },
		{ "labels", "%1", ".loadsym %1" },
		{ "coldstart", "", ".restart" },
		{ "warmstart", "", ".warmreset" },
		{ "help", "", ".help" },
		{ "?", "", ".help" },
		{ "?", "%*", "? %*" },
	};

	for(size_t i=0; i<sizeof(kA8Aliases)/sizeof(kA8Aliases[0]); ++i) {
		g_debugger.SetCommandAlias(kA8Aliases[i][0], kA8Aliases[i][1], kA8Aliases[i][2]);
	}

	ATConsoleWrite("Atari800-compatible command aliases set.\n");
}

void ATConsoleCmdAliasClearAll(int argc, const char *const *argv) {
	ATDebuggerCmdParser(argc, argv) >> 0;

	g_debugger.ClearCommandAliases();

	ATConsoleWrite("Command aliases cleared.\n");
}

void ATConsoleCmdAliasList(int argc, const char *const *argv) {
	g_debugger.ListCommandAliases();
}

namespace {
	bool IsValidAliasName(const VDStringSpanA& name) {
		if (name.empty())
			return false;

		VDStringSpanA::const_iterator it(name.begin()), itEnd(name.end());

		char c = *it;

		if (c == '.') {
			++it;

			if (it == itEnd)
				return false;

			c = *it;
		}

		if (!isalpha((unsigned char)c))
			return false;

		while(it != itEnd) {
			c = *it;

			if (!isalnum((unsigned char)c) && c != '_')
				return false;

			++it;
		}

		return true;
	}
}

void ATConsoleCmdAliasSet(int argc, const char *const *argv) {
	ATDebuggerCmdName alias(true);
	ATDebuggerCmdName command(false);

	ATDebuggerCmdParser(argc, argv) >> alias >> command >> 0;

	if (!IsValidAliasName(*alias))
		throw MyError("Invalid alias name: %s\n", alias->c_str());

	const bool existing = g_debugger.IsCommandAliasPresent(alias->c_str());

	VDStringA aliascmd(command->c_str());
	aliascmd += " %*";

	if (command.IsValid()) {
		g_debugger.SetCommandAlias(alias->c_str(), NULL, aliascmd.c_str());

		ATConsolePrintf(existing ? "Redefined alias: %s.\n" : "Defined alias: %s.\n", alias->c_str());
	} else if (existing) {
		g_debugger.SetCommandAlias(alias->c_str(), NULL, NULL);

		ATConsolePrintf("Deleted alias: %s.\n", alias->c_str());
	} else {
		ATConsolePrintf("Unknown alias: %s.\n", alias->c_str());
	}
}

void ATConsoleCmdAliasPattern(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdString aliaspat(true);
	ATDebuggerCmdString aliastmpl(false);

	parser >> aliaspat >> aliastmpl >> 0;

	VDStringRefA patname;
	VDStringRefA patargs(*aliaspat);

	if (!patargs.split(' ', patname)) {
		patname = patargs;
		patargs.clear();
	}

	VDStringA alias(patname);
	if (!IsValidAliasName(patname)) {
		throw MyError("Invalid alias name: %s\n", alias.c_str());
	}

	VDStringA aliasargs(patargs);

	const bool existing = g_debugger.GetCommandAlias(alias.c_str(), aliasargs.c_str()) != NULL;
	if (aliastmpl.IsValid()) {
		g_debugger.SetCommandAlias(alias.c_str(), aliasargs.c_str(), aliastmpl->c_str());

		ATConsolePrintf(existing ? "Redefined alias: %s %s.\n" : "Defined alias: %s %s.\n", alias.c_str(), aliasargs.c_str());
	} else if (existing) {
		g_debugger.SetCommandAlias(alias.c_str(), aliasargs.c_str(), NULL);

		ATConsolePrintf("Deleted alias: %s %s.\n", alias.c_str(), aliasargs.c_str());
	} else {
		ATConsolePrintf("Unknown alias: %s %s.\n", alias.c_str(), aliasargs.c_str());
	}
}

void ATConsoleQueueCommand(const char *s) {
	g_debugger.QueueCommand(s, true);
}

void ATConsoleExecuteCommand(const char *s, bool echo) {
	IATDebuggerActiveCommand *acmd = g_debugger.GetActiveCommand();

	if (acmd) {
		g_debugger.ExecuteCommand(s);
		return;
	}

	if (echo) {
		ATConsolePrintf("%s> ", g_debugger.GetPrompt());
		ATConsolePrintf("%s\n", s);
	} else if (!*s)
		return;

	vdfastvector<char> tempstr;
	vdfastvector<const char *> argptrs;

	int argc = ATDebuggerParseArgv(s, tempstr, argptrs);

	VDStringA tempcmd;

	if (!argc) {
		if (!echo)
			return;

		tempcmd = g_debugger.GetRepeatCommand();
		s = tempcmd.c_str();
		argc = ATDebuggerParseArgv(s, tempstr, argptrs);

		if (!argc)
			return;
	} else {
		if (echo)
			g_debugger.SetRepeatCommand(argptrs[0]);
	}

	const char **argv = argptrs.data();
	const char *cmd = argv[0];

	if (*cmd == '`')
		++cmd;
	else {
		vdfastvector<char> tempstr2;
		vdfastvector<const char *> argptrs2;

		if (g_debugger.MatchCommandAlias(cmd, argv+1, argc-1, tempstr2, argptrs2)) {
			if (argptrs2.empty()) {
				ATConsolePrintf("Incorrect parameters for alias '%s'.\n", cmd);
				return;
			}

			tempstr.swap(tempstr2);
			argptrs.swap(argptrs2);

			argc = argptrs.size() - 1;
			argv = argptrs.data();
			cmd = argv[0];
		}
	}

	const char *argstart = argc > 1 ? s + (argv[1] - tempstr.data()) : NULL;

	ATDebuggerCmdParser parser(argc-1, argv+1);

	if (!strcmp(cmd, "a")) {
		ATConsoleCmdAssemble(argc-1, argv+1);
		return;
	}
	
	if (!strcmp(cmd, "a8")) {
		ATConsoleCmdAliasA8(argc-1, argv+1);
		return;
	}
	
	if (!strcmp(cmd, "ac")) {
		ATConsoleCmdAliasClearAll(argc-1, argv+1);
		return;
	}
	
	if (!strcmp(cmd, "al")) {
		ATConsoleCmdAliasList(argc-1, argv+1);
		return;
	}
	
	if (!strcmp(cmd, "as")) {
		ATConsoleCmdAliasSet(argc-1, argv+1);
		return;
	}
	
	if (!strcmp(cmd, "ap")) {
		ATConsoleCmdAliasPattern(parser);
		return;
	}
	
	if (!strcmp(cmd, "t")) {
		ATConsoleCmdTrace();
		return;
	}
	
	if (!strcmp(cmd, "g")) {
		ATConsoleCmdGo(argc-1, argv+1);
		return;
	}
	
	if (!strcmp(cmd, "gt")) {
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
	} else if (!strcmp(cmd, "o")) {
		ATConsoleCmdStepOver();
	} else if (!strcmp(cmd, "s")) {
		ATConsoleCmdSearch(argc-1, argv+1);
	} else if (!strcmp(cmd, "st")) {
		ATConsoleCmdStaticTrace(parser);
	} else if (!strcmp(cmd, "bp")) {
		ATConsoleCmdBreakpt(argc-1, argv+1);
	} else if (!strcmp(cmd, "bt")) {
		ATConsoleCmdBreakptTrace(parser);
	} else if (!strcmp(cmd, "bc")) {
		ATConsoleCmdBreakptClear(argc-1, argv+1);
	} else if (!strcmp(cmd, "ba")) {
		ATConsoleCmdBreakptAccess(argc-1, argv+1);
	} else if (!strcmp(cmd, "bl")) {
		ATConsoleCmdBreakptList();
	} else if (!strcmp(cmd, "bs")) {
		ATConsoleCmdBreakptSector(parser);
	} else if (!strcmp(cmd, "bx")) {
		ATConsoleCmdBreakptExpr(argc-1, argv+1);
	} else if (!strcmp(cmd, "u")) {
		ATConsoleCmdUnassemble(parser);
	} else if (!strcmp(cmd, "r")) {
		ATConsoleCmdRegisters(argc-1, argv+1);
	} else if (!strcmp(cmd, "da")) {
		ATConsoleCmdDumpATASCII(argc-1, argv+1);
	} else if (!strcmp(cmd, "db")) {
		ATConsoleCmdDumpBytes(argc-1, argv+1);
	} else if (!strcmp(cmd, "dw")) {
		ATConsoleCmdDumpWords(argc-1, argv+1);
	} else if (!strcmp(cmd, "dd")) {
		ATConsoleCmdDumpDwords(argc-1, argv+1);
	} else if (!strcmp(cmd, "df")) {
		ATConsoleCmdDumpFloats(argc-1, argv+1);
	} else if (!strcmp(cmd, "di")) {
		ATConsoleCmdDumpINTERNAL(argc-1, argv+1);
	} else if (!strcmp(cmd, "lm")) {
		ATConsoleCmdListModules();
	} else if (!strcmp(cmd, "ln")) {
		ATConsoleCmdListNearestSymbol(argc-1, argv+1);
	} else if (!strcmp(cmd, "lfe")) {
		ATConsoleCmdLogFilterEnable(argc-1, argv+1);
	} else if (!strcmp(cmd, "lfd")) {
		ATConsoleCmdLogFilterDisable(argc-1, argv+1);
	} else if (!strcmp(cmd, "lfl")) {
		ATConsoleCmdLogFilterList(argc-1, argv+1);
	} else if (!strcmp(cmd, "lft")) {
		ATConsoleCmdLogFilterTag(argc-1, argv+1);
	} else if (!strcmp(cmd, "vta")) {
		ATConsoleCmdVerifierTargetAdd(argc-1, argv+1);
	} else if (!strcmp(cmd, "vtc")) {
		ATConsoleCmdVerifierTargetClear(argc-1, argv+1);
	} else if (!strcmp(cmd, "vtl")) {
		ATConsoleCmdVerifierTargetList(argc-1, argv+1);
	} else if (!strcmp(cmd, "vtr")) {
		ATConsoleCmdVerifierTargetReset(argc-1, argv+1);
	} else if (!strcmp(cmd, "wb")) {
		ATConsoleCmdWatchByte(parser);
	} else if (!strcmp(cmd, "ww")) {
		ATConsoleCmdWatchWord(parser);
	} else if (!strcmp(cmd, "wc")) {
		ATConsoleCmdWatchClear(argc-1, argv+1);
	} else if (!strcmp(cmd, "wl")) {
		ATConsoleCmdWatchList(argc-1, argv+1);
	} else if (!strcmp(cmd, "wx")) {
		ATConsoleCmdWatchExpr(argc-1, argv+1);
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
	} else if (!strcmp(cmd, "f")) {
		ATConsoleCmdFill(argc-1, argv+1);
	} else if (!strcmp(cmd, "fbx")) {
		ATConsoleCmdFillExp(parser);
	} else if (!strcmp(cmd, "m")) {
		ATConsoleCmdMove(argc-1, argv+1);
	} else if (!strcmp(cmd, "hma")) {
		ATConsoleCmdHeatMapDumpAccesses(parser);
	} else if (!strcmp(cmd, "hmc")) {
		ATConsoleCmdHeatMapClear(parser);
	} else if (!strcmp(cmd, "hmd")) {
		ATConsoleCmdHeatMapDumpMemory(parser);
	} else if (!strcmp(cmd, "hme")) {
		ATConsoleCmdHeatMapEnable(parser);
	} else if (!strcmp(cmd, "hmr")) {
		ATConsoleCmdHeatMapRegisters(parser);
	} else if (!strcmp(cmd, ".antic")) {
		ATConsoleCmdAntic();
	} else if (!strcmp(cmd, ".bank")) {
		ATConsoleCmdBank();
	} else if (!strcmp(cmd, ".dumpdlist")) {
		ATConsoleCmdDumpDisplayList(parser);
	} else if (!strcmp(cmd, ".dlhistory")) {
		ATConsoleCmdDumpDLHistory();
	} else if (!strcmp(cmd, ".gtia")) {
		ATConsoleCmdGTIA();
	} else if (!strcmp(cmd, ".pokey")) {
		ATConsoleCmdPokey();
	} else if (!strcmp(cmd, ".restart")) {
		g_sim.ColdReset();
	} else if (!strcmp(cmd, ".warmreset")) {
		g_sim.WarmReset();
	} else if (!strcmp(cmd, ".beam")) {
		ATAnticEmulator& antic = g_sim.GetAntic();
		ATConsolePrintf("Antic position: %d,%d\n", antic.GetBeamX(), antic.GetBeamY()); 
	} else if (!strcmp(cmd, ".dumpdsm")) {
		ATConsoleCmdDumpDsm(parser);
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
	} else if (!strcmp(cmd, ".readmem")) {
		ATConsoleCmdReadMem(parser);
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
	} else if (!strcmp(cmd, ".diskdumpsec")) {
		ATConsoleCmdDiskDumpSec(parser);
	} else if (!strcmp(cmd, ".dma")) {
		g_sim.GetAntic().DumpDMAPattern();
	} else if (!strcmp(cmd, ".dmamap")) {
		g_sim.GetAntic().DumpDMAActivityMap();
	} else if (!strcmp(cmd, ".dmabuf")) {
		g_sim.GetAntic().DumpDMALineBuffer();
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
	} else if (!strcmp(cmd, ".basic_dumpline")) {
		ATConsoleCmdBasicDumpLine(parser);
	} else if (!strcmp(cmd, ".basic_vars")) {
		ATConsoleCmdBasicVars(argc-1, argv+1);
	} else if (!strcmp(cmd, ".map")) {
		ATConsoleCmdMap(argc-1, argv+1);
	} else if (!strcmp(cmd, ".echo")) {
		ATConsoleCmdEcho(argc-1, argv+1);
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
	} else if (!strcmp(cmd, ".ide")) {
		ATConsoleCmdIDE(argc-1, argv+1);
	} else if (!strcmp(cmd, ".ide_dumpsec")) {
		ATConsoleCmdIDEDumpSec(parser);
	} else if (!strcmp(cmd, ".ide_rdsec")) {
		ATConsoleCmdIDEReadSec(parser);
	} else if (!strcmp(cmd, ".ide_wrsec")) {
		ATConsoleCmdIDEWriteSec(parser);
	} else if (!strcmp(cmd, ".batch")) {
		ATConsoleCmdRunBatchFile(argc-1, argv+1);
	} else if (!strcmp(cmd, ".onexeload")) {
		ATConsoleCmdOnExeLoad(argc-1, argv+1);
	} else if (!strcmp(cmd, ".onexerun")) {
		ATConsoleCmdOnExeRun(argc-1, argv+1);
	} else if (!strcmp(cmd, ".onexelist")) {
		ATConsoleCmdOnExeList(argc-1, argv+1);
	} else if (!strcmp(cmd, ".onexeclear")) {
		ATConsoleCmdOnExeClear(argc-1, argv+1);
	} else if (!strcmp(cmd, ".sourcemode")) {
		ATConsoleCmdSourceMode(argc-1, argv+1);
	} else if (!strcmp(cmd, ".ds1305")) {
		ATConsoleCmdDS1305(argc-1, argv+1);
	} else if (!strcmp(cmd, ".tape")) {
		ATConsoleCmdTape(parser);
	} else if (!strcmp(cmd, ".tapedata")) {
		ATConsoleCmdTapeData(parser);
	} else if (!strcmp(cmd, ".sid")) {
		ATConsoleCmdSID(parser);
	} else if (!strcmp(cmd, ".ciodevs")) {
		ATConsoleCmdCIODevs(parser);
	} else if (!strcmp(cmd, ".sum")) {
		ATConsoleCmdSum(parser);
	} else if (!strcmp(cmd, ".covox")) {
		ATConsoleCmdCovox(parser);
	} else if (!strcmp(cmd, ".ultimate")) {
		ATConsoleCmdUltimate(parser);
	} else if (!strcmp(cmd, ".pbi")) {
		ATConsoleCmdPBI(parser);
	} else if (!strcmp(cmd, ".base")) {
		ATConsoleCmdBase(parser);
	} else if (!strcmp(cmd, ".reload")) {
		ATConsoleCmdReload(parser);
	} else if (!strcmp(cmd, "?")) {
		ATConsoleCmdEval(argstart);
	} else {
		ATConsoleWrite("Unrecognized command. \".help\" for help\n");
	}
}
