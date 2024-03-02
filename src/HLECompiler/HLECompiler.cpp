//	Altirra - Atari 800/800XL emulator
//	HLE kernel compiler
//	Copyright (C) 2008-2009 Avery Lee
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
//	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#include "stdafx.h"

class HLECompiler {
public:
	HLECompiler();
	~HLECompiler();

	void Run(int argc, char **argv);

protected:
	void Parse(const wchar_t *fn);

	uint32	mNextHLEToken;

	vdautoptr<VDFileStream>			mpAsmFile;
	vdautoptr<VDTextOutputStream>	mpAsmOut;
	vdautoptr<VDFileStream>			mpCppFile;
	vdautoptr<VDTextOutputStream>	mpCppOut;
	vdautoptr<VDFileStream>			mpHeaderFile;
	vdautoptr<VDTextOutputStream>	mpHeaderOut;
};

HLECompiler::HLECompiler() {
}

HLECompiler::~HLECompiler() {
}

void HLECompiler::Run(int argc, char **argv) {
	if (argc < 5)
		throw MyError("Usage: hlecompiler source-file asm-output cpp-output h-output");

	const char *srcpath = argv[1];
	const char *asmpath = argv[2];
	const char *cpppath = argv[3];
	const char *hpath = argv[4];

	mNextHLEToken = 0x0000;

	mpAsmFile = new VDFileStream(VDTextAToW(asmpath).c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
	mpAsmOut = new VDTextOutputStream(mpAsmFile);
	mpCppFile = new VDFileStream(VDTextAToW(cpppath).c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
	mpCppOut = new VDTextOutputStream(mpCppFile);
	mpHeaderFile = new VDFileStream(VDTextAToW(hpath).c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
	mpHeaderOut = new VDTextOutputStream(mpHeaderFile);

	Parse(VDTextAToW(srcpath).c_str());

	mpCppOut->PutLine("void ATHLEKernel::Dispatch(uint16 id) {");
	mpCppOut->PutLine("\tswitch(id) {");
	for(uint32 i=0; i < mNextHLEToken; ++i)
		mpCppOut->FormatLine("\t\tcase 0x%04X: Callback_%04X(); break;", i, i);
	mpCppOut->PutLine("\t};");
	mpCppOut->PutLine("}");

	mpHeaderOut->PutLine("void Dispatch(uint16 id);");
	mpHeaderOut->PutLine();

	for(uint32 i=0; i<mNextHLEToken; ++i)
		mpHeaderOut->FormatLine("void Callback_%04X();", i);

	mpHeaderOut->Flush();
	mpHeaderOut = NULL;
	mpCppOut->Flush();
	mpCppOut = NULL;
	mpCppFile->close();
	mpCppFile = NULL;
	mpAsmOut->Flush();
	mpAsmOut = NULL;
	mpAsmFile->close();
	mpAsmFile = NULL;
}

void HLECompiler::Parse(const wchar_t *fn) {
	VDTextInputFile ifile(fn);
	static const char kWhitespace[]=" \t\v\r\n";

	const VDStringW fn1(VDGetFullPath(fn));
	VDStringW fn2;

	for(const wchar_t *u = fn1.c_str(); *u; ++u) {
		const wchar_t c = *u;

		if (c == '\\')
			fn2 += '\\';

		fn2 += c;
	}

	int line = 0;
	try {
		bool asmMode = true;
		bool protoIsNext = false;

		while(const char *s = ifile.GetNextLine()) {
			const char *t = s;

			++line;

			while(*t && strchr(kWhitespace, *t))
				++t;

			if (*t == '#') {
				++t;

				while(*t && strchr(kWhitespace, *t))
					++t;

				if (*t == '{') {
					++t;

					if (!asmMode)
						throw MyError("Already in C++ output mode");

					asmMode = false;
					protoIsNext = false;

					mpAsmOut->FormatLine("\tdta $42,a($%04X)", mNextHLEToken);

					mpCppOut->PutLine();
					mpCppOut->FormatLine("void ATHLEKernel::Callback_%04X() {", mNextHLEToken);
					mpCppOut->PutLine("\tusing namespace ATKernelSymbols;");
					mpCppOut->PutLine("\tATKernelDatabase kdb(mpMemory);");

					mpCppOut->FormatLine("#line %d \"%ls\"", line + 1, fn2.c_str());
				} else if (*t == '}') {
					++t;

					if (asmMode)
						throw MyError("Not in C++ output mode");

					asmMode = true;

					mpCppOut->PutLine("}");
					mpCppOut->PutLine();

					++mNextHLEToken;
				} else if (*t == '<') {
					++t;
					if (!asmMode)
						throw MyError("Already in C++ output mode");

					asmMode = false;
					protoIsNext = true;
				} else if (*t == '>') {
					++t;
					if (asmMode)
						throw MyError("Not in C++ output mode");

					asmMode = true;
				} else if (*t == '[') {
					++t;
					if (!asmMode)
						throw MyError("Already in C++ output mode");

					asmMode = false;
					protoIsNext = false;

					mpCppOut->PutLine("namespace {");
					mpCppOut->FormatLine("#line %d \"%ls\"", line + 1, fn2.c_str());					
				} else if (*t == ']') {
					++t;
					if (asmMode)
						throw MyError("Not in C++ output mode");

					asmMode = true;

					mpCppOut->PutLine("}");
				} else if (!strncmp(t, "include", 7)) {
					t += 7;

					bool valid = false;

					while(*t && strchr(kWhitespace, *t)) {
						valid = true;
						++t;
					}

					const char startChar = *t++;
					const char *startPos = t;
					if (startChar != '<' && startChar != '"')
						valid = false;
					else {
						while(*t && *t != '>' && *t != '"')
							++t;

						const char endChar = *t;
						if (!(startChar == '<' && endChar == '>') && !(startChar == '"' && endChar == '"'))
							valid = false;
					}

					if (!valid)
						throw MyError("Invalid #include directive");

					const wchar_t *pathEnd = VDFileSplitPath(fn);
					const VDStringW dir(fn, pathEnd);
					const VDStringA name(startPos, t);
					const VDStringW incpath(VDMakePath(dir.c_str(), VDTextAToW(name).c_str()));

					Parse(incpath.c_str());

					++t;
				} else {
					throw MyError("Unknown directive");
				}

				while(*t && strchr(kWhitespace, *t))
					++t;

				if (*t)
					throw MyError("Expected end of directive");

				continue;
			}

			if (asmMode)
				mpAsmOut->PutLine(s);
			else {
				if (protoIsNext) {
					protoIsNext = false;

					const char *paren = strchr(s, '(');
					bool ok = false;
					if (paren) {
						const char *name = paren;
						while(name > s) {
							const int c = (unsigned char)name[-1];
							if (!isalnum(c) && c != '_')
								break;

							--name;
						}

						const char *brace = strchr(paren, '{');
						if (brace) {
							mpCppOut->FormatLine("%.*s ATHLEKernel::%s", name-s, s, name);
							mpHeaderOut->FormatLine("%.*s;", brace-s, s);
							mpCppOut->PutLine("\tusing namespace ATKernelSymbols;");
							mpCppOut->PutLine("\tATKernelDatabase kdb(mpMemory);");
							ok = true;
						}
					}

					if (!ok)
						throw MyError("Unable to parse method declaration");

					mpCppOut->FormatLine("#line %d \"%ls\"", line+1, fn2.c_str());
				} else {
					mpCppOut->PutLine(s);
				}
			}
		}

		if (!asmMode)
			throw MyError("Cannot end source file in C++ mode");

	} catch(const MyError& e) {
		if (e.gets()[0] == '!')
			throw;

		throw MyError("!%ls(%d): Error! %s", fn, line, e.gets());
	}
}

int main(int argc, char **argv) {

	try {
		HLECompiler compiler;
		compiler.Run(argc, argv);
	} catch(const MyError& e) {
		const char *s = e.gets();

		if (*s == '!')
			++s;

		puts(s);
		return 5;
	}
	return 0;
}
