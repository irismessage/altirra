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
#include <vd2/system/vdstl.h>
#include "test.h"

template<class C>
static int RunVectorTest(bool checkCapacity) {
	C v;

	AT_TEST_ASSERT(v.empty());
	AT_TEST_ASSERT(v.size() == 0);
	AT_TEST_ASSERT(!checkCapacity || v.capacity() == 0);

	v.push_back(1);
	AT_TEST_ASSERT(v[0] == 1);
	AT_TEST_ASSERT(!v.empty());
	AT_TEST_ASSERT(v.size() == 1);

	v.push_back(2);
	AT_TEST_ASSERT(v[0] == 1);
	AT_TEST_ASSERT(v[1] == 2);
	AT_TEST_ASSERT(!v.empty());
	AT_TEST_ASSERT(v.size() == 2);
	AT_TEST_ASSERT(!checkCapacity || v.capacity() >= 2);

	{
		C v2;
		v.swap(v2);
	}
	AT_TEST_ASSERT(v.size() == 0);
	v.reserve(2);
	AT_TEST_ASSERT(v.size() == 0);
	AT_TEST_ASSERT(!checkCapacity || v.capacity() == 2);
	static const int k[2]={10,11};
	v.assign(k, k+2);
	v.insert(v.begin(), v[1]);
	AT_TEST_ASSERT(v[0] == 11);
	AT_TEST_ASSERT(v[1] == 10);
	AT_TEST_ASSERT(v[2] == 11);
	AT_TEST_ASSERT(v.size() == 3);
	AT_TEST_ASSERT(!checkCapacity || v.capacity() >= 3);

	// range erase
	v.clear();
	v.push_back(1);
	v.push_back(2);
	v.push_back(3);
	v.erase(v.begin(), v.end());
	AT_TEST_ASSERT(v.size() == 0);

	v.clear();
	v.push_back(1);
	v.push_back(2);
	v.push_back(3);
	v.erase(v.begin() + 1, v.begin() + 1);
	AT_TEST_ASSERT(v.size() == 3);
	AT_TEST_ASSERT(v[0] == 1 && v[1] == 2 && v[2] == 3);

	v.clear();
	v.push_back(1);
	v.push_back(2);
	v.push_back(3);
	v.erase(v.begin() + 1, v.begin() + 2);
	AT_TEST_ASSERT(v.size() == 2);
	AT_TEST_ASSERT(v[0] == 1 && v[1] == 3);

	v.clear();
	v.push_back(1);
	v.push_back(2);
	v.push_back(3);
	v.erase(v.begin() + 1, v.begin() + 3);
	AT_TEST_ASSERT(v.size() == 1);
	AT_TEST_ASSERT(v[0] == 1);

	v.clear();
	v.push_back(1);
	v.push_back(2);
	v.push_back(3);
	v.erase(v.begin() + 0, v.begin() + 2);
	AT_TEST_ASSERT(v.size() == 1);
	AT_TEST_ASSERT(v[0] == 3);

	return 0;
}

AT_DEFINE_TEST(System_FastVector) {
	int e = RunVectorTest<vdfastvector<int> >(true);
	return e;
}
