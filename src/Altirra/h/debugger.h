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

#ifndef f_AT_DEBUGGER_H
#define f_AT_DEBUGGER_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <vd2/system/event.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/enumparse.h>
#include <at/atcpu/execstate.h>
#include <at/atdebugger/symbols.h>
#include <at/atdebugger/target.h>

struct ATSymbol;
struct ATSourceLineInfo;
struct ATDebuggerExprParseOpts;
struct ATDebugExpEvalContext;
class ATDebugExpNode;
class ATBreakpointManager;
class IATDebugger;
class IATDebuggerClient;
class IATDebugTarget;
class ATDebuggerCmdParser;

enum ATDebugEvent {
	kATDebugEvent_BreakpointsChanged,
	kATDebugEvent_SymbolsChanged,
	kATDebugEvent_MemoryChanged,
	kATDebugEvent_CurrentTargetChanged,
	kATDebugEvent_TargetsChanged
};

enum ATDebugSrcMode {
	kATDebugSrcMode_Same,
	kATDebugSrcMode_Disasm,
	kATDebugSrcMode_Source
};

enum ATDebuggerStorageId {
	kATDebuggerStorageId_None,
	kATDebuggerStorageId_CustomSymbols	= 0x0100,
	kATDebuggerStorageId_All
};

enum class ATDebuggerSymbolLoadMode : uint8 {
	Default,
	Disabled,
	Deferred,
	Enabled
};

AT_DECLARE_ENUM_TABLE(ATDebuggerSymbolLoadMode);

enum class ATDebuggerScriptAutoLoadMode : uint8 {
	Default,
	Disabled,
	AskToLoad,
	Enabled
};

AT_DECLARE_ENUM_TABLE(ATDebuggerScriptAutoLoadMode);

struct ATDebuggerSystemState {
	uint16	mPC;
	uint16	mInsnPC;
	uint8	mPCBank;

	ATDebugDisasmMode mExecMode;
	ATCPUExecState mExecState;

	uint32	mPCModuleId;
	uint16	mPCFileId;
	uint16	mPCLine;

	uint32	mFrameExtPC;
	bool	mbRunning;

	uint32	mCycle;

	IATDebugTarget *mpDebugTarget;
};

struct ATCallStackFrame {
	uint32	mPC;
	uint16	mSP;
	uint8	mP;
};

enum class ATDebuggerWatchMode : uint8 {
	None,
	ByteAtAddress,
	WordAtAddress,
	ExprDec,
	ExprHex8,
	ExprHex16,
	ExprHex32
};

struct ATDebuggerWatchInfo {
	uint32	mAddress;
	ATDebuggerWatchMode	mMode;
	uint32	mTargetIndex;
	ATDebugExpNode *mpExpr;
};

struct ATDebuggerBreakpointInfo {
	uint32	mTargetIndex = 0;
	sint32	mNumber = -1;
	sint32	mAddress = 0;
	uint32	mLength = 0;
	bool	mbBreakOnPC = false;
	bool	mbBreakOnRead = false;
	bool	mbBreakOnWrite = false;
	bool	mbBreakOnInsn = false;
	bool	mbDeferred = false;
	bool	mbClearOnReset = false;
	bool	mbOneShot = false;
	bool	mbContinueExecution = false;
	ATDebugExpNode *mpCondition = nullptr;
	const char *mpCommand = nullptr;
};

class IATDebuggerActiveCommand : public IVDRefCount {
public:
	virtual bool IsBusy() const = 0;
	virtual const char *GetPrompt() = 0;
	virtual void BeginCommand(IATDebugger *debugger) = 0;
	virtual void EndCommand() = 0;
	virtual bool ProcessSubCommand(const char *s) = 0;
};

struct ATDebuggerOpenEvent {
	bool mbAllowOpen;
};

struct ATDebuggerRequestFileEvent {
	VDStringW mPath;
	bool mbSave;
};

struct ATDebuggerStepRange {
	uint32 mAddr;
	uint32 mSize;
};

struct ATDebuggerCmdDef {
	const char *mpName;
	void (*mpFunction)(ATDebuggerCmdParser&);
};

class IATDebugger {
public:
	virtual bool IsRunning() const = 0;
	virtual bool AreCommandsQueued() const = 0;

	virtual const ATDebuggerExprParseOpts& GetExprOpts() const = 0;

	virtual const char *GetTempString() const = 0;
	virtual void SetTempString(const char *s) = 0;

	virtual bool IsEnabled() const = 0;
	virtual void SetEnabled(bool enabled) = 0;

	virtual ATDebuggerScriptAutoLoadMode GetScriptAutoLoadMode() const = 0;
	virtual void SetScriptAutoLoadMode(ATDebuggerScriptAutoLoadMode mode) = 0;
	virtual void SetScriptAutoLoadConfirmFn(vdfunction<bool()> fn) = 0;

