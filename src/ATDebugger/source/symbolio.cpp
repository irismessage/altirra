//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008-2022 Avery Lee
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
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#include <stdafx.h>
#include <optional>
#include <bit>
#include <vd2/system/binary.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/strutil.h>
#include <at/atcore/address.h>
#include <at/atcore/vfs.h>
#include <at/atdebugger/internal/symstore.h>

///////////////////////////////////////////////////////////////////////////////

void ATLoadSymbols(ATSymbolStore& symstore, const wchar_t *filename, IVDRandomAccessStream& stream);
void ATLoadSymbolsFromMADSListing(ATSymbolStore& symstore, VDTextStream& ifile);
void ATLoadSymbols(ATSymbolStore& symstore, VDTextStream& ifile);
void ATLoadSymbolsFromCC65Labels(ATSymbolStore& symstore, VDTextStream& ifile);
void ATLoadSymbolsFromCC65DbgFile(ATSymbolStore& symstore, VDTextStream& ifile);
void ATLoadSymbolsFromLabels(ATSymbolStore& symstore, VDTextStream& ifile);
void ATLoadSymbolsFromKernelListing(ATSymbolStore& symstore, VDTextStream& ifile);
void ATLoadSymbolsFromELF(ATSymbolStore& symstore, IVDRandomAccessStream& stream);

void ATLoadSymbols(ATSymbolStore& symstore, const wchar_t *filename, IVDRandomAccessStream& stream) {
	uint8 buf[4];
	if (4 == stream.ReadData(buf, 4)) {
		static const uint8 kElfSignature[4] = { 0x7F, (uint8)'E', (uint8)'L', (uint8)'F' };

		if (!memcmp(buf, kElfSignature, 4))
			return ATLoadSymbolsFromELF(symstore, stream);
	}

	stream.Seek(0);

	{
		VDTextStream ts(&stream);

		const char *line = ts.GetNextLine();

		if (line) {
			if (!strncmp(line, "mads ", 5) || !strncmp(line, "xasm ", 5)) {
				ATLoadSymbolsFromMADSListing(symstore, ts);
				return;
			}

			if (!strncmp(line, "Altirra symbol file", 19)) {
				ATLoadSymbols(symstore, ts);
				return;
			}

			if (!strncmp(line, "ca65 ", 5))
				throw MyError("CA65 listings are not supported.");

			if (!strncmp(line, "version\tmajor=2,minor=", 22)) {
				ATLoadSymbolsFromCC65DbgFile(symstore, ts);
				return;
			}
		}
	}

	stream.Seek(0);

	VDTextStream ts2(&stream);

	const wchar_t *ext = VDFileSplitExt(filename);
	if (!vdwcsicmp(ext, L".lbl")) {
		ATLoadSymbolsFromCC65Labels(symstore, ts2);
		return;
	}

	if (!vdwcsicmp(ext, L".lab")) {
		ATLoadSymbolsFromLabels(symstore, ts2);
		return;
	}

	ATLoadSymbolsFromKernelListing(symstore, ts2);
}

void ATLoadSymbols(ATSymbolStore& symstore, VDTextStream& ifile) {
	enum {
		kStateNone,
		kStateSymbols
	} state = kStateNone;

	symstore.Init(0, 0x10000);

	int lineno = 0;
	while(const char *line = ifile.GetNextLine()) {
		++lineno;

		while(*line == ' ' || *line == '\t')
			++line;

		// skip comments
		if (*line == ';')
			continue;

		// skip blank lines
		if (!*line)
			continue;

		// check for group
		if (*line == '[') {
			const char *groupStart = ++line;

			while(*line != ']') {
				if (!*line)
					throw ATSymbolFileParsingException(lineno);
				++line;
			}

			VDStringSpanA groupName(groupStart, line);

			if (groupName == "symbols")
				state = kStateSymbols;
			else
				state = kStateNone;

			continue;
		}

		if (state == kStateSymbols) {
			// rwx address,length name
			uint32 rwxflags = 0;
			for(;;) {
				char c = *line++;

				if (!c)
					throw ATSymbolFileParsingException(lineno);

				if (c == ' ' || c == '\t')
					break;

				if (c == 'r')
					rwxflags |= kATSymbol_Read;
				else if (c == 'w')
					rwxflags |= kATSymbol_Write;
				else if (c == 'x')
					rwxflags |= kATSymbol_Execute;
			}

			if (!rwxflags)
				throw ATSymbolFileParsingException(lineno);

			while(*line == ' ' || *line == '\t')
				++line;

			char *end;
			unsigned long address = strtoul(line, &end, 16);

			if (line == end)
				throw ATSymbolFileParsingException(lineno);

			line = end;

			if (*line++ != ',')
				throw ATSymbolFileParsingException(lineno);

			unsigned long length = strtoul(line, &end, 16);
			if (line == end)
				throw ATSymbolFileParsingException(lineno);

			line = end;

			while(*line == ' ' || *line == '\t')
				++line;

			const char *nameStart = line;

			while(*line != ' ' && *line != '\t' && *line != ';' && *line)
				++line;

			if (line == nameStart)
				throw ATSymbolFileParsingException(lineno);

			const char *nameEnd = line;

			while(*line == ' ' || *line == '\t')
				++line;

			if (*line && *line != ';')
				throw ATSymbolFileParsingException(lineno);

			symstore.AddSymbol(address, VDStringA(nameStart, nameEnd).c_str(), length);
		}
	}
}

void ATLoadSymbolsFromCC65Labels(ATSymbolStore& symstore, VDTextStream& ifile) {
	VDStringA label;

	symstore.Init(0, 0x10000);

	while(const char *line = ifile.GetNextLine()) {
		unsigned long addr;
		int nameoffset;
		char namecheck;

		if (2 != sscanf(line, "al %6lx %n%c", &addr, &nameoffset, &namecheck))
			continue;

		if (namecheck == '.')
			++nameoffset;

		const char *labelStart = line + nameoffset;
		const char *labelEnd = labelStart;

		for(;;) {
			char c = *labelEnd;

			if (!c || c == ' ' || c == '\t' || c == '\n' || c== '\r')
				break;

			++labelEnd;
		}

		label.assign(labelStart, labelEnd);
		symstore.AddSymbol(addr, label.c_str());
	}
}

namespace {
	struct CC65Span {
		uint32 mSeg;
		uint32 mStart;
		uint32 mSize;
	};

	struct CC65Line {
		uint32 mFile;
		uint32 mLine;
		uint32 mSpan;
	};
}

void ATLoadSymbolsFromCC65DbgFile(ATSymbolStore& symstore, VDTextStream& ifile) {
	enum {
		kAttrib_Id,
		kAttrib_Name,
		kAttrib_Size,
		kAttrib_Mtime,
		kAttrib_Mod,
		kAttrib_Start,
		kAttrib_Addrsize,
		kAttrib_Type,
		kAttrib_Oname,
		kAttrib_Ooffs,
		kAttrib_Seg,
		kAttrib_Scope,
		kAttrib_Def,
		kAttrib_Ref,
		kAttrib_Val,
		kAttrib_File,
		kAttrib_Line,
		kAttrib_Span,

		kAttribCount
	};

	static const char *const kAttribNames[]={
		"id",
		"name",
		"size",
		"mtime",
		"mod",
		"start",
		"addrsize",
		"type",
		"oname",
		"ooffs",
		"seg",
		"scope",
		"def",
		"ref",
		"val",
		"file",
		"line",
		"span",
	};

	VDASSERTCT(vdcountof(kAttribNames) == kAttribCount);

	typedef vdhashmap<uint32, uint32> Segs;
	Segs segs;

	typedef vdhashmap<uint32, uint16> Files;
	Files files;

	typedef vdhashmap<uint32, CC65Span> CC65Spans;
	CC65Spans cc65spans;

	typedef vdfastvector<CC65Line> CC65Lines;
	CC65Lines cc65lines;

	VDStringSpanA attribs[kAttribCount];

	symstore.Init(0, 0x10000);

	while(const char *line = ifile.GetNextLine()) {
		VDStringRefA r(line);
		VDStringRefA token;

		if (!r.split('\t', token))
			continue;

		// parse out attributes
		uint32 attribMask = 0;

		while(!r.empty()) {
			VDStringRefA attrToken;
			if (!r.split('=', attrToken))
				break;

			int attr = -1;

			for(int i=0; i<(int)sizeof(kAttribNames)/sizeof(kAttribNames[0]); ++i) {
				if (attrToken == kAttribNames[i]) {
					attr = i;
					break;
				}
			}

			if (!r.empty() && r.front() == '"') {
				r.split('"', attrToken);
				r.split('"', attrToken);
				if (!r.empty() && r.front() == ',') {
					VDStringRefA dummyToken;
					r.split(',', dummyToken);
				}
			} else {
				if (!r.split(',', attrToken))
					attrToken = r;
			}

			if (attr >= 0) {
				attribs[attr] = attrToken;
				attribMask |= (1 << attr);
			}
		}

		if (token == "file") {
			// file id=0,name="hello.s",size=1682,mtime=0x532BC30D,mod=0
			if (~attribMask & ((1 << kAttrib_Id) | (1 << kAttrib_Name)))
				continue;

			unsigned id;
			char dummy;
			if (1 != sscanf(VDStringA(attribs[kAttrib_Id]).c_str(), "%u%c", &id, &dummy))
				continue;

			Files::insert_return_type result = files.insert(id);

			if (result.second)
				result.first->second = symstore.AddFileName(VDTextAToW(attribs[kAttrib_Name]).c_str());
		} else if (token == "line") {
			// line id=26,file=0,line=59,span=15
			if (~attribMask & ((1 << kAttrib_Id) | (1 << kAttrib_File) | (1 << kAttrib_Line) | (1 << kAttrib_Span) | (1 << kAttrib_Type)))
				continue;

			unsigned id;
			char dummy;
			if (1 != sscanf(VDStringA(attribs[kAttrib_Id]).c_str(), "%u%c", &id, &dummy))
				continue;

			unsigned file;
			if (1 != sscanf(VDStringA(attribs[kAttrib_File]).c_str(), "%u%c", &file, &dummy))
				continue;

			unsigned lineno;
			if (1 != sscanf(VDStringA(attribs[kAttrib_Line]).c_str(), "%u%c", &lineno, &dummy))
				continue;

			unsigned type;
			if (1 != sscanf(VDStringA(attribs[kAttrib_Type]).c_str(), "%u%c", &type, &dummy))
				continue;

			if (type != 1)
				continue;

			// span can have a + delimited list, which we must parse out; we produce one entry per span
			VDStringRefA spanList(attribs[kAttrib_Span]);

			while(!spanList.empty()) {
				VDStringRefA spanToken;

				if (!spanList.split('+', spanToken)) {
					spanToken = spanList;
					spanList.clear();
				}

				unsigned span;
				if (1 == sscanf(VDStringA(spanToken).c_str(), "%u%c", &span, &dummy)) {
					CC65Line cc65line;
					cc65line.mFile = file;
					cc65line.mLine = lineno;
					cc65line.mSpan = span;

					cc65lines.push_back(cc65line);
				}
			}
		} else if (token == "seg") {
			// seg id=0,name="CODE",start=0x002092,size=0x0863,addrsize=absolute,type=ro,oname="hello.xex",ooffs=407
			if (~attribMask & ((1 << kAttrib_Id) | (1 << kAttrib_Start) | (1 << kAttrib_Addrsize)))
				continue;

			if (attribs[kAttrib_Addrsize] != "absolute")
				continue;

			unsigned id;
			char dummy;
			if (1 != sscanf(VDStringA(attribs[kAttrib_Id]).c_str(), "%u%c", &id, &dummy))
				continue;

			unsigned start;
			if (1 != sscanf(VDStringA(attribs[kAttrib_Start]).c_str(), "%i%c", &start, &dummy))
				continue;

			segs[id] = start;
		} else if (token == "span") {
			// span id=0,seg=3,start=0,size=2,type=1
			if (~attribMask & ((1 << kAttrib_Id) | (1 << kAttrib_Seg) | (1 << kAttrib_Start) | (1 << kAttrib_Size)))
				continue;

			unsigned id;
			char dummy;
			if (1 != sscanf(VDStringA(attribs[kAttrib_Id]).c_str(), "%u%c", &id, &dummy))
				continue;

			unsigned seg;
			if (1 != sscanf(VDStringA(attribs[kAttrib_Seg]).c_str(), "%u%c", &seg, &dummy))
				continue;

			unsigned start;
			if (1 != sscanf(VDStringA(attribs[kAttrib_Start]).c_str(), "%u%c", &start, &dummy))
				continue;

			unsigned size;
			if (1 != sscanf(VDStringA(attribs[kAttrib_Size]).c_str(), "%u%c", &size, &dummy))
				continue;

			CC65Span span;
			span.mSeg = seg;
			span.mStart = start;
			span.mSize = size;

			cc65spans[id] = span;
		} else if (token == "sym") {
			// sym id=0,name="L0002",addrsize=absolute,size=1,scope=1,def=52+50,ref=16,val=0x20DD,seg=0,type=lab
			if (~attribMask & ((1 << kAttrib_Id) | (1 << kAttrib_Name) | (1 << kAttrib_Addrsize) | (1 << kAttrib_Val)))
				continue;

			if (attribs[kAttrib_Addrsize] != "absolute" && attribs[kAttrib_Addrsize] != "zeropage")
				continue;

			unsigned val;
			char dummy;
			if (1 != sscanf(VDStringA(attribs[kAttrib_Val]).c_str(), "%i%c", &val, &dummy))
				continue;

			unsigned size = 1;
			if (attribMask & (1 << kAttrib_Size)) {
				if (1 != sscanf(VDStringA(attribs[kAttrib_Size]).c_str(), "%u%c", &size, &dummy))
					continue;
			}

			symstore.AddSymbol(val, VDStringA(attribs[kAttrib_Name]).c_str(), size);
		}
	}

	// process line number information
	for(CC65Lines::const_iterator it(cc65lines.begin()), itEnd(cc65lines.end());
		it != itEnd;
		++it)
	{
		const CC65Line& cline = *it;

		// Okay, we need to do the following lookups:
		//	line -> file
		//	     -> span -> seg
		Files::const_iterator itFile = files.find(cline.mFile);
		if (itFile == files.end())
			continue;

		CC65Spans::const_iterator itSpan = cc65spans.find(cline.mSpan);
		if (itSpan == cc65spans.end())
			continue;

		Segs::const_iterator itSeg = segs.find(itSpan->second.mSeg);
		if (itSeg == segs.end())
			continue;

		const uint32 addr = itSeg->second + itSpan->second.mStart;
		const uint16 fileId = itFile->second;

		symstore.AddSourceLine(fileId, cline.mLine, addr, itSpan->second.mSize);
	}
}

