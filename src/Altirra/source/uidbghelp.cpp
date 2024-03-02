//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2023 Avery Lee
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

#include "stdafx.h"
#include <vd2/system/binary.h>
#include <vd2/system/file.h>
#include <vd2/Dita/services.h>
#include "versioninfo.h"
#include "oshelper.h"
#include "resource.h"
#include "uiaccessors.h"

void ATUIExportDebugHelp() {
	const VDStringW& path = VDGetSaveFileName(
		VDMAKEFOURCC('d', 'b', 'g', 'h'),
		ATUIGetNewPopupOwner(),
		L"Export Debugger Help",
		L"Web page (*.html)",
		L"html");

	if (path.empty())
		return;

	vdfastvector<uint8> buf;
	vdfastvector<uint8> raw;
	if (!ATLoadMiscResource(IDR_DEBUG_HELP_TEMPLATE, buf))
		return;
	if (!ATLoadMiscResource(IDR_DEBUG_HELP, raw))
		return;

	vdfastvector<uint8> output;

	// Scan for special comment markers of the form:
	//	<!--[INSERT_TAG]-->
	const uint8 *src = buf.data();
	const uint8 *end = src + buf.size();

	while(src != end) {
		const uint8 *elementStart = nullptr;
		const uint8 *elementEnd = nullptr;
		VDStringSpanA tagName;
		
		const uint8 *src2 = src;
		while(src2 != end) {
			// skip the first character, since we'll be searching for !, and
			// it can't be first because < would need to be there
			++src2;
			if (src2 == end)
				break;

			// search for !, which will be a bit less frequent in HTML than
			// an angle bracket
			const uint8 *bang = (const uint8 *)memchr(src2, '!', end - src2);
			if (!bang)
				break;

			// check if we have enough left for this to be part of "<!--[" and
			// have "x]-->" after
			if (end - bang < 4 + 1 + 4 )
				break;

			// check for entry - we incremented above, so we know we have one
			// char back
			if (memcmp(bang - 1, "<!--[", 5)) {
				src2 = bang + 1;
				continue;
			}

			// mark tag start
			const uint8 *tagStart = bang + 4;

			// scan for "]-->" ending
			const uint8 *closingBracket = (const uint8 *)memchr(bang + 3, ']', std::min<size_t>(end - tagStart, 20));
			if (!closingBracket) {
				// bogus, reject
				src2 = tagStart;
				continue;
			}

			if (end - closingBracket < 4 || memcmp(closingBracket + 1, "-->", 3)) {
				// bogus, reject
				src2 = tagStart;
				continue;
			}

			// collect tag name and element pointers
			tagName = VDStringSpanA((const char *)tagStart, (const char *)closingBracket);
			elementStart = bang - 1;
			elementEnd = closingBracket + 4;
			break;
		}

		// append any accumulated text
		output.insert(output.end(), src, elementStart ? elementStart : end);

		// end if we didn't find an insertion element
		if (!elementStart)
			break;

		// process the insertion element by its tag
		if (tagName == "VERSION") {
			const auto& ver = AT_VERSION;

			output.insert(output.end(), (const uint8 *)&ver[0], (const uint8 *)&ver[0] + vdcountof(ver) - 1);
		} else if (tagName == "DBGHELP_CONTENT") {
			for(uint8 c : raw) {
				if (c == '&') {
					const auto& ampstr = "&amp;";
					output.insert(output.end(), (const uint8 *)ampstr, (const uint8 *)ampstr+5);
				} else if (c == '<') {
					const auto& ltstr = "&lt;";
					output.insert(output.end(), (const uint8 *)ltstr, (const uint8 *)ltstr+4);
				} else if (c == '>') {
					const auto& gtstr = "&gt;";
					output.insert(output.end(), (const uint8 *)gtstr, (const uint8 *)gtstr+4);
				} else
					output.push_back(c);
			}
		}

		// resume scanning after element
		src = elementEnd;
	}

	VDFile f(path.c_str(), nsVDFile::kCreateAlways | nsVDFile::kWrite);
	f.write(output.data(), output.size());
	f.close();
}
