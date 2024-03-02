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

#ifndef AT_CONSOLE_H
#define AT_CONSOLE_H

#include <vd2/system/VDString.h>

///////////////////////////////////////////////////////////////////////////
void ATConsoleWrite(const char *s);
void ATConsolePrintf(const char *format, ...);
void ATConsoleTaggedPrintf(const char *format, ...);

///////////////////////////////////////////////////////////////////////////

void ATConsoleGetFont(struct tagLOGFONTW& font, int& pointSizeTenths);
void ATConsoleSetFont(const struct tagLOGFONTW& font, int pointSizeTenths);
void ATConsoleSetFontDpi(unsigned dpi);

void ATShowConsole();
void ATOpenConsole();
void ATCloseConsole();
bool ATIsDebugConsoleActive();

class IATSourceWindow {
public:
	virtual const wchar_t *GetPath() const = 0;
	virtual const wchar_t *GetPathAlias() const = 0;

	virtual void FocusOnLine(int line) = 0;
	virtual void ActivateLine(int line) = 0;
};

IATSourceWindow *ATGetSourceWindow(const wchar_t *s);
IATSourceWindow *ATOpenSourceWindow(const wchar_t *s);

///////////////////////////////////////////////////////////////////////////

class ATUIPane;
class ATFrameWindow;

void ATGetUIPanes(vdfastvector<ATUIPane *>& panes);
ATUIPane *ATGetUIPane(uint32 id);
ATUIPane *ATGetUIPaneByFrame(ATFrameWindow *frame);
void ATActivateUIPane(uint32 id, bool giveFocus, bool visible = true, uint32 relid = 0, int reldock = 0);
void ATCloseUIPane(uint32 id);

ATUIPane *ATUIGetActivePane();
uint32 ATUIGetActivePaneId();

enum ATUIPaneCommandId {
	kATUIPaneCommandId_DebugRun,
	kATUIPaneCommandId_DebugToggleBreakpoint,
	kATUIPaneCommandId_DebugStepOver,
	kATUIPaneCommandId_DebugStepInto,
	kATUIPaneCommandId_DebugStepOut
};

class IATUIDebuggerPane {
public:
	enum { kTypeID = 'uidp' };

	virtual bool OnPaneCommand(ATUIPaneCommandId id) = 0;
};

bool ATRestorePaneLayout(const char *name);
void ATSavePaneLayout(const char *name);
void ATLoadDefaultPaneLayout();

enum {
	kATUIPaneId_None,
	kATUIPaneId_Display,
	kATUIPaneId_Console,
	kATUIPaneId_Registers,
	kATUIPaneId_CallStack,
	kATUIPaneId_Disassembly,
	kATUIPaneId_History,
	kATUIPaneId_Memory,
	kATUIPaneId_PrinterOutput,
	kATUIPaneId_Profiler,
	kATUIPaneId_DebugDisplay,

	kATUIPaneId_IndexMask = 0xFF,
	kATUIPaneId_MemoryN = 0x100,
	kATUIPaneId_WatchN = 0x200,

	kATUIPaneId_Source = 0x10000,

	kATUIPaneId_Count
};

void *ATGetConsoleFontW32();
int ATGetConsoleFontLineHeightW32();

#endif