void ATLoadSymbolsFromLabels(ATSymbolStore& symstore, VDTextStream& ifile) {
	symstore.Init(0, 0x10000);

	VDStringA label;
	while(const char *line = ifile.GetNextLine()) {
		unsigned long addr;
		int nameoffset;
		char namecheck;

		if (2 != sscanf(line, "%6lx %n%c", &addr, &nameoffset, &namecheck))
			continue;

		const char *labelStart = line + nameoffset;
		const char *labelEnd = labelStart;

		for(;;) {
			char c = *labelEnd;

			if (!c || c == ' ' || c == '\t' || c == '\n' || c== '\r')
				break;

			++labelEnd;
		}

		label.assign(labelStart, labelEnd);
		symstore.AddSymbol(addr, label.c_str());
	}
}

namespace {
	struct FileEntry {
		uint16 mFileId;
		int mNextLine;
		int mForcedLine;
	};
}

void ATLoadSymbolsFromMADSListing(ATSymbolStore& symstore, VDTextStream& ifile) {
	uint16 fileid = 0;

	enum {
		kModeNone,
		kModeSource,
		kModeLabels
	} mode = kModeNone;

	VDStringA label;

	typedef vdfastvector<FileEntry> FileStack;
	FileStack fileStack;
	int nextline = 1;
	bool macroMode = false;
	int forcedLine = -1;
	bool directivePending = false;

	uint32 bankOffsetMap[256] {};
	bool extBanksUsed = false;

	symstore.Init(0, 0x1000000);

	vdfastvector<ATSymbolStore::Symbol> symbols;
	vdfastvector<ATSymbolStore::Directive> directives;
	vdfastvector<std::tuple<uint16, uint32, uint32>> lines;		// file/line/offset

	while(const char *line = ifile.GetNextLine()) {
		char space0;
		int origline;
		int address;
		int address2;
		unsigned bank;
		char dummy;
		char space1;
		char space2;
		char space3;
		char space4;
		int op;

		if (!strncmp(line, "Macro: ", 7)) {
			macroMode = true;
		} else if (!strncmp(line, "Source: ", 8)) {
			if (macroMode)
				macroMode = false;
			else {
				if (fileid) {
					FileEntry& fe = fileStack.push_back();
					fe.mNextLine = nextline;
					fe.mFileId = fileid;
					fe.mForcedLine = forcedLine;
				}

				fileid = symstore.AddFileName(VDTextAToW(line+8).c_str());
				forcedLine = -1;
				mode = kModeSource;
				nextline = 1;
			}

			continue;
		} else if (!strncmp(line, "Label table:", 12)) {
			fileid = 0;
			mode = kModeLabels;
		}

		if (mode == kModeSource) {
			if (macroMode)
				continue;

			bool valid = false;
			int afterline = 0;

			if (2 == sscanf(line, "%c%5d%n", &space0, &origline, &afterline)
				&& space0 == ' ')
			{
				if (fileid && origline > 0) {
					// check for discontinuous line (mads doesn't re-emit the parent line)
					if (origline != nextline) {
						FileStack::const_reverse_iterator it(fileStack.rbegin()), itEnd(fileStack.rend());

						for(; it != itEnd; ++it) {
							const FileEntry& fe = *it;
							if (fe.mNextLine == origline && fe.mFileId != fileid) {
								fileid = fe.mFileId;
								forcedLine = fe.mForcedLine;
								break;
							}
						}
					}

					nextline = origline + 1;
				}

				// 105 1088 8D ...
				//  12 02,ACBB A2 FF...
				// 131 2000-201F> 00 ...
				//   4 FFFF> 2000-2006> EA              nop
				//  12 03,2000-2000> 60                         rts
				if (7 == sscanf(line, "%c%5d%c%4x%c%2x%c", &space0, &origline, &space1, &address, &space2, &op, &space3)
					&& space0 == ' '
					&& space1 == ' '
					&& space2 == ' '
					&& (space3 == ' ' || space3 == '\t'))
				{
					valid = true;
				} else if (8 == sscanf(line, "%c%5d%c%2x,%4x%c%2x%c", &space0, &origline, &space1, &bank, &address, &space2, &op, &space3)
					&& space0 == ' '
					&& space1 == ' '
					&& space2 == ' '
					&& (space3 == ' ' || space3 == '\t'))
				{
					valid = true;
					address += bank << 16;
				} else if (8 == sscanf(line, "%c%5d%c%4x-%4x>%c%2x%c", &space0, &origline, &space1, &address, &address2, &space2, &op, &space3)
					&& space0 == ' '
					&& space1 == ' '
					&& space2 == ' '
					&& (space3 == ' ' || space3 == '\t'))
				{
					valid = true;
				} else if (9 == sscanf(line, "%c%5d%cFFFF>%c%4x-%4x>%c%2x%c", &space0, &origline, &space1, &space2, &address, &address2, &space3, &op, &space4)
					&& space0 == ' '
					&& space1 == ' '
					&& space2 == ' '
					&& space3 == ' '
					&& (space4 == ' ' || space4 == '\t'))
				{
					valid = true;
				} else if (9 == sscanf(line, "%c%5d%c%2x,%4x-%4x>%c%2x%c", &space0, &origline, &space1, &bank, &address, &address2, &space2, &op, &space3)
					&& space0 == ' '
					&& space1 == ' '
					&& space2 == ' '
					&& (space3 == ' ' || space3 == '\t'))
				{
					valid = true;
					address += bank << 16;
				} else {
					// Look for a comment line.
					const char *s = line + afterline;

					while(*s == ' ' || *s == '\t')
						++s;

					if (*s == ';') {
						// We have a comment. Check if it has one of these special syntaxes:
						// ; ### file(num)    [line number directive]
						++s;

						while(*s == ' ' || *s == '\t')
							++s;

						if (s[0] == '#' && s[1] == '#') {
							s += 2;

							if (*s == '#') {
								// ;### file(line) ...     [line number directive]
								++s;
								while(*s == ' ' || *s == '\t')
									++s;

								const char *fnstart = s;
								const char *fnend;
								if (*s == '"') {
									++s;
									fnstart = s;

									while(*s && *s != '"')
										++s;

									fnend = s;

									if (*s)
										++s;
								} else {
									while(*s && *s != ' ' && *s != '(')
										++s;

									fnend = s;
								}

								while(*s == ' ' || *s == '\t')
									++s;

								char term = 0;
								unsigned newlineno;
								if (2 == sscanf(s, "(%u %c", &newlineno, &term) && term == ')') {
									fileid = symstore.AddFileName(VDTextAToW(fnstart, (int)(fnend - fnstart)).c_str());
									forcedLine = newlineno;
								}
							} else if (!strncmp(s, "ASSERT", 6) && (s[6] == ' ' || s[6] == '\t')) {
								// ;##ASSERT <address> <condition>
								s += 7;

								while(*s == ' ' || *s == '\t')
									++s;

								VDStringSpanA arg(s);
								arg = arg.trim(" \t");

								if (!arg.empty()) {
									ATSymbolStore::Directive& dir = directives.push_back();
									dir.mOffset = 0;
									dir.mType = kATSymbolDirType_Assert;
									dir.mArgOffset = symstore.AddName(arg);

									directivePending = true;
								}
							} else if (!strncmp(s, "TRACE", 5) && (s[5] == ' ' || s[5] == '\t')) {
								// ;##TRACE <printf arguments>
								s += 6;

								while(*s == ' ' || *s == '\t')
									++s;

								VDStringSpanA arg(s);
								arg = arg.trim(" \t");

								if (!arg.empty()) {
									ATSymbolStore::Directive& dir = directives.push_back();
									dir.mOffset = 0;
									dir.mType = kATSymbolDirType_Trace;
									dir.mArgOffset = symstore.AddName(arg);

									directivePending = true;
								}
							} else if (!strncmp(s, "BANK", 4) && (s[4] == ' ' || s[4] == '\t')) {
								s += 5;

								while(*s == ' ' || *s == '\t')
									++s;

								unsigned bank = 0;
								if (*s == '$') {
									char *t = const_cast<char *>(s);
									bank = strtoul(s + 1, &t, 16);
									s = t;
								} else {
									char *t = const_cast<char *>(s);
									bank = strtoul(s, &t, 10);
									s = t;
								}

								if (bank < 256) {
									while(*s == ' ' || *s == '\t')
										++s;

									const char *typeNameStart = s;
									while(*s && *s != ' ' && *s != '\t')
										++s;

									const VDStringSpanA arg(typeNameStart, s);

									while(*s == ' ' || *s == '\t')
										++s;

									uint32 bankOffset = 0;
									bool needDestBank = false;

									if (arg.comparei("default") == 0) {
										bankOffset = 0U - (bank << 16);
									} else if (arg.comparei("ram") == 0) {
										bankOffset = 0U - (bank << 16) + kATAddressSpace_RAM;
									} else if (arg.comparei("ext") == 0) {
										bankOffset = 0U - (bank << 16) + kATAddressSpace_PORTB;
										needDestBank = true;
									} else if (arg.comparei("cart") == 0) {
										bankOffset = 0U - (bank << 16) + kATAddressSpace_CB;
										needDestBank = true;
									}

									if (needDestBank) {
										unsigned destBank = 0;
										bool destBankValid = false;
										char dummy;

										if (*s == '$') {
											destBankValid = (1 == sscanf(s + 1, "%x%c", &destBank, &dummy));
										} else {
											destBankValid = (1 == sscanf(s, "%u%c", &destBank, &dummy));
										}

										if (destBankValid && destBank < 256)
											bankOffset += destBank << 16;
										else
											bankOffset = 0;
									}

									if (bankOffset) {
										bankOffsetMap[bank] = bankOffset;
										extBanksUsed = true;
									}
								}
							}
						}
					}
				}
			} else if (8 == sscanf(line, "%6x%c%c%c%c%c%2x%c", &address, &space0, &space1, &dummy, &space2, &space3, &op, &space4)
				&& space0 == ' '
				&& space1 == ' '
				&& space2 == ' '
				&& space3 == ' '
				&& space4 == ' '
				&& isdigit((unsigned char)dummy))
			{
				valid = true;
			} else if (6 == sscanf(line, "%6d%c%4x%c%2x%c", &origline, &space0, &address, &space1, &op, &space2)
				&& space0 == ' '
				&& space1 == ' '
				&& (space2 == ' ' || space2 == '\t'))
			{
				valid = true;
			}

			if (valid) {
				if (directivePending) {
					for(auto it = directives.rbegin(), itEnd = directives.rend(); it != itEnd; ++it) {
						ATSymbolStore::Directive& d = *it;

						if (d.mOffset)
							break;

						d.mOffset = address;
					}

					directivePending = false;
				}

				if (forcedLine >= 0) {
					lines.emplace_back(fileid, forcedLine, address);
					forcedLine = -2;
				} else if (forcedLine == -1)
					lines.emplace_back(fileid, origline, address);
			}
		} else if (mode == kModeLabels) {
			// MADS:
			// 00      11A3    DLI
			//
			// xasm:
			//         2000 MAIN

			if (isxdigit((unsigned char)line[0])) {
				int pos1 = -1;
				int pos2 = -1;
				unsigned bank;
				if (2 == sscanf(line, "%2x %4x %n%*s%n", &bank, &address, &pos1, &pos2) && pos1 >= 0 && pos2 > pos1) {
					label.assign(line + pos1, line + pos2);

					char end;
					char dummy;
					unsigned srcBank;
					if (2 == sscanf(label.c_str(), "__ATBANK_%02X_RA%c%c", &srcBank, &end, &dummy) && end == 'M' && srcBank < 256 && address < 256) {
						extBanksUsed = true;
						bankOffsetMap[srcBank] = kATAddressSpace_RAM - (srcBank << 16);
					} else if (2 == sscanf(label.c_str(), "__ATBANK_%02X_CAR%c%c", &srcBank, &end, &dummy) && end == 'T' && srcBank < 256 && address < 256) {
						extBanksUsed = true;
						bankOffsetMap[srcBank] = kATAddressSpace_CB - (srcBank << 16) + (address << 16);
					} else if (2 == sscanf(label.c_str(), "__ATBANK_%02X_SHARE%c%c", &srcBank, &end, &dummy) && end == 'D' && srcBank < 256 && address < 256) {
						extBanksUsed = true;
						bankOffsetMap[srcBank] = kATAddressSpace_CB - (srcBank << 16) + 0x1000000;
					} else if (2 == sscanf(label.c_str(), "__ATBANK_%02X_EX%c%c", &srcBank, &end, &dummy) && end == 'T' && srcBank < 256 && address < 256) {
						extBanksUsed = true;
						bankOffsetMap[srcBank] = kATAddressSpace_PORTB - (srcBank << 16) + 0x1000000;
					} else if (label == "__ATBANK_00_GLOBAL") {
						symstore.SetBank0Global(true);
					} else {
						ATSymbolStore::Symbol& sym = symbols.emplace_back();
						sym.mOffset = ((uint32)bank << 16) + address;
						sym.mNameOffset = symstore.AddName(label);
						sym.mFlags = kATSymbol_Read | kATSymbol_Write | kATSymbol_Execute;
						sym.mSize = 1;
					}
				}
			} else {
				int pos1 = -1;
				int pos2 = -1;
				if (1 == sscanf(line, "%4x %n%*s%n", &address, &pos1, &pos2) && pos1 >= 0 && pos2 >= pos1) {
					label.assign(line + pos1, line + pos2);

					ATSymbolStore::Symbol& sym = symbols.emplace_back();
					sym.mOffset = address;
					sym.mNameOffset = symstore.AddName(label);
					sym.mFlags = kATSymbol_Read | kATSymbol_Write | kATSymbol_Execute;
					sym.mSize = 1;
				}
			}
		}
	}

	if (extBanksUsed) {
		symstore.Init(0, 0xFFFFFFFF);

		for(ATSymbolStore::Symbol& sym : symbols) {
			uint32 symBankOffset = bankOffsetMap[(sym.mOffset >> 16) & 0xFF];

			if (symBankOffset) {
				sym.mOffset += symBankOffset;

				switch(sym.mOffset & kATAddressSpaceMask) {
					case kATAddressSpace_PORTB:
						if ((uint16)(sym.mOffset - 0x4000) >= 0x4000)
							sym.mOffset &= 0xFFFF;
						break;

					case kATAddressSpace_CB:
						if ((uint16)(sym.mOffset - 0xA000) >= 0x2000)
							sym.mOffset &= 0xFFFF;
						break;
				}
			}
		}

		for(ATSymbolStore::Directive& directive : directives) {
			directive.mOffset += bankOffsetMap[(directive.mOffset >> 16) & 0xFF];
		}

		for(auto& line : lines) {
			std::get<2>(line) += bankOffsetMap[(std::get<2>(line) >> 16) & 0xFF];
		}
	}

	for(const auto& line : lines)
		symstore.AddSourceLine(std::get<0>(line), std::get<1>(line), std::get<2>(line));

	symstore.AddSymbols(symbols);

	// remove useless directives
	while(!directives.empty() && directives.back().mOffset == 0)
		directives.pop_back();

	symstore.SetDirectives(directives);
}

