//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2023 Avery Lee
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
#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl_hashset.h>
#include <test.h>

DEFINE_TEST(System_HashMap) {
	vdhashmap<int, int> v;

	const auto checkSize = [](const auto& v, size_t n) {
		AT_TEST_ASSERT(v.size() == n);
		AT_TEST_ASSERT(v.empty() == (n == 0));
		AT_TEST_ASSERT(std::distance(v.begin(), v.end()) == n);
	};

	checkSize(v, 0);

	for(int i=0; i<10; ++i) {
		auto r = v.insert({i, i*10});
		AT_TEST_ASSERT(r.first->first == i);
		AT_TEST_ASSERT(r.first->second == i*10);
		AT_TEST_ASSERT(r.second);

		checkSize(v, i+1);

		auto r2 = v.insert({i, i*10});
		AT_TEST_ASSERT(r.first == r2.first);
		AT_TEST_ASSERT(!r2.second);

		checkSize(v, i+1);
	}

	auto v2(v);
	checkSize(v, 10);
	checkSize(v2, 10);

	v2 = std::move(v);
	checkSize(v, 0);
	checkSize(v2, 10);

	v = vdhashmap<int, int>(std::move(v2));
	checkSize(v, 10);
	checkSize(v2, 0);

	for(int i=0; i<10; ++i) {
		auto it = v.find(i);
		AT_TEST_ASSERT(it != v.end());
		AT_TEST_ASSERT(it->first == i);
		AT_TEST_ASSERT(it->second == i * 10);

		v.erase(it);
		AT_TEST_ASSERT(v.find(i) == v.end());
		
		checkSize(v, 9 - i);
	}

	return 0;
}