	virtual bool GetDebugLinkEnabled() const = 0;
	virtual void SetDebugLinkEnabled(bool enabled) = 0;

	virtual void ShowBannerOnce() = 0;

	virtual void SetSourceMode(ATDebugSrcMode src) = 0;
	virtual bool Tick() = 0;
	virtual void Break() = 0;
	virtual void Stop() = 0;
	virtual void Run(ATDebugSrcMode sourceMode) = 0;
	virtual void RunTraced() = 0;

	// Breakpoints
	//
	// All user-accessible breakpoints are identified by a zero-based user breakpoint index. This is
	// _not_ necessarily the same as the user-visible breakpoint ID, which is of the form [group-tag.]number.
	// Each user breakpoint is associated with an underlying system breakpoint, which has its own separate
	// index value. However, not all system breakpoints have user indices as they may be internal to the
	// debugging engine.
	//
	// All breakpoint indices are numbers are stable -- deleting a breakpoint will not renumber any other
	// existing breakpoints.
	//
	virtual bool IsDeferredBreakpointSet(const char *fn, uint32 line) = 0;
	virtual bool ClearUserBreakpoint(uint32 useridx, bool notify) = 0;
	virtual void ClearAllBreakpoints() = 0;
	virtual bool IsBreakpointAtPC(uint32 addr) const = 0;
	virtual void ToggleBreakpoint(uint32 addr) = 0;
	virtual void ToggleAccessBreakpoint(uint32 addr, bool write) = 0;
	virtual void ToggleSourceBreakpoint(const char *fn, uint32 line) = 0;
	virtual uint32 SetBreakpoint(sint32 useridx, const ATDebuggerBreakpointInfo& bpInfo) = 0;
	virtual uint32 SetSourceBreakpoint(const char *fn, uint32 line, ATDebugExpNode *condexp, const char *command, bool continueExecution = false) = 0;
	virtual vdvector<VDStringA> GetBreakpointGroups() const = 0;
	virtual bool GetBreakpointInfo(uint32 useridx, ATDebuggerBreakpointInfo& info) const = 0;
	virtual void GetBreakpointList(vdfastvector<uint32>& bps, const char *group = nullptr) const = 0;
	virtual ATDebugExpNode *GetBreakpointCondition(uint32 useridx) const = 0;

	virtual int AddWatch(uint32 address, int length) = 0;
	virtual int AddWatchExpr(ATDebugExpNode *expr, ATDebuggerWatchMode mode) = 0;
	virtual bool ClearWatch(int idx) = 0;
	virtual void ClearAllWatches() = 0;
	virtual int FindWatch(uint32 address) const = 0;
	virtual bool GetWatchInfo(int idx, ATDebuggerWatchInfo& info) = 0;

	virtual void StepInto(ATDebugSrcMode sourceMode, const ATDebuggerStepRange *stepRanges = NULL, uint32 stepRangeCount = 0) = 0;
	virtual void StepOver(ATDebugSrcMode sourceMode, const ATDebuggerStepRange *stepRanges = NULL, uint32 stepRangeCount = 0) = 0;
	virtual void StepOut(ATDebugSrcMode sourceMode) = 0;
	virtual uint16 GetPC() const = 0;
	virtual void SetPC(uint16 pc) = 0;
	virtual uint32 GetExtPC() const = 0;
	virtual uint16 GetFramePC() const = 0;
	virtual void SetFramePC(uint16 pc) = 0;
	virtual uint32 GetFrameExtPC() const = 0;
	virtual void SetFrameExtPC(uint32 pc) = 0;
	virtual uint32 GetCallStack(ATCallStackFrame *dst, uint32 maxCount) = 0;
	virtual void DumpCallStack() = 0;
	virtual void ListModules() = 0;

	virtual bool IsBreakOnEXERunAddrEnabled() const = 0;
	virtual void SetBreakOnEXERunAddrEnabled(bool en) = 0;

	virtual void AddClient(IATDebuggerClient *client, bool requestUpdate = false) = 0;
	virtual void RemoveClient(IATDebuggerClient *client) = 0;
	virtual void RequestClientUpdate(IATDebuggerClient *client) = 0;
	
	virtual void SetAutoLoadSystemSymbols(bool enable) = 0;
	virtual bool IsAutoLoadSystemSymbolsEnabled() const = 0;

	virtual ATDebuggerSymbolLoadMode GetSymbolLoadMode(bool whenEnabled) const = 0;
	virtual void SetSymbolLoadMode(bool whenEnabled, ATDebuggerSymbolLoadMode mode) = 0;