void ATLoadSymbolsFromKernelListing(ATSymbolStore& symstore, VDTextStream& ifile) {
	// hardcoded for now for the kernel
	symstore.Init(0xD800, 0x2800);

	while(const char *line = ifile.GetNextLine()) {
		int len = (int)strlen(line);
		if (len < 33)
			continue;

		// What we're looking for:
		//    3587  F138  A9 00            ZERORM

		const char *s = line;
		const char *t;

		if (*s++ != ' ') continue;
		if (*s++ != ' ') continue;
		if (*s++ != ' ') continue;

		// skip line number
		while(*s == ' ')
			++s;
		if (!isdigit((unsigned char)*s++)) continue;
		while(isdigit((unsigned char)*s))
			++s;

		if (*s++ != ' ') continue;
		if (*s++ != ' ') continue;

		// read address
		uint32 address = 0;
		for(int i=0; i<4; ++i) {
			char c = *s;
			if (!isxdigit((unsigned char)c))
				goto fail;

			++s;
			c = toupper(c);
			if (c >= 'A')
				c -= 7;

			address = (address << 4) + (c - '0');
		}

		// skip two more spaces
		if (*s++ != ' ') continue;
		if (*s++ != ' ') continue;

		// check for first opcode byte
		if (!isxdigit((unsigned char)*s++)) continue;
		if (!isxdigit((unsigned char)*s++)) continue;

		// skip all the way to label
		s = line + 33;
		t = s;
		while(isalpha((unsigned char)*t))
			++t;

		if (t != s) {
			symstore.AddSymbol(address, VDStringA(s, (uint32)(t-s)).c_str());
		}

fail:
		;
	}
}

class ATELFNotSupportedException final : public MyError {
public:
	ATELFNotSupportedException() : MyError("The ELF file uses features not currently supported by this program.") {}
};

class ATELFUnexpectedEndException final : public MyError {
public:
	ATELFUnexpectedEndException() : MyError("Unexpected end of stream while reading from ELF file.") {}
};

static constexpr uint8 kATELF_EI_CLASS = 4;
static constexpr uint8 kATELF_EI_DATA = 5;
static constexpr uint8 kATELF_EI_VERSION = 6;

// EI_CLASS values
static constexpr uint8 kATELF_ELFCLASS32 = 1;

// EI_DATA values
static constexpr uint8 kATELF_ELFDATA2LSB = 1;
static constexpr uint8 kATELF_ELFDATA2MSB = 2;

// EI_VERSION values
static constexpr uint8 kATELF_EV_CURRENT = 1;

static constexpr uint16 kATELF_SHN_UNDEF = 0;

struct ATELFHeader32 {
	uint8	mIdent[16];		// identification
	uint16	mType;			// object file type
	uint16	mMachine;		// machine type
	uint32	mVersion;		// ELF version
	uint32	mEntry;			// entry point VA 
	uint32	mPHOff;			// program header file offset
	uint32	mSHOff;			// section header file offset
	uint32	mFlags;			// processor-specific flags
	uint16	mEHSize;		// ELF header size in bytes
	uint16	mPHEntSize;		// program header table entry size
	uint16	mPHNum;			// program header table entry count
	uint16	mSHEntSize;		// section header table entry size
	uint16	mSHNum;			// section header table entry count
	uint16	mSHStrNdx;		// section header table entry number for string table, or SHN_UNDEF

	void Decode(uint64 fileSize) {
		// ident[0:4] assumed to already have been checked as part of file detection
		if (mIdent[kATELF_EI_CLASS] != kATELF_ELFCLASS32)
			throw ATELFNotSupportedException();

		if (mIdent[kATELF_EI_DATA] != kATELF_ELFDATA2LSB && mIdent[kATELF_EI_DATA] != kATELF_ELFDATA2MSB)
			throw ATELFNotSupportedException();

		if (mIdent[kATELF_EI_VERSION] != kATELF_EV_CURRENT)
			throw ATELFNotSupportedException();

		// swizzle to native (which currently we assume is little endian)
		if (IsSwizzleNeeded())
			Swizzle();

		// validate that tables are aligned to 4, as both have 32-bit fields
		if ((mPHOff & 3) || (mSHOff & 3))
			throw ATELFNotSupportedException();

		// validate that tables are beyond the ELF header
		if (mPHOff < sizeof(*this) || mSHOff < sizeof(*this))
			throw ATELFNotSupportedException();

		// validate min table size
		if (mSHNum < 1)
			throw ATELFNotSupportedException();

		// validate min table entry size
		if (mPHEntSize < 32 || mSHEntSize < 40)
			throw ATELFNotSupportedException();

		// validate tables fit (off by one for max size is ok; MAX_UINT32 is not possible anyway)
		if (mPHNum > 0xFFFFFFFFU / mPHEntSize || fileSize - mPHOff < (uint32)mPHNum * mPHEntSize)
			throw ATELFNotSupportedException();

		if (mSHNum > 0xFFFFFFFFU / mSHEntSize || fileSize - mSHOff < (uint32)mSHNum * mSHEntSize)
			throw ATELFNotSupportedException();

		if (mSHStrNdx >= mSHNum)
			throw ATELFNotSupportedException();
	}

	void Swizzle() {
		mType		= VDSwizzleU16(mType		);
		mMachine	= VDSwizzleU16(mMachine		);
		mVersion	= VDSwizzleU32(mVersion		);
		mEntry		= VDSwizzleU32(mEntry		);
		mPHOff		= VDSwizzleU32(mPHOff		);
		mSHOff		= VDSwizzleU16(mSHOff		);
		mFlags		= VDSwizzleU16(mFlags		);
		mEHSize		= VDSwizzleU16(mEHSize		);
		mPHEntSize	= VDSwizzleU16(mPHEntSize	);
		mPHNum		= VDSwizzleU16(mPHNum		);
		mSHEntSize	= VDSwizzleU16(mSHEntSize	);
		mSHNum		= VDSwizzleU16(mSHNum		);
		mSHStrNdx	= VDSwizzleU16(mSHStrNdx	);
	}

	bool IsSwizzleNeeded() const {
		return mIdent[kATELF_EI_DATA] != (std::endian::native == std::endian::big ? kATELF_ELFDATA2MSB : kATELF_ELFDATA2LSB);
	}
};

static constexpr uint32 kATELF_SHT_NULL = 0;
static constexpr uint32 kATELF_SHT_SYMTAB = 2;
static constexpr uint32 kATELF_SHT_STRTAB = 3;
static constexpr uint32 kATELF_SHT_NOBITS = 8;

struct ATELFSectionHeader32 {
	uint32 mName;		// string table index
	uint32 mType;		// section type
	uint32 mFlags;		// section flags
	uint32 mAddr;		// virtual address
	uint32 mOffset;		// file offset
	uint32 mSize;		// size in bytes (unless SHT_NOBITS)
	uint32 mLink;		// index link
	uint32 mInfo;		// extra info
	uint32 mAddrAlign;	// address alignment
	uint32 mEntSize;	// fixed size entry size in bytes

	void Decode(const ATELFHeader32& hdr, const uint64 len, const ATELFSectionHeader32& strhdr) {
		if (hdr.IsSwizzleNeeded())
			Swizzle();

		if (mType != kATELF_SHT_NOBITS && mType != kATELF_SHT_NULL) {
			if (mOffset >= len || len - mOffset < mSize)
				throw ATELFNotSupportedException();
		}

		if (mName >= strhdr.mSize)
			throw ATELFNotSupportedException();
	}

	void Swizzle() {
		mName		= VDSwizzleU32(mName);
		mType		= VDSwizzleU32(mType);
		mFlags		= VDSwizzleU32(mFlags);
		mAddr		= VDSwizzleU32(mAddr);
		mOffset		= VDSwizzleU32(mOffset);
		mSize		= VDSwizzleU32(mSize);
		mLink		= VDSwizzleU32(mLink);
		mInfo		= VDSwizzleU32(mInfo);
		mAddrAlign	= VDSwizzleU32(mAddrAlign);
		mEntSize	= VDSwizzleU32(mEntSize);
	}
};

static_assert(sizeof(ATELFHeader32) == 52);
static_assert(sizeof(ATELFSectionHeader32) == 40);

