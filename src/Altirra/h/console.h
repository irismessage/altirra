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

void ATConsoleGetFont(struct tagLOGFONTW& font);
void ATConsoleSetFont(const struct tagLOGFONTW& font);

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

ATUIPane *ATGetUIPane(uint32 id);
ATUIPane *ATGetUIPaneByFrame(ATFrameWindow *frame);
void ATActivateUIPane(uint32 id, bool giveFocus, bool visible = true);

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
	kATUIPaneId_Count
};

void *ATGetConsoleFontW32();
int ATGetConsoleFontLineHeightW32();

#endif
