//	Altirra - Atari 800/800XL/5200 emulator
//	Compiler - Command-line driver
//	Copyright (C) 2009-2012 Avery Lee
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

extern int cmd_lzpack(int argc, const char *const *argv);
extern int cmd_lzunpack(int argc, const char *const *argv);

void print_usage();

int main(int argc, const char *const *argv) {
	if (argc < 2) {
		print_usage();
		exit(0);
	}

	const char *cmdname = argv[1];
	--argc;
	++argv;

	if (!strcmp(cmdname, "lzpack"))
		return cmd_lzpack(argc, argv);
	else if (!strcmp(cmdname, "lzunpack"))
		return cmd_lzunpack(argc, argv);

	print_usage();
	return 10;
}

void print_usage() {
	puts("atcompiler <command> [arguments...]");
	puts("");
	puts("Commands:");
	puts("  lzpack - compress using LZ77");
	puts("  lzunpack - decompress using LZ77");
}