static constexpr uint8 kATELF_DW_TAG_array_type = 0x01;
static constexpr uint8 kATELF_DW_TAG_class_type = 0x02;
static constexpr uint8 kATELF_DW_TAG_entry_point = 0x03;
static constexpr uint8 kATELF_DW_TAG_enumeration_type = 0x04;
static constexpr uint8 kATELF_DW_TAG_formal_parameter = 0x05;
static constexpr uint8 kATELF_DW_TAG_imported_declaration = 0x08;
static constexpr uint8 kATELF_DW_TAG_label = 0x0a;
static constexpr uint8 kATELF_DW_TAG_lexical_block = 0x0b;
static constexpr uint8 kATELF_DW_TAG_member = 0x0d;
static constexpr uint8 kATELF_DW_TAG_pointer_type = 0x0f;
static constexpr uint8 kATELF_DW_TAG_reference_type = 0x10;
static constexpr uint8 kATELF_DW_TAG_compile_unit = 0x11;
static constexpr uint8 kATELF_DW_TAG_string_type = 0x12;
static constexpr uint8 kATELF_DW_TAG_structure_type = 0x13;
static constexpr uint8 kATELF_DW_TAG_subroutine_type = 0x15;
static constexpr uint8 kATELF_DW_TAG_typedef = 0x16;
static constexpr uint8 kATELF_DW_TAG_union_type = 0x17;
static constexpr uint8 kATELF_DW_TAG_unspecified_parameters = 0x18;
static constexpr uint8 kATELF_DW_TAG_variant = 0x19;
static constexpr uint8 kATELF_DW_TAG_common_block = 0x1a;
static constexpr uint8 kATELF_DW_TAG_common_inclusion = 0x1b;
static constexpr uint8 kATELF_DW_TAG_inheritance = 0x1c;
static constexpr uint8 kATELF_DW_TAG_inlined_subroutine = 0x1d;
static constexpr uint8 kATELF_DW_TAG_module = 0x1e;
static constexpr uint8 kATELF_DW_TAG_ptr_to_member_type = 0x1f;
static constexpr uint8 kATELF_DW_TAG_set_type = 0x20;
static constexpr uint8 kATELF_DW_TAG_subrange_type = 0x21;
static constexpr uint8 kATELF_DW_TAG_with_stmt = 0x22;
static constexpr uint8 kATELF_DW_TAG_access_declaration = 0x23;
static constexpr uint8 kATELF_DW_TAG_base_type = 0x24;
static constexpr uint8 kATELF_DW_TAG_catch_block = 0x25;
static constexpr uint8 kATELF_DW_TAG_const_type = 0x26;
static constexpr uint8 kATELF_DW_TAG_constant = 0x27;
static constexpr uint8 kATELF_DW_TAG_enumerator = 0x28;
static constexpr uint8 kATELF_DW_TAG_file_type = 0x29;
static constexpr uint8 kATELF_DW_TAG_friend = 0x2a;
static constexpr uint8 kATELF_DW_TAG_namelist = 0x2b;
static constexpr uint8 kATELF_DW_TAG_namelist_item = 0x2c;
static constexpr uint8 kATELF_DW_TAG_packed_type = 0x2d;
static constexpr uint8 kATELF_DW_TAG_subprogram = 0x2e;
static constexpr uint8 kATELF_DW_TAG_template_type_parameter = 0x2f;
static constexpr uint8 kATELF_DW_TAG_template_value_parameter = 0x30;
static constexpr uint8 kATELF_DW_TAG_thrown_type = 0x31;
static constexpr uint8 kATELF_DW_TAG_try_block = 0x32;
static constexpr uint8 kATELF_DW_TAG_variant_part = 0x33;
static constexpr uint8 kATELF_DW_TAG_variable = 0x34;
static constexpr uint8 kATELF_DW_TAG_volatile_type = 0x35;
static constexpr uint8 kATELF_DW_TAG_dwarf_procedure = 0x36;
static constexpr uint8 kATELF_DW_TAG_restrict_type = 0x37;
static constexpr uint8 kATELF_DW_TAG_interface_type = 0x38;
static constexpr uint8 kATELF_DW_TAG_namespace = 0x39;
static constexpr uint8 kATELF_DW_TAG_imported_module = 0x3a;
static constexpr uint8 kATELF_DW_TAG_unspecified_type = 0x3b;
static constexpr uint8 kATELF_DW_TAG_partial_unit = 0x3c;
static constexpr uint8 kATELF_DW_TAG_imported_unit = 0x3d;
static constexpr uint8 kATELF_DW_TAG_condition = 0x3f;
static constexpr uint8 kATELF_DW_TAG_shared_type = 0x40;
static constexpr uint8 kATELF_DW_TAG_type_unit = 0x41;
static constexpr uint8 kATELF_DW_TAG_rvalue_reference_type = 0x42;
static constexpr uint8 kATELF_DW_TAG_template_alias = 0x43;
static constexpr uint8 kATELF_DW_TAG_coarray_type = 0x44;
static constexpr uint8 kATELF_DW_TAG_generic_subrange = 0x45;
static constexpr uint8 kATELF_DW_TAG_dynamic_type = 0x46;
static constexpr uint8 kATELF_DW_TAG_atomic_type = 0x47;
static constexpr uint8 kATELF_DW_TAG_call_site = 0x48;
static constexpr uint8 kATELF_DW_TAG_call_site_parameter = 0x49;
static constexpr uint8 kATELF_DW_TAG_skeleton_unit = 0x4a;
static constexpr uint8 kATELF_DW_TAG_immutable_type = 0x4b;
static constexpr uint32 kATELF_DW_TAG_lo_user = 0x4080;
static constexpr uint32 kATELF_DW_TAG_hi_user = 0xffff;

static constexpr uint8 kATELF_DW_AT_sibling = 0x01;
static constexpr uint8 kATELF_DW_AT_location = 0x02;
static constexpr uint8 kATELF_DW_AT_name = 0x03;
static constexpr uint8 kATELF_DW_AT_ordering = 0x09;
static constexpr uint8 kATELF_DW_AT_byte_size = 0x0b;
static constexpr uint8 kATELF_DW_AT_bit_size = 0x0d;
static constexpr uint8 kATELF_DW_AT_stmt_list = 0x10;
static constexpr uint8 kATELF_DW_AT_low_pc = 0x11;
static constexpr uint8 kATELF_DW_AT_high_pc = 0x12;
static constexpr uint8 kATELF_DW_AT_language = 0x13;
static constexpr uint8 kATELF_DW_AT_discr = 0x15;
static constexpr uint8 kATELF_DW_AT_discr_value = 0x16;
static constexpr uint8 kATELF_DW_AT_visibility = 0x17;
static constexpr uint8 kATELF_DW_AT_import = 0x18;
static constexpr uint8 kATELF_DW_AT_string_length = 0x19;
static constexpr uint8 kATELF_DW_AT_common_reference = 0x1a;
static constexpr uint8 kATELF_DW_AT_comp_dir = 0x1b;
static constexpr uint8 kATELF_DW_AT_const_value = 0x1c;
static constexpr uint8 kATELF_DW_AT_containing_type = 0x1d;
static constexpr uint8 kATELF_DW_AT_default_value = 0x1e;
static constexpr uint8 kATELF_DW_AT_inline = 0x20;
static constexpr uint8 kATELF_DW_AT_is_optional = 0x21;
static constexpr uint8 kATELF_DW_AT_lower_bound = 0x22;
static constexpr uint8 kATELF_DW_AT_producer = 0x25;
static constexpr uint8 kATELF_DW_AT_prototyped = 0x27;
static constexpr uint8 kATELF_DW_AT_return_addr = 0x2a;
static constexpr uint8 kATELF_DW_AT_start_scope = 0x2c;
static constexpr uint8 kATELF_DW_AT_bit_stride = 0x2e;
static constexpr uint8 kATELF_DW_AT_upper_bound = 0x2f;
static constexpr uint8 kATELF_DW_AT_abstract_origin = 0x31;
static constexpr uint8 kATELF_DW_AT_accessibility = 0x32;
static constexpr uint8 kATELF_DW_AT_address_class = 0x33;
static constexpr uint8 kATELF_DW_AT_artificial = 0x34;
static constexpr uint8 kATELF_DW_AT_base_types = 0x35;
static constexpr uint8 kATELF_DW_AT_calling_convention = 0x36;
static constexpr uint8 kATELF_DW_AT_count = 0x37;
static constexpr uint8 kATELF_DW_AT_data_member_location = 0x38;
static constexpr uint8 kATELF_DW_AT_decl_column = 0x39;
static constexpr uint8 kATELF_DW_AT_decl_file = 0x3a;
static constexpr uint8 kATELF_DW_AT_decl_line = 0x3b;
static constexpr uint8 kATELF_DW_AT_declaration = 0x3c;
static constexpr uint8 kATELF_DW_AT_discr_list = 0x3d;
static constexpr uint8 kATELF_DW_AT_encoding = 0x3e;
static constexpr uint8 kATELF_DW_AT_external = 0x3f;
static constexpr uint8 kATELF_DW_AT_frame_base = 0x40;
static constexpr uint8 kATELF_DW_AT_friend = 0x41;
static constexpr uint8 kATELF_DW_AT_identifier_case = 0x42;
static constexpr uint8 kATELF_DW_AT_namelist_item = 0x44;
static constexpr uint8 kATELF_DW_AT_priority = 0x45;
static constexpr uint8 kATELF_DW_AT_segment = 0x46;
static constexpr uint8 kATELF_DW_AT_specification = 0x47;
static constexpr uint8 kATELF_DW_AT_static_link = 0x48;
static constexpr uint8 kATELF_DW_AT_type = 0x49;
static constexpr uint8 kATELF_DW_AT_use_location = 0x4a;
static constexpr uint8 kATELF_DW_AT_variable_parameter = 0x4b;
static constexpr uint8 kATELF_DW_AT_virtuality = 0x4c;
static constexpr uint8 kATELF_DW_AT_vtable_elem_location = 0x4d;
static constexpr uint8 kATELF_DW_AT_allocated = 0x4e;
static constexpr uint8 kATELF_DW_AT_associated = 0x4f;
static constexpr uint8 kATELF_DW_AT_data_location = 0x50;
static constexpr uint8 kATELF_DW_AT_byte_stride = 0x51;
static constexpr uint8 kATELF_DW_AT_entry_pc = 0x52;
static constexpr uint8 kATELF_DW_AT_use_UTF8 = 0x53;
static constexpr uint8 kATELF_DW_AT_extension = 0x54;
static constexpr uint8 kATELF_DW_AT_ranges = 0x55;
static constexpr uint8 kATELF_DW_AT_trampoline = 0x56;
static constexpr uint8 kATELF_DW_AT_call_column = 0x57;
static constexpr uint8 kATELF_DW_AT_call_file = 0x58;
static constexpr uint8 kATELF_DW_AT_call_line = 0x59;
static constexpr uint8 kATELF_DW_AT_description = 0x5a;
static constexpr uint8 kATELF_DW_AT_binary_scale = 0x5b;
static constexpr uint8 kATELF_DW_AT_decimal_scale = 0x5c;
static constexpr uint8 kATELF_DW_AT_small = 0x5d;
static constexpr uint8 kATELF_DW_AT_decimal_sign = 0x5e;
static constexpr uint8 kATELF_DW_AT_digit_count = 0x5f;
static constexpr uint8 kATELF_DW_AT_picture_string = 0x60;
static constexpr uint8 kATELF_DW_AT_mutable = 0x61;
static constexpr uint8 kATELF_DW_AT_threads_scaled = 0x62;
static constexpr uint8 kATELF_DW_AT_explicit = 0x63;
static constexpr uint8 kATELF_DW_AT_object_pointer = 0x64;
static constexpr uint8 kATELF_DW_AT_endianity = 0x65;
static constexpr uint8 kATELF_DW_AT_elemental = 0x66;
static constexpr uint8 kATELF_DW_AT_pure = 0x67;
static constexpr uint8 kATELF_DW_AT_recursive = 0x68;
static constexpr uint8 kATELF_DW_AT_signature = 0x69;
static constexpr uint8 kATELF_DW_AT_main_subprogram = 0x6a;
static constexpr uint8 kATELF_DW_AT_data_bit_offset = 0x6b;
static constexpr uint8 kATELF_DW_AT_const_expr = 0x6c;
static constexpr uint8 kATELF_DW_AT_enum_class = 0x6d;
static constexpr uint8 kATELF_DW_AT_linkage_name = 0x6e;
static constexpr uint8 kATELF_DW_AT_string_length_bit_size = 0x6f;
static constexpr uint8 kATELF_DW_AT_string_length_byte_size = 0x70;
static constexpr uint8 kATELF_DW_AT_rank = 0x71;
static constexpr uint8 kATELF_DW_AT_str_offsets_base = 0x72;
static constexpr uint8 kATELF_DW_AT_addr_base = 0x73;
static constexpr uint8 kATELF_DW_AT_rnglists_base = 0x74;
static constexpr uint8 kATELF_DW_AT_dwo_name = 0x76;
static constexpr uint8 kATELF_DW_AT_reference = 0x77;
static constexpr uint8 kATELF_DW_AT_rvalue_reference = 0x78;
static constexpr uint8 kATELF_DW_AT_macros = 0x79;
static constexpr uint8 kATELF_DW_AT_call_all_calls = 0x7a;
static constexpr uint8 kATELF_DW_AT_call_all_source_calls = 0x7b;
static constexpr uint8 kATELF_DW_AT_call_all_tail_calls = 0x7c;
static constexpr uint8 kATELF_DW_AT_call_return_pc = 0x7d;
static constexpr uint8 kATELF_DW_AT_call_value = 0x7e;
static constexpr uint8 kATELF_DW_AT_call_origin = 0x7f;
static constexpr uint8 kATELF_DW_AT_call_parameter = 0x80;
static constexpr uint8 kATELF_DW_AT_call_pc = 0x81;
static constexpr uint8 kATELF_DW_AT_call_tail_call = 0x82;
static constexpr uint8 kATELF_DW_AT_call_target = 0x83;
static constexpr uint8 kATELF_DW_AT_call_target_clobbered = 0x84;
static constexpr uint8 kATELF_DW_AT_call_data_location = 0x85;
static constexpr uint8 kATELF_DW_AT_call_data_value = 0x86;
static constexpr uint8 kATELF_DW_AT_noreturn = 0x87;
static constexpr uint8 kATELF_DW_AT_alignment = 0x88;
static constexpr uint8 kATELF_DW_AT_export_symbols = 0x89;
static constexpr uint8 kATELF_DW_AT_deleted = 0x8a;
static constexpr uint8 kATELF_DW_AT_defaulted = 0x8b;
static constexpr uint8 kATELF_DW_AT_loclists_base = 0x8c;
static constexpr uint32 kATELF_DW_AT_lo_user = 0x2000;
static constexpr uint32 kATELF_DW_AT_hi_user = 0x3fff;

