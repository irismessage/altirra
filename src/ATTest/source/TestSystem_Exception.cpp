//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
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
#include "test.h"

AT_DEFINE_TEST(System_Exception) {
	VDStringA s;

	s.sprintf("%254c", ' ');
	AT_TEST_ASSERT(s == VDException("%254c", ' ').c_str());
	AT_TEST_ASSERT(s == VDException(VDException("%254c", ' ')).c_str());

	s.sprintf("%255c", ' ');
	AT_TEST_ASSERT(s == VDException("%255c", ' ').c_str());

	s.sprintf("%256c", ' ');
	AT_TEST_ASSERT(s == VDException("%256c", ' ').c_str());

	s.sprintf("%257c", ' ');
	AT_TEST_ASSERT(s == VDException("%257c", ' ').c_str());

	s.sprintf("%2000c", ' ');
	AT_TEST_ASSERT(s.size() == 2000);
	AT_TEST_ASSERT(s == VDException("%2000c", ' ').c_str());

	{
		VDException a("hello, world!");
		VDException b(a);

		AT_TEST_ASSERT(!a.empty());
		AT_TEST_ASSERT(!b.empty());
		AT_TEST_ASSERT(VDStringSpanA(a.c_str()) == "hello, world!");
		AT_TEST_ASSERT(VDStringSpanW(a.wc_str()) == L"hello, world!");
		AT_TEST_ASSERT(VDStringSpanA(b.c_str()) == "hello, world!");
		AT_TEST_ASSERT(VDStringSpanW(b.wc_str()) == L"hello, world!");
	}

	{
		VDException a("hello, world!");
		VDException b(std::move(a));

		AT_TEST_ASSERT(!b.empty());
		AT_TEST_ASSERT(VDStringSpanA(b.c_str()) == "hello, world!");
		AT_TEST_ASSERT(VDStringSpanW(b.wc_str()) == L"hello, world!");
	}


	{
		VDException a;
		a.assign("abc");

		AT_TEST_ASSERT(VDStringSpanA(a.c_str()) == "abc");
		AT_TEST_ASSERT(VDStringSpanW(a.wc_str()) == L"abc");
	}

	{
		VDException a(L"abc");

		AT_TEST_ASSERT(VDStringSpanA(a.c_str()) == "abc");
		AT_TEST_ASSERT(VDStringSpanW(a.wc_str()) == L"abc");
	}

	{
		VDException a("abc %d", 10);

		AT_TEST_ASSERT(VDStringSpanA(a.c_str()) == "abc 10");
		AT_TEST_ASSERT(VDStringSpanW(a.wc_str()) == L"abc 10");
	}

	{
		VDException a(L"abc %d", 10);

		AT_TEST_ASSERT(VDStringSpanA(a.c_str()) == "abc 10");
		AT_TEST_ASSERT(VDStringSpanW(a.wc_str()) == L"abc 10");
	}

	{
		VDException a("x%s");

		AT_TEST_ASSERT(VDStringSpanA(a.c_str()) == "x%s");
		AT_TEST_ASSERT(VDStringSpanW(a.wc_str()) == L"x%s");
	}

	{
		VDException a(L"\u3041\u3042");

		AT_TEST_ASSERT(VDStringSpanW(a.wc_str()) == L"\u3041\u3042");
	}

	{
		VDException ex;

		AT_TEST_ASSERT(ex.empty());
		AT_TEST_ASSERT(ex.c_str() == nullptr);
		AT_TEST_ASSERT(ex.wc_str() == nullptr);
		AT_TEST_ASSERT(ex.what() != nullptr);
	}

	{
		VDUserCancelException ex;

		AT_TEST_ASSERT(!ex.empty());
		AT_TEST_ASSERT(VDStringSpanA(ex.c_str()) == "");
		AT_TEST_ASSERT(VDStringSpanW(ex.wc_str()) == L"");

		VDException ex2(std::move(ex));
		AT_TEST_ASSERT(ex.empty());
		AT_TEST_ASSERT(!ex2.empty());
		AT_TEST_ASSERT(VDStringSpanA(ex2.c_str()) == "");
		AT_TEST_ASSERT(VDStringSpanW(ex2.wc_str()) == L"");
	}

	try {
		try {
			throw VDException("Hello, world!");
		} catch(const VDException& ex) {
			AT_TEST_ASSERT(VDStringSpanA(ex.c_str()) == "Hello, world!");
		}
	} catch(...) {
		AT_TEST_ASSERTF(false, "Exception not caught");
	}

	try {
		try {
			throw VDException("Hello, world!");
		} catch(...) {
			try {
				throw;
			} catch(const VDException& ex) {
				AT_TEST_ASSERT(VDStringSpanA(ex.c_str()) == "Hello, world!");
			} catch(...) {
				AT_TEST_ASSERTF(false, "Exception not caught");
			}
		}
	} catch(...) {
		AT_TEST_ASSERTF(false, "Exception not caught");
	}

	try {
		try {
			throw VDException("Hello, world!");
		} catch(const std::exception& ex) {
			AT_TEST_ASSERT(VDStringSpanA(ex.what()) == "Hello, world!");
		}
	} catch(...) {
		AT_TEST_ASSERTF(false, "Exception not caught");
	}

	// test narrow string formatting exceptions
	{
		VDException ex("%ls", L"\uFFFE");

		AT_TEST_ASSERT(VDStringSpanA(ex.c_str()) == "<%ls>");
		AT_TEST_ASSERT(VDStringSpanW(ex.wc_str()) == L"<%ls>");
	}

	return 0;
}