	virtual bool IsSymbolLoadingEnabled() const = 0;
	virtual uint32 LoadSymbols(const wchar_t *fileName, bool processDirectives = true, const uint32 *targetIdOverride = nullptr, bool loadImmediately = false) = 0;
	virtual void UnloadSymbols(uint32 moduleId) = 0;
	virtual void LoadDeferredSymbols(uint32 moduleId) = 0;
	virtual void LoadAllDeferredSymbols() = 0;
	virtual void ProcessSymbolDirectives(uint32 id) = 0;

	virtual sint32 ResolveSymbol(const char *s, bool allowGlobal = false, bool allowShortBase = true, bool allowNakedHex = true) = 0;
	virtual uint32 ResolveSymbolThrow(const char *s, bool allowGlobal = false, bool allowShortBase = true) = 0;
	virtual void EnumSourceFiles(const vdfunction<void(const wchar_t *, uint32)>& fn) const = 0;

	virtual void AddCustomSymbol(uint32 address, uint32 len, const char *name, uint32 rwxmode, uint32 moduleId = 0) = 0;
	virtual void RemoveCustomSymbol(uint32 address) = 0;
	virtual void LoadCustomSymbols(const wchar_t *filename) = 0;
	virtual void SaveCustomSymbols(const wchar_t *filename) = 0;

	virtual VDStringA GetAddressText(uint32 globalAddr, bool useHexSpecifier, bool addSymbolInfo = false) = 0;

	virtual void GetDirtyStorage(vdfastvector<ATDebuggerStorageId>& ids) const = 0;

	virtual sint32 EvaluateThrow(const char *s) = 0;
	virtual std::pair<bool, sint32> Evaluate(ATDebugExpNode *expr) = 0;
	virtual ATDebugExpEvalContext GetEvalContext() const = 0;

	virtual void StartActiveCommand(IATDebuggerActiveCommand *cmd) = 0;

	virtual void DefineCommands(const ATDebuggerCmdDef *defs, size_t numDefs) = 0;

	virtual bool IsCommandAliasPresent(const char *alias) const = 0;
	virtual const char *GetCommandAlias(const char *alias, const char *args) const = 0;
	virtual void SetCommandAlias(const char *alias, const char *args, const char *command) = 0;
	virtual void ListCommandAliases() = 0;
	virtual void ClearCommandAliases() = 0;

	virtual void QueueBatchFile(const wchar_t *s) = 0;
	virtual void QueueAutoLoadBatchFile(const wchar_t *s) = 0;
	virtual void QueueCommand(const char *cmd, bool echo) = 0;
	virtual void QueueCommandFront(const char *s, bool echo) = 0;

	virtual void WriteMemoryCPU(uint16 address, const void *data, uint32 len) = 0;
	virtual void WriteGlobalMemory(uint32 address, const void *data, uint32 len) = 0;

	virtual const char *GetPrompt() const = 0;
	virtual VDEvent<IATDebugger, const char *>& OnPromptChanged() = 0;

	virtual bool SetTarget(uint32 index) = 0;
	virtual uint32 GetTargetIndex() const = 0;
	virtual IATDebugTarget *GetTarget() const = 0;
	virtual void GetTargetList(vdfastvector<IATDebugTarget *>& targets) = 0;

	virtual VDEvent<IATDebugger, bool>& OnRunStateChanged() = 0;

	virtual VDEvent<IATDebugger, ATDebuggerOpenEvent *>& OnDebuggerOpen() = 0;

	virtual void SetOnRequestFile(vdfunction<void(ATDebuggerRequestFileEvent&)> fn) = 0;
};

struct ATDebuggerSymbol {
	ATSymbol mSymbol;
	uint32 mModuleId;
};

class IATDebuggerSymbolLookup {
public:
	virtual bool GetSourceFilePath(uint32 moduleId, uint16 fileId, VDStringW& path) = 0;
	virtual bool LookupSymbol(uint32 addr, uint32 flags, ATSymbol& symbol) = 0;
	virtual bool LookupSymbol(uint32 addr, uint32 flags, ATDebuggerSymbol& symbol) = 0;
	virtual bool LookupLine(uint32 addr, bool searchUp, uint32& moduleId, ATSourceLineInfo& lineInfo) = 0;
	virtual bool LookupFile(const wchar_t *fileName, uint32& moduleId, uint16& fileId) = 0;
	virtual void GetLinesForFile(uint32 moduleId, uint16 fileId, vdfastvector<ATSourceLineInfo>& lines) = 0;
	virtual sint32 ResolveSymbol(const char *s, bool allowGlobal = false, bool allowShortBase = true, bool allowNakedHex = true) = 0;
};

class IATDebuggerClient {
public:
	virtual void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) = 0;
	virtual void OnDebuggerEvent(ATDebugEvent eventId) = 0;
};

IATDebugger *ATGetDebugger();
IATDebuggerSymbolLookup *ATGetDebuggerSymbolLookup();

#endif