static constexpr uint8 kATELF_DW_FORM_addr = 0x01;
static constexpr uint8 kATELF_DW_FORM_block2 = 0x03;
static constexpr uint8 kATELF_DW_FORM_block4 = 0x04;
static constexpr uint8 kATELF_DW_FORM_data2 = 0x05;
static constexpr uint8 kATELF_DW_FORM_data4 = 0x06;
static constexpr uint8 kATELF_DW_FORM_data8 = 0x07;
static constexpr uint8 kATELF_DW_FORM_string = 0x08;
static constexpr uint8 kATELF_DW_FORM_block = 0x09;
static constexpr uint8 kATELF_DW_FORM_block1 = 0x0a;
static constexpr uint8 kATELF_DW_FORM_data1 = 0x0b;
static constexpr uint8 kATELF_DW_FORM_flag = 0x0c;
static constexpr uint8 kATELF_DW_FORM_sdata = 0x0d;
static constexpr uint8 kATELF_DW_FORM_strp = 0x0e;
static constexpr uint8 kATELF_DW_FORM_udata = 0x0f;
static constexpr uint8 kATELF_DW_FORM_ref_addr = 0x10;
static constexpr uint8 kATELF_DW_FORM_ref1 = 0x11;
static constexpr uint8 kATELF_DW_FORM_ref2 = 0x12;
static constexpr uint8 kATELF_DW_FORM_ref4 = 0x13;
static constexpr uint8 kATELF_DW_FORM_ref8 = 0x14;
static constexpr uint8 kATELF_DW_FORM_ref_udata = 0x15;
static constexpr uint8 kATELF_DW_FORM_indirect = 0x16;
static constexpr uint8 kATELF_DW_FORM_sec_offset = 0x17;
static constexpr uint8 kATELF_DW_FORM_exprloc = 0x18;
static constexpr uint8 kATELF_DW_FORM_flag_present = 0x19;
static constexpr uint8 kATELF_DW_FORM_strx = 0x1a;
static constexpr uint8 kATELF_DW_FORM_addrx = 0x1b;
static constexpr uint8 kATELF_DW_FORM_ref_sup4 = 0x1c;
static constexpr uint8 kATELF_DW_FORM_strp_sup = 0x1d;
static constexpr uint8 kATELF_DW_FORM_data16 = 0x1e;
static constexpr uint8 kATELF_DW_FORM_line_strp = 0x1f;
static constexpr uint8 kATELF_DW_FORM_ref_sig8 = 0x20;
static constexpr uint8 kATELF_DW_FORM_implicit_const = 0x21;
static constexpr uint8 kATELF_DW_FORM_loclistx = 0x22;
static constexpr uint8 kATELF_DW_FORM_rnglistx = 0x23;
static constexpr uint8 kATELF_DW_FORM_ref_sup8 = 0x24;
static constexpr uint8 kATELF_DW_FORM_strx1 = 0x25;
static constexpr uint8 kATELF_DW_FORM_strx2 = 0x26;
static constexpr uint8 kATELF_DW_FORM_strx3 = 0x27;
static constexpr uint8 kATELF_DW_FORM_strx4 = 0x28;
static constexpr uint8 kATELF_DW_FORM_addrx1 = 0x29;
static constexpr uint8 kATELF_DW_FORM_addrx2 = 0x2a;
static constexpr uint8 kATELF_DW_FORM_addrx3 = 0x2b;
static constexpr uint8 kATELF_DW_FORM_addrx4 = 0x2c;

static constexpr uint8 kATELF_DW_OP_addr = 0x03;
static constexpr uint8 kATELF_DW_OP_deref = 0x06;
static constexpr uint8 kATELF_DW_OP_const1u = 0x08;
static constexpr uint8 kATELF_DW_OP_const1s = 0x09;
static constexpr uint8 kATELF_DW_OP_const2u = 0x0a;
static constexpr uint8 kATELF_DW_OP_const2s = 0x0b;
static constexpr uint8 kATELF_DW_OP_const4u = 0x0c;
static constexpr uint8 kATELF_DW_OP_const4s = 0x0d;
static constexpr uint8 kATELF_DW_OP_const8u = 0x0e;
static constexpr uint8 kATELF_DW_OP_const8s = 0x0f;
static constexpr uint8 kATELF_DW_OP_constu = 0x10;
static constexpr uint8 kATELF_DW_OP_consts = 0x11;
static constexpr uint8 kATELF_DW_OP_dup = 0x12;
static constexpr uint8 kATELF_DW_OP_drop = 0x13;
static constexpr uint8 kATELF_DW_OP_over = 0x14;
static constexpr uint8 kATELF_DW_OP_pick = 0x15;
static constexpr uint8 kATELF_DW_OP_swap = 0x16;
static constexpr uint8 kATELF_DW_OP_rot = 0x17;
static constexpr uint8 kATELF_DW_OP_xderef = 0x18;
static constexpr uint8 kATELF_DW_OP_abs = 0x19;
static constexpr uint8 kATELF_DW_OP_and = 0x1a;
static constexpr uint8 kATELF_DW_OP_div = 0x1b;
static constexpr uint8 kATELF_DW_OP_minus = 0x1c;
static constexpr uint8 kATELF_DW_OP_mod = 0x1d;
static constexpr uint8 kATELF_DW_OP_mul = 0x1e;
static constexpr uint8 kATELF_DW_OP_neg = 0x1f;
static constexpr uint8 kATELF_DW_OP_not = 0x20;
static constexpr uint8 kATELF_DW_OP_or = 0x21;
static constexpr uint8 kATELF_DW_OP_plus = 0x22;
static constexpr uint8 kATELF_DW_OP_plus_uconst = 0x23;
static constexpr uint8 kATELF_DW_OP_shl = 0x24;
static constexpr uint8 kATELF_DW_OP_shr = 0x25;
static constexpr uint8 kATELF_DW_OP_shra = 0x26;
static constexpr uint8 kATELF_DW_OP_xor = 0x27;
static constexpr uint8 kATELF_DW_OP_bra = 0x28;
static constexpr uint8 kATELF_DW_OP_eq = 0x29;
static constexpr uint8 kATELF_DW_OP_ge = 0x2a;
static constexpr uint8 kATELF_DW_OP_gt = 0x2b;
static constexpr uint8 kATELF_DW_OP_le = 0x2c;
static constexpr uint8 kATELF_DW_OP_lt = 0x2d;
static constexpr uint8 kATELF_DW_OP_ne = 0x2e;
static constexpr uint8 kATELF_DW_OP_skip = 0x2f;
static constexpr uint8 kATELF_DW_OP_lit0 = 0x30;
static constexpr uint8 kATELF_DW_OP_lit1 = 0x31;
static constexpr uint8 kATELF_DW_OP_lit31 = 0x4f;
static constexpr uint8 kATELF_DW_OP_reg0 = 0x50;
static constexpr uint8 kATELF_DW_OP_reg1 = 0x51;
static constexpr uint8 kATELF_DW_OP_reg31 = 0x6f;
static constexpr uint8 kATELF_DW_OP_breg0 = 0x70;
static constexpr uint8 kATELF_DW_OP_breg1 = 0x71;
static constexpr uint8 kATELF_DW_OP_breg31 = 0x8f;
static constexpr uint8 kATELF_DW_OP_regx = 0x90;
static constexpr uint8 kATELF_DW_OP_fbreg = 0x91;
static constexpr uint8 kATELF_DW_OP_bregx = 0x92;
static constexpr uint8 kATELF_DW_OP_piece = 0x93;
static constexpr uint8 kATELF_DW_OP_deref_size = 0x94;
static constexpr uint8 kATELF_DW_OP_xderef_size = 0x95;
static constexpr uint8 kATELF_DW_OP_nop = 0x96;
static constexpr uint8 kATELF_DW_OP_push_object_address = 0x97;
static constexpr uint8 kATELF_DW_OP_call2 = 0x98;
static constexpr uint8 kATELF_DW_OP_call4 = 0x99;
static constexpr uint8 kATELF_DW_OP_call_ref = 0x9a;
static constexpr uint8 kATELF_DW_OP_form_tls_address = 0x9b;
static constexpr uint8 kATELF_DW_OP_call_frame_cfa = 0x9c;
static constexpr uint8 kATELF_DW_OP_bit_piece = 0x9d;
static constexpr uint8 kATELF_DW_OP_implicit_value = 0x9e;
static constexpr uint8 kATELF_DW_OP_stack_value = 0x9f;
static constexpr uint8 kATELF_DW_OP_implicit_pointer = 0xa0;
static constexpr uint8 kATELF_DW_OP_addrx = 0xa1;
static constexpr uint8 kATELF_DW_OP_constx = 0xa2;
static constexpr uint8 kATELF_DW_OP_entry_value = 0xa3;
static constexpr uint8 kATELF_DW_OP_const_type = 0xa4;
static constexpr uint8 kATELF_DW_OP_regval_type = 0xa5;
static constexpr uint8 kATELF_DW_OP_deref_type = 0xa6;
static constexpr uint8 kATELF_DW_OP_xderef_type = 0xa7;
static constexpr uint8 kATELF_DW_OP_convert = 0xa8;
static constexpr uint8 kATELF_DW_OP_reinterpret = 0xa9;
static constexpr uint8 kATELF_DW_OP_lo_user = 0xe0;
static constexpr uint8 kATELF_DW_OP_hi_user = 0xff;

static constexpr uint8 kATELF_DW_LLE_end_of_list = 0x00;
static constexpr uint8 kATELF_DW_LLE_base_addressx = 0x01;
static constexpr uint8 kATELF_DW_LLE_startx_endx = 0x02;
static constexpr uint8 kATELF_DW_LLE_startx_length = 0x03;
static constexpr uint8 kATELF_DW_LLE_offset_pair = 0x04;
static constexpr uint8 kATELF_DW_LLE_default_location = 0x05;
static constexpr uint8 kATELF_DW_LLE_base_address = 0x06;
static constexpr uint8 kATELF_DW_LLE_start_end = 0x07;
static constexpr uint8 kATELF_DW_LLE_start_length = 0x08;

static constexpr uint8 kATELF_DW_UT_compile = 0x01;
static constexpr uint8 kATELF_DW_UT_type = 0x02;
static constexpr uint8 kATELF_DW_UT_partial = 0x03;
static constexpr uint8 kATELF_DW_UT_skeleton = 0x04;
static constexpr uint8 kATELF_DW_UT_split_compile = 0x05;
static constexpr uint8 kATELF_DW_UT_split_type = 0x06;

static constexpr uint8 kATELF_DW_LNS_copy = 0x01;
static constexpr uint8 kATELF_DW_LNS_advance_pc = 0x02;
static constexpr uint8 kATELF_DW_LNS_advance_line = 0x03;
static constexpr uint8 kATELF_DW_LNS_set_file = 0x04;
static constexpr uint8 kATELF_DW_LNS_set_column = 0x05;
static constexpr uint8 kATELF_DW_LNS_negate_stmt = 0x06;
static constexpr uint8 kATELF_DW_LNS_set_basic_block = 0x07;
static constexpr uint8 kATELF_DW_LNS_const_add_pc = 0x08;
static constexpr uint8 kATELF_DW_LNS_fixed_advance_pc = 0x09;
static constexpr uint8 kATELF_DW_LNS_set_prologue_end = 0x0a;
static constexpr uint8 kATELF_DW_LNS_set_epilogue_begin = 0x0b;
static constexpr uint8 kATELF_DW_LNS_set_isa = 0x0c;

static constexpr uint8 kATELF_DW_LNE_end_sequence = 0x01;
static constexpr uint8 kATELF_DW_LNE_set_address = 0x02;
static constexpr uint8 kATELF_DW_LNE_set_discriminator = 0x04;
static constexpr uint8 kATELF_DW_LNE_lo_user = 0x80;
static constexpr uint8 kATELF_DW_LNE_hi_user = 0xff;

