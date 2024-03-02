//	Asuka - VirtualDub Build/Post-Mortem Utility
//	Copyright (C) 2005 Avery Lee
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
#include <vector>
#include <list>
#include <string>
#include <d3d11.h>
#include <d3d10.h>
#include <d3dcompiler.h>
#include <vd2/system/refcount.h>
#include <vd2/system/file.h>
#include <vd2/system/vdstl.h>

#pragma comment(lib, "D3DCompiler")

void tool_fxc10(const vdfastvector<const char *>& args, const vdfastvector<const char *>& switches, bool amd64) {
	if (args.size() != 5) {
		puts("usage: asuka fxc10 source.fx entrypoint profile target.inl symbol");
		exit(5);
	}

	const char *filename = args[0];
	const char *entrypoint = args[1];
	const char *profile = args[2];
	const char *outfilename = args[3];
	const char *symbol = args[4];

	printf("Asuka: Compiling effect file (Direct3D 10 %s): %s[%s] -> %s.\n", profile, filename, entrypoint, outfilename);

	VDFile f(filename);
	vdfastvector<uint8> srcData((uint32)f.size());
	f.read(srcData.data(), srcData.size());
	f.close();

	vdrefptr<ID3D10Blob> blob;
	vdrefptr<ID3D10Blob> errors;
	HRESULT hr = D3DCompile(srcData.data(), srcData.size(), filename, NULL, NULL, entrypoint, profile, 0, 0, ~blob, ~errors);

	if (FAILED(hr)) {
		printf("Effect compilation failed for \"%s\" (%08x)\n", filename, hr);

		if (errors)
			puts((const char *)errors->GetBufferPointer());

		exit(10);
	}

	errors.clear();

	FILE *fo = fopen(outfilename, "w");
	if (!fo) {
		printf("Couldn't open %s for write\n", outfilename);
		exit(10);
	}

	fprintf(fo, "static const uint8 %s[]={\n", symbol);
	
	const uint8 *src = (const uint8 *)blob->GetBufferPointer();
	const size_t len = blob->GetBufferSize();
	for(size_t i=0; i<len; i+=16) {
		fprintf(fo, "\t");
		for(size_t j=i; j<i+16 && j<len; ++j) {
			fprintf(fo, "0x%02x,", src[j]);
		}
		fprintf(fo, "\n");
	}
	fprintf(fo, "};\n");
	fclose(fo);

	printf("Asuka: Compilation was successful.\n");
}
