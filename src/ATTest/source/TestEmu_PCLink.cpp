//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2022 Avery Lee
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
#include <variant>
#include <at/atcore/cio.h>
#include "test.h"
#include "pclink.h"

class ATPCLinkDeviceTest {
public:
	using Result = std::variant<uint8, VDStringA>;

	static Result ResolvePath(const char *curDir, const char *path);

	static void RunTest();
};

ATPCLinkDeviceTest::Result ATPCLinkDeviceTest::ResolvePath(const char *curDir, const char *path) {
	VDStringA resolvedPath;
	uint8 code = ATPCLinkDevice::ResolvePathStatic(path, curDir, resolvedPath);

	if (code == kATCIOStat_Success)
		return Result(std::move(resolvedPath));
	else
		return Result(code);
}

void ATPCLinkDeviceTest::RunTest() {
	const auto ResultPath = [](const char *s) { return Result(VDString(s)); };
	const auto ResultError = [](auto err) { return Result((uint8)err); };

	TEST_ASSERT(ResolvePath("", "") == ResultPath(""));
	TEST_ASSERT(ResolvePath("", "FOO") == ResultPath("\\FOO"));
	TEST_ASSERT(ResolvePath("", "FOO.BAR") == ResultPath("\\FOO.BAR"));
	TEST_ASSERT(ResolvePath("", "FOO.") == ResultPath("\\FOO"));
	TEST_ASSERT(ResolvePath("", ".FOO") == ResultPath("\\.FOO"));
	TEST_ASSERT(ResolvePath("", ".FOOF") == ResultError(kATCIOStat_FileNameErr));
	TEST_ASSERT(ResolvePath("", "FOOBARBA.Z") == ResultPath("\\FOOBARBA.Z"));
	TEST_ASSERT(ResolvePath("", "FOOBARBAZ") == ResultError(kATCIOStat_FileNameErr));
	TEST_ASSERT(ResolvePath("", "FOOBARBAZ.FOO") == ResultError(kATCIOStat_FileNameErr));
	TEST_ASSERT(ResolvePath("", ".") == ResultError(kATCIOStat_FileNameErr));
	TEST_ASSERT(ResolvePath("", "foo") == ResultPath("\\FOO"));
	TEST_ASSERT(ResolvePath("", "FOO\\BAR") == ResultPath("\\FOO\\BAR"));
	TEST_ASSERT(ResolvePath("", "FOO\\\\BAR") == ResultError(kATCIOStat_FileNameErr));
	TEST_ASSERT(ResolvePath("", "FOO>BAR") == ResultPath("\\FOO\\BAR"));
	TEST_ASSERT(ResolvePath("\\FOO", "BAR") == ResultPath("\\FOO\\BAR"));
	TEST_ASSERT(ResolvePath("\\FOO", "\\BAR") == ResultPath("\\BAR"));
	TEST_ASSERT(ResolvePath("\\FOO", "\\BAR\\") == ResultPath("\\BAR"));
	TEST_ASSERT(ResolvePath("\\FOO", "<") == ResultPath(""));
	TEST_ASSERT(ResolvePath("\\FOO", "<BAR") == ResultPath("\\BAR"));
	TEST_ASSERT(ResolvePath("\\FOO\\BAR", "<BAZ") == ResultPath("\\FOO\\BAZ"));
	TEST_ASSERT(ResolvePath("\\FOO\\BAR", "..\\<BAZ") == ResultPath("\\BAZ"));
	TEST_ASSERT(ResolvePath("\\FOO\\BAR", "<BAZ>") == ResultPath("\\FOO\\BAZ"));
	TEST_ASSERT(ResolvePath("\\FOO", "..") == ResultPath(""));
	TEST_ASSERT(ResolvePath("\\FOO", "..\\") == ResultPath(""));

	// SDX does not allow upwards traversal from the root; the .. entry simply
	// does not exist in MAIN.
	TEST_ASSERT(ResolvePath("", "..") == Result((uint8)kATCIOStat_PathNotFound));
	TEST_ASSERT(ResolvePath("", "<") == Result((uint8)kATCIOStat_PathNotFound));
}

DEFINE_TEST(Emu_PCLink) {
	ATPCLinkDeviceTest::RunTest();
	return 0;
}