static constexpr uint8 kATELF_DW_LNCT_path = 0x1;
static constexpr uint8 kATELF_DW_LNCT_directory_index = 0x2;
static constexpr uint8 kATELF_DW_LNCT_timestamp = 0x3;
static constexpr uint8 kATELF_DW_LNCT_size = 0x4;
static constexpr uint8 kATELF_DW_LNCT_MD5 = 0x5;
static constexpr uint32 kATELF_DW_LNCT_lo_user = 0x2000;
static constexpr uint32 kATELF_DW_LNCT_hi_user = 0x3fff;

// Top-level chained structure in .debug_info sections.
struct ATELFDebugUnitHeader32 {
	static constexpr uint32 kRawSize = 7;

	uint32	mUnitLength;
	uint16	mVersion;
	uint8	mUnitType;

	void Decode(const ATELFHeader32& hdr) {
		if (hdr.IsSwizzleNeeded())
			Swizzle();
	}

	void Swizzle() {
		mUnitLength	= VDSwizzleU32(mUnitLength);
		mVersion	= VDSwizzleU16(mVersion);
	}
};

struct ATELFDebugCompilationUnitHeader32 {
	uint32	mUnitLength;
	uint16	mVersion;
	uint8	mUnitType;
	uint8	mAddressSize;
	uint32	mDebugAbbrevOffset;

	void Decode(const ATELFHeader32& hdr) {
		if (hdr.IsSwizzleNeeded())
			Swizzle();
	}

	void Swizzle() {
		mUnitLength			= VDSwizzleU32(mUnitLength);
		mVersion			= VDSwizzleU16(mVersion);
		mDebugAbbrevOffset	= VDSwizzleU32(mDebugAbbrevOffset);
	}
};

class ATELFStreamReader {
public:
	ATELFStreamReader(const void *p, size_t n, const ATELFHeader32& hdr)
		: mpSrc((const uint8 *)p + n)
		, mOffset(-(ptrdiff_t)n)
		, mInitOffset(mOffset)
		, mbSwizzle(hdr.IsSwizzleNeeded())
	{
	}

	uint8 ReadU8() {
		mOffset += 1;
		if (mOffset > 0)
			throw ATELFUnexpectedEndException();
		return mpSrc[mOffset - 1];
	}

	sint8 ReadS8() {
		return (sint8)ReadU8();
	}

	uint16 ReadU16() {
		mOffset += 2;
		if (mOffset > 0)
			throw ATELFUnexpectedEndException();
		uint16 v = VDReadUnalignedU16(&mpSrc[mOffset - 2]);
		return mbSwizzle ? VDSwizzleU16(v) : v;
	}

	sint16 ReadS16() {
		return (sint16)ReadU16();
	}

	uint32 ReadU24() {
		mOffset += 3;
		if (mOffset > 0)
			throw ATELFUnexpectedEndException();

		if (mbSwizzle) {
			return mpSrc[mOffset - 1]
				+ ((uint32)mpSrc[mOffset - 2] << 8)
				+ ((uint32)mpSrc[mOffset - 3] << 16);
		} else {
			return mpSrc[mOffset - 3]
				+ ((uint32)mpSrc[mOffset - 2] << 8)
				+ ((uint32)mpSrc[mOffset - 1] << 16);
		}
	}

	sint32 ReadS24() {
		return (sint32)((ReadU24() + UINT32_C(0xFF800000)) ^ UINT32_C(0xFF800000));
	}

	uint32 ReadU32() {
		mOffset += 4;
		if (mOffset > 0)
			throw ATELFUnexpectedEndException();
		uint32 v = VDReadUnalignedU32(&mpSrc[mOffset - 4]);
		return mbSwizzle ? VDSwizzleU32(v) : v;
	}

	sint32 ReadS32() {
		return (sint32)ReadU32();
	}

	sint32 ReadSLEB128() {
		uint32 v = ReadU8();

		if (v & 0x80) {
			v &= 0x7F;

			uint8 c = ReadU8();
			v += (c & 0x7F) << 7;

			if (c & 0x80) {
				c = ReadU8();
				v += (c & 0x7F) << 14;

				if (c & 0x80) {
					c = ReadU8();
					v += (c & 0x7F) << 21;

					if (c & 0x80) {
						c = ReadU8();
						v += (c & 0x0F) << 28;

						if (c & 0xF0) {
							if (c & 0x70)
								throw ATELFNotSupportedException();

							do {
								c = ReadU8();

								if (c & 0x7F)
									throw ATELFNotSupportedException();
							} while(c & 0x80);
						}
					} else {
						v += 0xF8000000;
						v ^= 0xF8000000;
					}
				} else {
					v += 0xFFF00000;
					v ^= 0xFFF00000;
				}
			} else {
				v += 0xFFFFE000;
				v ^= 0xFFFFE000;
			}
		} else {
			v += 0xFFFFFFC0;
			v ^= 0xFFFFFFC0;
		}

		return (sint32)v;
	}

	uint32 ReadULEB128() {
		uint32 v = ReadU8();

		if (v & 0x80) {
			v &= 0x7F;

			uint8 c = ReadU8();
			v += (c & 0x7F) << 7;

			if (c & 0x80) {
				c = ReadU8();
				v += (c & 0x7F) << 14;

				if (c & 0x80) {
					c = ReadU8();
					v += (c & 0x7F) << 21;

					if (c & 0x80) {
						c = ReadU8();
						v += (c & 0x0F) << 28;

						if (c & 0xF0) {
							if (c & 0x70)
								throw ATELFNotSupportedException();

							do {
								c = ReadU8();

								if (c & 0x7F)
									throw ATELFNotSupportedException();
							} while(c & 0x80);
						}
					}
				}
			}
		}

		return v;
	}

	void Skip(size_t n) {
		if (n > (size_t)-mOffset)
			throw ATELFUnexpectedEndException();

		mOffset += n;
	}

	bool IsAtEnd() const {
		return !mOffset;
	}

	uint32 GetPosition() const {
		return (uint32)(mOffset - mInitOffset);
	}

	const void *GetPositionPtr() const {
		return &mpSrc[mOffset];
	}

private:
	const uint8 *mpSrc;
	ptrdiff_t mOffset;
	ptrdiff_t mInitOffset;
	bool mbSwizzle;
};

struct ATELFAddress {
	bool mbRelative;
	uint32 mValue;

	static ATELFAddress Absolute(uint32 v) {
		return ATELFAddress{false, v};
	}

	static ATELFAddress Relative(uint32 v) {
		return ATELFAddress{true, v};
	}
};

class ATELFSymbolReader {
public:
	void Read(ATSymbolStore& symstore, IVDRandomAccessStream& stream);

private:
	static void SkipAttributes(ATELFStreamReader& attribReader, ATELFStreamReader& abbrevReader);
	static void SkipAttribute(ATELFStreamReader& reader, ATELFStreamReader *abbrevReader, uint32 form);
	const char *TryReadAttrString(ATELFStreamReader& reader, uint32 form);
	std::optional<ATELFAddress> TryReadAddress(ATELFStreamReader& reader, ATELFStreamReader& abbrevReader, uint32 form);
	std::optional<uint32> TryReadAttrU32(ATELFStreamReader& reader, ATELFStreamReader& abbrevReader, uint32 form);

	vdblock<uint8> mDebugInfo;
	vdblock<uint8> mDebugAbbrev;
	vdblock<uint32> mDebugAddrTable;
	vdblock<char> mDebugStringTable;
	vdblock<char> mDebugLineStringTable;
	vdblock<uint32> mDebugStringOffsetTable;
};

