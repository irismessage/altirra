//	Asuka - VirtualDub Build/Post-Mortem Utility
//	Copyright (C) 2005-2007 Avery Lee
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

#include <stdafx.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/file.h>

#include <windows.h>

#include <stdio.h>
#include <map>
#include <string>

#include "utils.h"
#include "resource.h"

void help() {
	puts("Altirra Build/Post-Mortem Utility for "
#if VD_CPU_AMD64
			"AMD64"
#else
			"80x86"
#endif
			);
	puts("Copyright (C) Avery Lee 2005-2021. Licensed under GNU General Public License");
	puts("");
	puts("Usage: Asuka <command> [args...]");
	puts("");
	puts("Asuka fontencode   Extract TrueType font glyph outlines");
	puts("Asuka fxc10        Compile shaders for Direct3D");
	puts("Asuka makearray    Convert binary file to C array");
	puts("Asuka maketables   Regenerate precomputed tables");
	puts("Asuka snapsetup    Temporarily change windows settings for screencaps");
	puts("Asuka checkimports Check DLL/EXE imports");
	puts("Asuka hash         Compute string hashes");
	puts("Asuka signxml      Sign XML document");
	puts("Asuka signexport   Export public key for XML signing");
	exit(5);
}

void fail(const char *format, ...) {
	va_list val;
	va_start(val, format);
	fputs("Asuka: Failed: ", stdout);
	vprintf(format, val);
	fputc('\n', stdout);
	va_end(val);
	exit(10);
}
