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

#include <vd2/system/vdstl.h>
#include "symbols.h"

struct ATSymbol;
struct ATSourceLineInfo;
class IATDebuggerClient;

enum ATDebugEvent {
	kATDebugEvent_BreakpointsChanged
};

enum ATDebugSrcMode {
	kATDebugSrcMode_Same,
	kATDebugSrcMode_Disasm,
	kATDebugSrcMode_Source
};

struct ATDebuggerSystemState {
	uint16	mPC;
	uint16	mInsnPC;
	uint8	mA;
	uint8	mX;
	uint8	mY;
	uint8	mP;
	uint8	mS;
	uint8	mAH;
	uint8	mXH;
	uint8	mYH;
	uint8	mSH;
	uint8	mK;
	uint8	mB;
	uint16	mD;

	uint32	mPCModuleId;
	uint16	mPCFileId;
	uint16	mPCLine;

	uint16	mFramePC;
	bool	mbRunning;
	bool	mbEmulation;
};

struct ATCallStackFrame {
	uint16	mPC;
	uint8	mS;
	uint8	mP;
	uint8	mK;
};

class IATDebugger {
public:
	virtual bool IsRunning() const = 0;

	virtual void Detach() = 0;
	virtual void SetSourceMode(ATDebugSrcMode src) = 0;
	virtual void Break() = 0;
	virtual void Run(ATDebugSrcMode sourceMode) = 0;
	virtual void RunTraced() = 0;
	virtual void ClearAllBreakpoints() = 0;
	virtual void ToggleBreakpoint(uint16 addr) = 0;
	virtual void StepInto(ATDebugSrcMode sourceMode) = 0;
	virtual void StepOver(ATDebugSrcMode sourceMode) = 0;
	virtual void StepOut(ATDebugSrcMode sourceMode) = 0;
	virtual void SetPC(uint16 pc) = 0;
	virtual uint16 GetFramePC() const = 0;
	virtual void SetFramePC(uint16 pc) = 0;
	virtual uint32 GetCallStack(ATCallStackFrame *dst, uint32 maxCount) = 0;
	virtual void DumpCallStack() = 0;
	virtual void ListModules() = 0;

	virtual void AddClient(IATDebuggerClient *client, bool requestUpdate = false) = 0;
	virtual void RemoveClient(IATDebuggerClient *client) = 0;
	virtual void RequestClientUpdate(IATDebuggerClient *client) = 0;

	virtual uint32 LoadSymbols(const wchar_t *fileName) = 0;
	virtual void UnloadSymbols(uint32 moduleId) = 0;

	virtual sint32 ResolveSymbol(const char *s, bool allowGlobal = false) = 0;
	virtual VDStringA GetAddressText(uint32 globalAddr, bool useHexSpecifier) = 0;
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
	virtual bool LookupLine(uint32 addr, uint32& moduleId, ATSourceLineInfo& lineInfo) = 0;
	virtual bool LookupFile(const wchar_t *fileName, uint32& moduleId, uint16& fileId) = 0;
	virtual void GetLinesForFile(uint32 moduleId, uint16 fileId, vdfastvector<ATSourceLineInfo>& lines) = 0;
};

class IATDebuggerClient {
public:
	virtual void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) = 0;
	virtual void OnDebuggerEvent(ATDebugEvent eventId) = 0;
};

IATDebugger *ATGetDebugger();
IATDebuggerSymbolLookup *ATGetDebuggerSymbolLookup();

#endif