void ATELFSymbolReader::Read(ATSymbolStore& symstore, IVDRandomAccessStream& stream) {
	ATELFHeader32 hdr32;

	stream.Seek(0);
	stream.Read(&hdr32, sizeof hdr32);

	const uint64 len = (uint64)stream.Length();

	hdr32.Decode(len);

	// read in the string table
	ATELFSectionHeader32 shdrStrTab32;

	stream.Seek(hdr32.mSHOff + hdr32.mSHEntSize * hdr32.mSHStrNdx);
	stream.Read(&shdrStrTab32, sizeof shdrStrTab32);

	shdrStrTab32.Decode(hdr32, len, shdrStrTab32);

	if (shdrStrTab32.mSize > 50*1024*1024)
		throw ATELFNotSupportedException();

	vdblock<char> strtab(shdrStrTab32.mSize);
	stream.Seek(shdrStrTab32.mOffset);
	stream.Read(strtab.data(), shdrStrTab32.mSize);

	// validate string table termination
	if (strtab.empty() || strtab.front() != 0 || strtab.back() != 0)
		throw ATELFNotSupportedException();

	ATELFSectionHeader32 shdrDebugInfo;
	ATELFSectionHeader32 shdrDebugAbbrev;
	ATELFSectionHeader32 shdrDebugAddr;
	ATELFSectionHeader32 shdrDebugStr;
	ATELFSectionHeader32 shdrDebugStrOffsets;
	ATELFSectionHeader32 shdrDebugLine;
	ATELFSectionHeader32 shdrDebugLineStr;

	bool foundDebugInfo = false;
	bool foundDebugAbbrev = false;
	bool foundDebugAddr = false;
	bool foundDebugStr = false;
	bool foundDebugStrOffsets = false;
	bool foundDebugLine = false;
	bool foundDebugLineStr = false;

	stream.Seek(hdr32.mSHOff + hdr32.mSHEntSize);
	ATELFSectionHeader32 shdr32;
	for(uint32 i=1; i<hdr32.mSHNum; ++i) {
		stream.Read(&shdr32, sizeof shdr32);
		shdr32.Decode(hdr32, len, shdrStrTab32);

		const char *name = strtab.data() + shdr32.mName;

		if (!strcmp(name, ".debug_info")) {
			shdrDebugInfo = shdr32;
			foundDebugInfo = true;
		} else if (!strcmp(name, ".debug_abbrev")) {
			shdrDebugAbbrev = shdr32;
			foundDebugAbbrev = true;
		} else if (!strcmp(name, ".debug_addr")) {
			shdrDebugAddr = shdr32;
			foundDebugAddr = true;
		} else if (!strcmp(name, ".debug_str")) {
			shdrDebugStr = shdr32;
			foundDebugStr = true;
		} else if (!strcmp(name, ".debug_str_offsets")) {
			shdrDebugStrOffsets = shdr32;
			foundDebugStrOffsets = true;
		} else if (!strcmp(name, ".debug_line")) {
			shdrDebugLine = shdr32;
			foundDebugLine = true;
		} else if (!strcmp(name, ".debug_line_str")) {
			shdrDebugLineStr = shdr32;
			foundDebugLineStr = true;
		}
	}

	if (!foundDebugInfo || !foundDebugAbbrev)
		return;

	// sanity check sizes
	if (shdrDebugInfo.mSize < ATELFDebugUnitHeader32::kRawSize)
		throw ATELFNotSupportedException();

	if (shdrDebugInfo.mSize > 50*1024*1024 || shdrDebugAbbrev.mSize > 50*1024*1024)
		throw ATELFNotSupportedException();

	// read in debug info and debug abbrevation sections
	mDebugInfo.resize(shdrDebugInfo.mSize);
	stream.Seek(shdrDebugInfo.mOffset);
	stream.Read(mDebugInfo.data(), shdrDebugInfo.mSize);

	mDebugAbbrev.resize(shdrDebugAbbrev.mSize);
	stream.Seek(shdrDebugAbbrev.mOffset);
	stream.Read(mDebugAbbrev.data(), shdrDebugAbbrev.mSize);

	// read in debug address table, if one is present
	if (foundDebugAddr) {
		vdblock<uint8> debugAddr;
		debugAddr.resize(shdrDebugAddr.mSize);
		stream.Seek(shdrDebugAddr.mOffset);
		stream.Read(debugAddr.data(), shdrDebugAddr.mSize);

		struct ATELFDebugAddrHeader32 {
			uint32 mUnitLength;
			uint16 mVersion;
			uint8 mAddressSize;
			uint8 mSegmentSelectorSize;

			void Decode(const ATELFHeader32& hdr) {
				if (hdr.IsSwizzleNeeded())
					Swizzle();
			}

			void Swizzle() {
				mUnitLength = VDSwizzleU32(mUnitLength);
				mVersion = VDSwizzleU16(mVersion);
			}
		} debugAddrHdr;

		if (debugAddr.size() >= sizeof(ATELFDebugAddrHeader32)) {
			memcpy(&debugAddrHdr, debugAddr.data(), sizeof(ATELFDebugAddrHeader32));
			debugAddrHdr.Decode(hdr32);

			if (debugAddrHdr.mUnitLength > 4 && debugAddrHdr.mUnitLength <= debugAddr.size() - 4) {
				if (debugAddrHdr.mAddressSize == 2) {
					uint32 numAddrs = (debugAddrHdr.mUnitLength - 4) >> 1;
					
					mDebugAddrTable.resize(numAddrs);

					const uint8 *src16 = &debugAddr[8];
					if (hdr32.IsSwizzleNeeded()) {
						for(uint32 i=0; i<numAddrs; ++i) {
							mDebugAddrTable[i] = VDSwizzleU16(VDReadUnalignedU16(src16));
							src16 += 2;
						}
					} else {
						for(uint32 i=0; i<numAddrs; ++i) {
							mDebugAddrTable[i] = VDReadUnalignedU16(src16);
							src16 += 2;
						}
					}
				}
			}
		}
	}

	// read in debug string table
	if (foundDebugStr) {
		mDebugStringTable.resize(shdrDebugStr.mSize + 1);	// +1 for safety terminator
		stream.Seek(shdrDebugStr.mOffset);
		stream.Read(mDebugStringTable.data(), shdrDebugStr.mSize);
		
		// force a safety terminator at the end just in case
		mDebugStringTable.back() = 0;
	}
	
	// read in debug line string table
	if (foundDebugLineStr) {
		mDebugLineStringTable.resize(shdrDebugLineStr.mSize + 1);	// +1 for safety terminator
		stream.Seek(shdrDebugLineStr.mOffset);
		stream.Read(mDebugLineStringTable.data(), shdrDebugLineStr.mSize);
		
		// force a safety terminator at the end just in case
		mDebugLineStringTable.back() = 0;
	}

	// read in debug string offset table
	if (foundDebugStrOffsets && shdrDebugStrOffsets.mSize > 8) {
		struct ATELFDebugStrOffsetsHeader32 {
			uint32 mUnitLength;
			uint16 mVersion;
			uint16 mPad;

			void Decode(const ATELFHeader32& hdr) {
				if (hdr.IsSwizzleNeeded())
					Swizzle();
			}

			void Swizzle() {
				mUnitLength = VDSwizzleU32(mUnitLength);
				mVersion = VDSwizzleU16(mVersion);
			}
		} debugStrOffsetsHdr;

		static_assert(sizeof(ATELFDebugStrOffsetsHeader32) == 8);

		stream.Seek(shdrDebugStrOffsets.mOffset);
		stream.Read(&debugStrOffsetsHdr, sizeof debugStrOffsetsHdr);

		debugStrOffsetsHdr.Decode(hdr32);

		if (debugStrOffsetsHdr.mUnitLength >= 4 && debugStrOffsetsHdr.mUnitLength <= shdrDebugStrOffsets.mSize - 4) {
			const uint32 numStrs = (debugStrOffsetsHdr.mUnitLength - 4) >> 2;

			mDebugStringOffsetTable.resize(numStrs);
			stream.Read(mDebugStringOffsetTable.data(), numStrs * 4);

			if (hdr32.IsSwizzleNeeded()) {
				for(uint32& offset : mDebugStringOffsetTable)
					offset = VDSwizzleU32(offset);
			}

			uint32 offsetLimit = mDebugStringTable.size();
			for(const uint32& offset : mDebugStringOffsetTable) {
				if (offset >= offsetLimit)
					throw ATELFNotSupportedException();
			}
		}
	}

	// process units
	using AbbrevLookupTable = vdhashmap<uint32, uint32>;

	vdhashmap<uint32, AbbrevLookupTable> abbrevCache;

	uint32 unitOffset = 0;
	while(unitOffset + ATELFDebugUnitHeader32::kRawSize < shdrDebugInfo.mSize) {
		ATELFDebugUnitHeader32 unitHdr {};

		memcpy(&unitHdr, &mDebugInfo[unitOffset], ATELFDebugUnitHeader32::kRawSize);
		unitHdr.Decode(hdr32);

		if ((shdrDebugInfo.mSize - unitOffset) - 4 < unitHdr.mUnitLength)
			throw ATELFNotSupportedException();

		if (unitHdr.mUnitType == kATELF_DW_UT_compile && unitHdr.mUnitLength >= sizeof(ATELFDebugCompilationUnitHeader32)) {
			ATELFDebugCompilationUnitHeader32 cuHdr {};

			memcpy(&cuHdr, &mDebugInfo[unitOffset], sizeof cuHdr);

			cuHdr.Decode(hdr32);

			if (cuHdr.mDebugAbbrevOffset >= shdrDebugAbbrev.mSize)
				throw ATELFNotSupportedException();

			auto abbrevTableLookup = abbrevCache.insert(cuHdr.mDebugAbbrevOffset);
			if (abbrevTableLookup.second) {
				ATELFStreamReader abbrevReader(&mDebugAbbrev[cuHdr.mDebugAbbrevOffset], mDebugAbbrev.size() - cuHdr.mDebugAbbrevOffset, hdr32);

				for(;;) {
					const uint32 abbrCode = abbrevReader.ReadULEB128();

					if (!abbrCode)
						break;

					abbrevTableLookup.first->second.insert_as(abbrCode).first->second = abbrevReader.GetPosition() + cuHdr.mDebugAbbrevOffset;

					[[maybe_unused]] const uint32 tag = abbrevReader.ReadULEB128();
					[[maybe_unused]] const uint8 hasChildren = abbrevReader.ReadU8();

					for(;;) {
						const uint32 attr = abbrevReader.ReadULEB128();
						[[maybe_unused]] const uint32 form = abbrevReader.ReadULEB128();

						if (!attr)
							break;

						if (form == kATELF_DW_FORM_implicit_const)
							abbrevReader.ReadSLEB128();
					}
				}
			}

			const auto& abbrevTable = abbrevTableLookup.first->second;

			ATELFStreamReader cuReader(&mDebugInfo[unitOffset + 12], unitHdr.mUnitLength - 8, hdr32);
			int nestingLevel = 0;

			while(!cuReader.IsAtEnd()) {
				const uint32 attrCode = cuReader.ReadULEB128();

				if (!attrCode) {
					if (nestingLevel)
						--nestingLevel;

					continue;
				}

				const auto abbrevLookup = abbrevTable.find(attrCode);
				if (abbrevLookup == abbrevTable.end())
					throw ATELFNotSupportedException();

				const uint32 abbrevOffset = abbrevLookup->second;

				ATELFStreamReader abbrevReader2(&mDebugAbbrev[abbrevOffset], shdrDebugAbbrev.mSize - abbrevOffset, hdr32);
				const uint32 tag = abbrevReader2.ReadULEB128();
				const uint8 hasChildren = abbrevReader2.ReadU8();

				if (tag == kATELF_DW_TAG_subprogram) {
					const char *name = nullptr;
					std::optional<ATELFAddress> lowPC;
					std::optional<ATELFAddress> highPC;

					for(;;) {
						const uint32 attr = abbrevReader2.ReadULEB128();
						const uint32 form = abbrevReader2.ReadULEB128();

						if (!attr)
							break;

						switch(attr) {
							case kATELF_DW_AT_name:
								name = TryReadAttrString(cuReader, form);
								break;

							case kATELF_DW_AT_low_pc:
								lowPC = TryReadAddress(cuReader, abbrevReader2, form);
								break;

							case kATELF_DW_AT_high_pc:
								highPC = TryReadAddress(cuReader, abbrevReader2, form);
								break;

							default:
								SkipAttribute(cuReader, &abbrevReader2, form);
								break;
						}
					}

					if (name && lowPC.has_value() && highPC.has_value() && !lowPC->mbRelative && (highPC->mbRelative || highPC->mValue > lowPC->mValue))
						symstore.AddSymbol(lowPC->mValue, name, highPC->mbRelative ? highPC->mValue : highPC->mValue - lowPC->mValue);
				} else {
					SkipAttributes(cuReader, abbrevReader2);
				}

				if (hasChildren)
					++nestingLevel;
			}
		}

		unitOffset += 4 + unitHdr.mUnitLength;
	}

	// process debug line section
	if (foundDebugLine) {
		vdblock<char> debugLineBuf(shdrDebugLine.mSize);

		stream.Seek(shdrDebugLine.mOffset);
		stream.Read(debugLineBuf.data(), shdrDebugLine.mSize);

		ATELFStreamReader unitReader(debugLineBuf.data(), debugLineBuf.size(), hdr32);

		while(!unitReader.IsAtEnd()) {
			const uint32 unitLength = unitReader.ReadU32();
			const void *unitStart = unitReader.GetPositionPtr();

			unitReader.Skip(unitLength);

			ATELFStreamReader reader(unitStart, unitLength, hdr32);

			[[maybe_unused]]
			const uint16 version = reader.ReadU16();

			if (version != 5)
				throw ATELFNotSupportedException();

			[[maybe_unused]]
			const uint8 addressSize = reader.ReadU8();

			[[maybe_unused]]
			const uint8 segmentSelectorSize = reader.ReadU8();

			const uint32 headerLength = reader.ReadU32();

			const uint32 headerPos = reader.GetPosition();
			if (debugLineBuf.size() - headerPos < headerLength)
				throw ATELFNotSupportedException();

			const uint8 minInsnLength = reader.ReadU8();
			const uint8 maxInsnOps = reader.ReadU8();
			if (!maxInsnOps)
				throw ATELFNotSupportedException();

			[[maybe_unused]]
			const uint8 defaultIsStmt = reader.ReadU8();
			const sint8 lineBase = reader.ReadS8();
			const uint8 lineRange = reader.ReadU8();
			if (!lineRange)
				throw ATELFNotSupportedException();

			const uint8 opcodeBase = reader.ReadU8();
			if (!opcodeBase)
				throw ATELFNotSupportedException();

			const uint8 *standardOpcodeLengths = (const uint8 *)reader.GetPositionPtr();
			reader.Skip(opcodeBase - 1);

			// validate that the arg counts specified match what we expect from the spec
			static constexpr uint8 kStandardOpcodeArgCounts[] = {
				0,	// DW_LNS_copy
				1,	// DW_LNS_advance_pc
				1,	// DW_LNS_advance_line
				1,	// DW_LNS_set_file
				1,	// DW_LNS_set_column
				0,	// DW_LNS_negate_stmt
				0,	// DW_LNS_set_basic_block
				0,	// DW_LNS_const_add_pc
				1,	// DW_LNS_fixed_advance_pc
				0,	// DW_LNS_set_prologue_end
				0,	// DW_LNS_set_epilogue_begin
				1,	// DW_LNS_set_isa
			};

			if (opcodeBase > 1 && memcmp(standardOpcodeLengths, kStandardOpcodeArgCounts, std::min<size_t>(opcodeBase - 1, vdcountof(kStandardOpcodeArgCounts))))
				throw ATELFNotSupportedException();

			const uint8 dirEntFormatCount = reader.ReadU8();
			vdblock<std::pair<uint32, uint32>> dirFormat(dirEntFormatCount);
			for(uint32 i=0; i<dirEntFormatCount; ++i) {
				const uint32 type = reader.ReadULEB128();
				const uint32 form = reader.ReadULEB128();
				dirFormat[i] = std::pair<uint32, uint32>(type, form);

				if (form == kATELF_DW_FORM_implicit_const)
					reader.ReadSLEB128();
			}

			const uint32 dirCount = reader.ReadULEB128();
			for(uint32 i=0; i<dirCount; ++i) {
				for(const auto& formatEnt : dirFormat) {
					SkipAttribute(reader, nullptr, formatEnt.second);
				}
			}

			const uint8 fileFormatCount = reader.ReadU8();
			vdblock<std::pair<uint32, uint32>> fileFormat(fileFormatCount);
			for(uint32 i=0; i<fileFormatCount; ++i) {
				const uint32 type = reader.ReadULEB128();
				const uint32 form = reader.ReadULEB128();
				fileFormat[i] = std::pair<uint32, uint32>(type, form);

				if (form == kATELF_DW_FORM_implicit_const)
					reader.ReadSLEB128();
			}

			const uint32 fileNameCount = reader.ReadULEB128();
			vdfastvector<const char *> fileNames(fileNameCount, nullptr);
			for(uint32 i=0; i<fileNameCount; ++i) {
				for(const auto& formatEnt : fileFormat) {
					if (formatEnt.first == kATELF_DW_LNCT_path) {
						const char *path = TryReadAttrString(reader, formatEnt.second);

						fileNames[i] = path;
					} else
						SkipAttribute(reader, nullptr, formatEnt.second);
				}
			}

			vdfastvector<sint32> fileIdCache(fileNameCount, -1);

			const uint32 headerEnd = reader.GetPosition();
			if (headerLength < headerEnd - headerPos)
				throw ATELFNotSupportedException();

			ATELFStreamReader lpReader((const uint8 *)unitStart + headerPos + headerLength, unitLength - (headerPos + headerLength), hdr32);
	
			uint32 address = 0;
			uint32 file = 1;
			uint32 line = 1;
			uint32 column = 0;
			uint32 opIndex = 0;

			uint32 pendingAddress = 0;
			uint16 pendingFileId = 0;
			uint16 pendingLine = 0;

			while(!lpReader.IsAtEnd()) {
				const uint8 opcode = lpReader.ReadU8();
				bool addLineEntry = false;

				if (opcode) {	// standard/special opcode
					if (opcode < opcodeBase) {	// standard opcode
						switch(opcode) {
							case kATELF_DW_LNS_copy:
								addLineEntry = true;
								break;

							case kATELF_DW_LNS_advance_pc:
								{
									uint32 opAdvance = lpReader.ReadULEB128();

									address += minInsnLength * (opIndex + opAdvance) / maxInsnOps;
									opIndex = (opIndex + opAdvance) % maxInsnOps;
								}
								break;

							case kATELF_DW_LNS_advance_line:
								line += lpReader.ReadSLEB128();
								break;

							case kATELF_DW_LNS_set_file:
								file = lpReader.ReadULEB128();
								break;

							case kATELF_DW_LNS_set_column:
								column = lpReader.ReadULEB128();
								break;

							case kATELF_DW_LNS_negate_stmt:
								// flip is_stmt (currently ignored)
								break;

							case kATELF_DW_LNS_set_basic_block:
								// set basic_block (currently ignored)
								break;

							case kATELF_DW_LNS_const_add_pc:
								{
									// do special opcode 255
									const uint32 adjOpcode = 255 - opcodeBase;
									const uint32 opAdvance = adjOpcode / lineRange;

									address += minInsnLength * (opIndex + opAdvance) / maxInsnOps;
									opIndex = (opIndex + opAdvance) % maxInsnOps;
								}
								break;

							case kATELF_DW_LNS_fixed_advance_pc:
								address = lpReader.ReadU16();
								opIndex = 0;
								break;

							case kATELF_DW_LNS_set_prologue_end:
								// set prologue_end = true (ignored)
								break;

							case kATELF_DW_LNS_set_epilogue_begin:
								// set epilogue_begin = true (ignored)
								break;

							case kATELF_DW_LNS_set_isa:
								lpReader.ReadULEB128();
								// set isa (ignored)
								break;
						}
					} else {					// special opcode
						const uint32 adjOpcode = opcode - opcodeBase;
						line += (uint32)(sint32)lineBase + adjOpcode % lineRange;
						const uint32 opAdvance = adjOpcode / lineRange;

						address += minInsnLength * ((opIndex + opAdvance) / maxInsnOps);
						opIndex = (opIndex + opAdvance) % maxInsnOps;

						addLineEntry = true;
					}

					if (addLineEntry) {
						if (pendingFileId && address > pendingAddress)
							symstore.AddSourceLine(pendingFileId, pendingLine, pendingAddress, address - pendingAddress);

						pendingAddress = address;
						pendingLine = line;

						pendingFileId = 0;

						if (file < fileNameCount) {
							sint32& cachedFileId = fileIdCache[file];

							if (cachedFileId < 0) {
								if (fileNames[file])
									pendingFileId = symstore.AddFileName(VDTextU8ToW(VDStringSpanA(fileNames[file])).c_str());

								cachedFileId = pendingFileId;
							} else
								pendingFileId = (uint16)cachedFileId;
						}
					}
				} else {		// extended opcode
					const uint32 opSize = lpReader.ReadULEB128();
					const void *op = lpReader.GetPositionPtr();

					lpReader.Skip(opSize);

					ATELFStreamReader xopReader(op, opSize, hdr32);
					const uint8 xopCode = xopReader.ReadU8();

					if (xopCode == kATELF_DW_LNE_end_sequence) {
						if (pendingFileId) {
							if (address > pendingAddress)
								symstore.AddSourceLine(pendingFileId, pendingLine, pendingAddress, address - pendingAddress);

							pendingFileId = 0;
						}

						address = 0;
						file = 1;
						line = 1;
						column = 0;
						opIndex = 0;
					} else if (xopCode == kATELF_DW_LNE_set_address)
						address = xopReader.ReadU16();
				}
			}
		}
	}
}

