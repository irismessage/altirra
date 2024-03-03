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
#include <vd2/system/VDString.h>
#include "test.h"

AT_DEFINE_TEST(System_Vector) {
	vdvector<VDStringA> x;
	const vdvector<VDStringA>& xc = x;

	x.push_back(VDStringA("abc"));
	x.push_back(VDStringA("def"));
	x.push_back(VDStringA("ghi"));
	AT_TEST_ASSERT(x[0] == "abc");
	AT_TEST_ASSERT(x[1] == "def");
	AT_TEST_ASSERT(x[2] == "ghi");
	AT_TEST_ASSERT(x.at(0) == "abc");
	AT_TEST_ASSERT(x.at(1) == "def");
	AT_TEST_ASSERT(x.at(2) == "ghi");
	AT_TEST_ASSERT(x.front() == "abc");
	AT_TEST_ASSERT(x.back() == "ghi");
	AT_TEST_ASSERT(x.size() == 3);
	AT_TEST_ASSERT(x.capacity() >= 3);
	AT_TEST_ASSERT(!x.empty());

	AT_TEST_ASSERT(xc[0] == "abc");
	AT_TEST_ASSERT(xc[1] == "def");
	AT_TEST_ASSERT(xc[2] == "ghi");
	AT_TEST_ASSERT(xc.at(0) == "abc");
	AT_TEST_ASSERT(xc.at(1) == "def");
	AT_TEST_ASSERT(xc.at(2) == "ghi");
	AT_TEST_ASSERT(xc.front() == "abc");
	AT_TEST_ASSERT(xc.back() == "ghi");
	AT_TEST_ASSERT(xc.size() == 3);
	AT_TEST_ASSERT(xc.capacity() >= 3);
	AT_TEST_ASSERT(!xc.empty());

	x.insert(x.begin() + 1, VDStringA("foo1"));
	x.insert(x.begin() + 2, VDStringA("foo2"));
	x.insert(x.begin() + 3, VDStringA("foo3"));
	x.insert(x.begin() + 4, VDStringA("foo4"));
	AT_TEST_ASSERT(x[0] == "abc");
	AT_TEST_ASSERT(x[1] == "foo1");
	AT_TEST_ASSERT(x[2] == "foo2");
	AT_TEST_ASSERT(x[3] == "foo3");
	AT_TEST_ASSERT(x[4] == "foo4");
	AT_TEST_ASSERT(x[5] == "def");
	AT_TEST_ASSERT(x[6] == "ghi");
	AT_TEST_ASSERT(x.front() == "abc");
	AT_TEST_ASSERT(x.back() == "ghi");
	AT_TEST_ASSERT(x.size() == 7);
	AT_TEST_ASSERT(x.capacity() >= 7);
	AT_TEST_ASSERT(!x.empty());

	VDStringA ins[4]={
		VDStringA("ins1"),
		VDStringA("ins2"),
		VDStringA("ins3"),
		VDStringA("ins4"),
	};

	x.insert(x.end(), ins, ins+4);
	AT_TEST_ASSERT(x[0] == "abc");
	AT_TEST_ASSERT(x[1] == "foo1");
	AT_TEST_ASSERT(x[2] == "foo2");
	AT_TEST_ASSERT(x[3] == "foo3");
	AT_TEST_ASSERT(x[4] == "foo4");
	AT_TEST_ASSERT(x[5] == "def");
	AT_TEST_ASSERT(x[6] == "ghi");
	AT_TEST_ASSERT(x[7] == "ins1");
	AT_TEST_ASSERT(x[8] == "ins2");
	AT_TEST_ASSERT(x[9] == "ins3");
	AT_TEST_ASSERT(x[10] == "ins4");
	AT_TEST_ASSERT(x.front() == "abc");
	AT_TEST_ASSERT(x.back() == "ins4");
	AT_TEST_ASSERT(x.size() == 11);
	AT_TEST_ASSERT(x.capacity() >= 11);
	AT_TEST_ASSERT(!x.empty());

	x.insert(x.begin(), 2, VDStringA("x"));
	AT_TEST_ASSERT(x[0] == "x");
	AT_TEST_ASSERT(x[1] == "x");
	AT_TEST_ASSERT(x[2] == "abc");
	AT_TEST_ASSERT(x[3] == "foo1");
	AT_TEST_ASSERT(x[4] == "foo2");
	AT_TEST_ASSERT(x[5] == "foo3");
	AT_TEST_ASSERT(x[6] == "foo4");
	AT_TEST_ASSERT(x[7] == "def");
	AT_TEST_ASSERT(x[8] == "ghi");
	AT_TEST_ASSERT(x[9] == "ins1");
	AT_TEST_ASSERT(x[10] == "ins2");
	AT_TEST_ASSERT(x[11] == "ins3");
	AT_TEST_ASSERT(x[12] == "ins4");
	AT_TEST_ASSERT(x.front() == "x");
	AT_TEST_ASSERT(x.back() == "ins4");
	AT_TEST_ASSERT(x.size() == 13);
	AT_TEST_ASSERT(x.capacity() >= 13);
	AT_TEST_ASSERT(!x.empty());

	x.resize(6);
	AT_TEST_ASSERT(x.front() == "x");
	AT_TEST_ASSERT(x.back() == "foo3");
	AT_TEST_ASSERT(x.size() == 6);
	AT_TEST_ASSERT(x.capacity() >= 6);
	AT_TEST_ASSERT(!x.empty());

	x.resize(7, VDStringA("q"));
	AT_TEST_ASSERT(x.front() == "x");
	AT_TEST_ASSERT(x.back() == "q");
	AT_TEST_ASSERT(x.size() == 7);
	AT_TEST_ASSERT(x.capacity() >= 7);
	AT_TEST_ASSERT(!x.empty());

	x.reserve(0);
	AT_TEST_ASSERT(x.front() == "x");
	AT_TEST_ASSERT(x.back() == "q");
	AT_TEST_ASSERT(x.size() == 7);
	AT_TEST_ASSERT(x.capacity() >= 7);
	AT_TEST_ASSERT(!x.empty());

	x.reserve(x.capacity() * 2);
	AT_TEST_ASSERT(x.front() == "x");
	AT_TEST_ASSERT(x.back() == "q");
	AT_TEST_ASSERT(x.size() == 7);
	AT_TEST_ASSERT(x.capacity() >= 7);
	AT_TEST_ASSERT(!x.empty());

	x.pop_back();
	AT_TEST_ASSERT(x.front() == "x");
	AT_TEST_ASSERT(x.back() == "foo3");
	AT_TEST_ASSERT(x.size() == 6);
	AT_TEST_ASSERT(x.capacity() >= 6);
	AT_TEST_ASSERT(!x.empty());

	x.erase(x.begin() + 3);
	AT_TEST_ASSERT(x[0] == "x");
	AT_TEST_ASSERT(x[1] == "x");
	AT_TEST_ASSERT(x[2] == "abc");
	AT_TEST_ASSERT(x[3] == "foo2");
	AT_TEST_ASSERT(x[4] == "foo3");
	AT_TEST_ASSERT(x.size() == 5);

	x.erase(x.begin() + 2, x.begin() + 2);
	AT_TEST_ASSERT(x[0] == "x");
	AT_TEST_ASSERT(x[1] == "x");
	AT_TEST_ASSERT(x[2] == "abc");
	AT_TEST_ASSERT(x[3] == "foo2");
	AT_TEST_ASSERT(x[4] == "foo3");
	AT_TEST_ASSERT(x.size() == 5);

	x.erase(x.begin() + 1, x.begin() + 3);
	AT_TEST_ASSERT(x[0] == "x");
	AT_TEST_ASSERT(x[1] == "foo2");
	AT_TEST_ASSERT(x[2] == "foo3");
	AT_TEST_ASSERT(x.size() == 3);

	vdvector<VDStringA> t;
	t.swap(x);
	AT_TEST_ASSERT(t[0] == "x");
	AT_TEST_ASSERT(t[1] == "foo2");
	AT_TEST_ASSERT(t[2] == "foo3");
	AT_TEST_ASSERT(t.size() == 3);
	AT_TEST_ASSERT(x.size() == 0);
	t.swap(x);
	AT_TEST_ASSERT(x[0] == "x");
	AT_TEST_ASSERT(x[1] == "foo2");
	AT_TEST_ASSERT(x[2] == "foo3");
	AT_TEST_ASSERT(x.size() == 3);
	AT_TEST_ASSERT(t.size() == 0);

	x.clear();
	AT_TEST_ASSERT(x.size() == 0);
	AT_TEST_ASSERT(x.capacity() >= 5);
	AT_TEST_ASSERT(x.empty());

	x.push_back(VDStringA("a"));
	x.push_back(VDStringA("b"));
	x.push_back(VDStringA("c"));
	vdvector<VDStringA> c(x);
	AT_TEST_ASSERT(c[0] == "a");
	AT_TEST_ASSERT(c[1] == "b");
	AT_TEST_ASSERT(c[2] == "c");
	AT_TEST_ASSERT(c.size() == 3);
	AT_TEST_ASSERT(c.capacity() == 3);

	x = vdvector<VDStringA>(5);
	AT_TEST_ASSERT(x[0] == "");
	AT_TEST_ASSERT(x[1] == "");
	AT_TEST_ASSERT(x[2] == "");
	AT_TEST_ASSERT(x[3] == "");
	AT_TEST_ASSERT(x[4] == "");
	AT_TEST_ASSERT(x.size() == 5);

	x = vdvector<VDStringA>(3, VDStringA("foo"));
	AT_TEST_ASSERT(x[0] == "foo");
	AT_TEST_ASSERT(x[1] == "foo");
	AT_TEST_ASSERT(x[2] == "foo");
	AT_TEST_ASSERT(x.size() == 3);

	x.push_back_as("abc");
	AT_TEST_ASSERT(x[0] == "foo");
	AT_TEST_ASSERT(x[1] == "foo");
	AT_TEST_ASSERT(x[2] == "foo");
	AT_TEST_ASSERT(x[3] == "abc");
	AT_TEST_ASSERT(x.size() == 4);

	x.insert_as(x.begin() + 1, "def");
	AT_TEST_ASSERT(x[0] == "foo");
	AT_TEST_ASSERT(x[1] == "def");
	AT_TEST_ASSERT(x[2] == "foo");
	AT_TEST_ASSERT(x[3] == "foo");
	AT_TEST_ASSERT(x[4] == "abc");
	AT_TEST_ASSERT(x.size() == 5);

	x.resize(5);
	x.clear();
	x.insert(x.begin(), VDStringA("abc"));
	AT_TEST_ASSERT(x.size() == 1);
	AT_TEST_ASSERT(x[0] == "abc");
	x.insert(x.begin(), VDStringA("def"));
	AT_TEST_ASSERT(x.size() == 2);
	AT_TEST_ASSERT(x[0] == "def");
	AT_TEST_ASSERT(x[1] == "abc");

	return 0;
}