void ATELFSymbolReader::SkipAttributes(ATELFStreamReader& attribReader, ATELFStreamReader& abbrevReader) {
	for(;;) {
		const uint32 attr = abbrevReader.ReadULEB128();
		const uint32 form = abbrevReader.ReadULEB128();

		if (!attr)
			break;

		SkipAttribute(attribReader, &abbrevReader, form);
	}
}

void ATELFSymbolReader::SkipAttribute(ATELFStreamReader& cuReader, ATELFStreamReader *abReader, uint32 form) {
	switch(form) {
		case kATELF_DW_FORM_addr:
			cuReader.ReadU16();
			break;

		case kATELF_DW_FORM_block1:
			cuReader.Skip(cuReader.ReadU8());
			break;

		case kATELF_DW_FORM_block2:
			cuReader.Skip(cuReader.ReadU16());
			break;

		case kATELF_DW_FORM_block4:
			cuReader.Skip(cuReader.ReadU32());
			break;

		case kATELF_DW_FORM_block:
			cuReader.Skip(cuReader.ReadULEB128());
			break;

		case kATELF_DW_FORM_data1:
			cuReader.ReadU8();
			break;

		case kATELF_DW_FORM_data2:
			cuReader.ReadU16();
			break;

		case kATELF_DW_FORM_data4:
			cuReader.ReadU32();
			break;

		case kATELF_DW_FORM_data8:
			cuReader.Skip(8);
			break;

		case kATELF_DW_FORM_data16:
			cuReader.Skip(16);
			break;

		case kATELF_DW_FORM_sdata:
			cuReader.ReadSLEB128();
			break;

		case kATELF_DW_FORM_udata:
			cuReader.ReadULEB128();
			break;

		// reference types
		case kATELF_DW_FORM_ref1:
			cuReader.ReadU8();
			break;

		case kATELF_DW_FORM_ref2:
			cuReader.ReadU16();
			break;

		case kATELF_DW_FORM_ref4:
			cuReader.ReadU32();
			break;

		case kATELF_DW_FORM_ref8:
			cuReader.Skip(8);
			break;

		case kATELF_DW_FORM_ref_udata:
			cuReader.ReadULEB128();
			break;

		case kATELF_DW_FORM_ref_addr:
			cuReader.ReadU32();
			break;

		case kATELF_DW_FORM_ref_sig8:
			cuReader.Skip(8);
			break;

		case kATELF_DW_FORM_ref_sup4:
			cuReader.ReadU32();
			break;

		case kATELF_DW_FORM_ref_sup8:
			cuReader.Skip(8);
			break;

		// string types
		case kATELF_DW_FORM_string:
			while(cuReader.ReadU8())
				;
			break;

		case kATELF_DW_FORM_strp:
			cuReader.ReadU32();
			break;

		case kATELF_DW_FORM_line_strp:
			cuReader.ReadU32();
			break;

		case kATELF_DW_FORM_strp_sup:
			cuReader.ReadU32();
			break;

		case kATELF_DW_FORM_strx1:
			cuReader.ReadU8();
			break;

		case kATELF_DW_FORM_strx2:
			cuReader.ReadU16();
			break;

		case kATELF_DW_FORM_strx3:
			cuReader.Skip(3);
			break;

		case kATELF_DW_FORM_strx4:
			cuReader.ReadU32();
			break;

		case kATELF_DW_FORM_strx:
			cuReader.ReadULEB128();
			break;

		// flag types
		case kATELF_DW_FORM_flag:
			cuReader.ReadU8();
			break;

		case kATELF_DW_FORM_flag_present:
			break;

		// lineptr/loclist/loclistsptr/macptr
		case kATELF_DW_FORM_sec_offset:
			cuReader.ReadU32();
			break;

		// loclist
		case kATELF_DW_FORM_loclistx:
			cuReader.ReadULEB128();
			break;

		case kATELF_DW_FORM_indirect:

		case kATELF_DW_FORM_exprloc:
			cuReader.Skip(cuReader.ReadULEB128());
			break;

		case kATELF_DW_FORM_addrx1:
			cuReader.ReadU8();
			break;

		case kATELF_DW_FORM_addrx2:
			cuReader.ReadU16();
			break;

		case kATELF_DW_FORM_addrx3:
			cuReader.Skip(3);
			break;

		case kATELF_DW_FORM_addrx4:
			cuReader.ReadU32();
			break;

		case kATELF_DW_FORM_addrx:
			cuReader.ReadULEB128();
			break;

		case kATELF_DW_FORM_implicit_const:
			if (abReader)
				abReader->ReadSLEB128();
			break;

		case kATELF_DW_FORM_rnglistx:
			cuReader.ReadULEB128();
			break;

		default:
			throw ATELFNotSupportedException();
	}
}

const char *ATELFSymbolReader::TryReadAttrString(ATELFStreamReader& reader, uint32 form) {
	uint32 stringIndex = 0;

	switch(form) {
		case kATELF_DW_FORM_string:
			{
				const char *s = (const char *)reader.GetPositionPtr();
				while(reader.ReadU8())
					;

				return s;
			}

		case kATELF_DW_FORM_strx1:
			stringIndex = reader.ReadU8();
			break;

		case kATELF_DW_FORM_strx2:
			stringIndex = reader.ReadU16();
			break;

		case kATELF_DW_FORM_strx3:
			stringIndex = reader.ReadU24();
			break;

		case kATELF_DW_FORM_strx4:
			stringIndex = reader.ReadU32();
			break;

		case kATELF_DW_FORM_strx:
			stringIndex = reader.ReadULEB128();
			break;

		case kATELF_DW_FORM_line_strp:
			{
				const uint32 offset = reader.ReadU32();

				if (offset >= mDebugLineStringTable.size())
					throw ATELFNotSupportedException();

				return &mDebugLineStringTable[offset];

			}
			break;

		default:
			SkipAttribute(reader, nullptr, form);
			return nullptr;
	}

	if (stringIndex >= mDebugStringOffsetTable.size())
		throw ATELFNotSupportedException();

	return &mDebugStringTable[mDebugStringOffsetTable[stringIndex]];
}

std::optional<ATELFAddress> ATELFSymbolReader::TryReadAddress(ATELFStreamReader& reader, ATELFStreamReader& abbrevReader, uint32 form) {
	std::optional<uint32> addrIndex;

	switch(form) {
		case kATELF_DW_FORM_addr:
			return ATELFAddress::Absolute(reader.ReadU16());

		case kATELF_DW_FORM_addrx1:
			addrIndex = reader.ReadU8();
			break;

		case kATELF_DW_FORM_addrx2:
			addrIndex = reader.ReadU16();
			break;

		case kATELF_DW_FORM_addrx3:
			addrIndex = reader.ReadU24();
			break;

		case kATELF_DW_FORM_addrx4:
			addrIndex = reader.ReadU32();
			break;

		case kATELF_DW_FORM_addrx:
			addrIndex = reader.ReadULEB128();
			break;

	}

	if (addrIndex.has_value()) {
		if (addrIndex.value() >= mDebugAddrTable.size())
			throw ATELFNotSupportedException();

		return ATELFAddress::Absolute(mDebugAddrTable[addrIndex.value()]);
	}

	auto v = TryReadAttrU32(reader, abbrevReader, form);

	return v.has_value() ? std::optional(ATELFAddress::Relative(v.value())) : std::nullopt;
}

std::optional<uint32> ATELFSymbolReader::TryReadAttrU32(ATELFStreamReader& reader, ATELFStreamReader& abbrevReader, uint32 form) {
	switch(form) {
		case kATELF_DW_FORM_data1:
			return reader.ReadU8();

		case kATELF_DW_FORM_data2:
			return reader.ReadU16();

		case kATELF_DW_FORM_data4:
			return reader.ReadU32();

		case kATELF_DW_FORM_udata:
			return reader.ReadULEB128();

		case kATELF_DW_FORM_implicit_const:
			return (uint32)reader.ReadSLEB128();

		default:
			SkipAttribute(reader, nullptr, form);
			return std::nullopt;
	}
}

void ATLoadSymbolsFromELF(ATSymbolStore& symstore, IVDRandomAccessStream& stream) {
	ATELFSymbolReader reader;
	reader.Read(symstore, stream);
}

///////////////////////////////////////////////////////////////////////////////

void ATLoadSymbols(const wchar_t *path, IATSymbolStore **outsymbols) {
	vdrefptr<ATSymbolStore> symbols(new ATSymbolStore);

	vdrefptr<ATVFSFileView> view;
	ATVFSOpenFileView(path, false, ~view);
	auto& fs = view->GetStream();

	ATLoadSymbols(*symbols, path, fs);

	*outsymbols = symbols.release();
}

void ATLoadSymbols(const wchar_t *filename, IVDRandomAccessStream& stream, IATSymbolStore **outsymbols) {
	vdrefptr<ATSymbolStore> symbols(new ATSymbolStore);
	
	ATLoadSymbols(*symbols, filename, stream);

	*outsymbols = symbols.release();
}

void ATSaveSymbols(const wchar_t *path, IATSymbolStore *syms) {
	VDFileStream fs(path, nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
	VDTextOutputStream tos(&fs);

	// write header
	tos.PutLine("Altirra symbol file");

	// write out symbols
	tos.PutLine();
	tos.PutLine("[symbols]");

	uint32 base = syms->GetDefaultBase();

	const uint32 n = syms->GetSymbolCount();
	for(uint32 i=0; i<n; ++i) {
		ATSymbolInfo sym;

		syms->GetSymbol(i, sym);

		tos.FormatLine("%s%s%s %04x,%x %s"
			, sym.mFlags & kATSymbol_Read ? "r" : ""
			, sym.mFlags & kATSymbol_Write ? "w" : ""
			, sym.mFlags & kATSymbol_Execute ? "x" : ""
			, base + sym.mOffset
			, sym.mLength
			, sym.mpName);
	}
}
