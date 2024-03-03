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

#ifndef f_AT_ATTEST_TEST_H
#define f_AT_ATTEST_TEST_H

#ifdef ATNRELEASE
	#define AT_TESTS_ENABLED 1
#endif

#include <vd2/system/error.h>

#ifdef AT_TESTS_ENABLED
	typedef int (*ATTestFn)();

	extern void ATTestAddTest(ATTestFn, const char *, bool autoRun);

	#define AT_DEFINE_TEST2(name, autorun) \
		static class ATTest_##name { \
		public: \
			ATTest_##name() { \
				ATTestAddTest(RunTest, #name, autorun); \
			} \
			static int RunTest(); \
		} g_ATTest_##name; \
		int ATTest_##name::RunTest()

	#define AT_DEFINE_TEST(name) AT_DEFINE_TEST2(name, true)
	#define AT_DEFINE_TEST_NONAUTO(name) AT_DEFINE_TEST2(name, false)

	class ATTestAssertionException : public MyError {
	public:
		ATTestAssertionException(const char *s, ...) {
			va_list val;
			va_start(val, s);
			vsetf(s, val);
			va_end(val);
		}
	};

	bool ATTestShouldBreak();

	void ATTestBeginTestLoop();
	bool ATTestShouldContinueTestLoop();

	void ATTestSetArguments(const wchar_t *args);
	const wchar_t *ATTestGetArguments();

	extern bool g_ATTestTracingEnabled;
	void ATTestTrace(const char *msg);
	void ATTestTraceF(const char *format, ...);

	#define AT_TEST_ASSERT_STRINGIFY(x) AT_TEST_ASSERT_STRINGIFY1(x)
	#define AT_TEST_ASSERT_STRINGIFY1(x) #x

	#define AT_TEST_ASSERT(condition) if (!(condition)) { ATTestShouldBreak() ? __debugbreak() : throw ATTestAssertionException("%s", "Test assertion failed at line " AT_TEST_ASSERT_STRINGIFY(__LINE__) ": " #condition); volatile int _x = 0; (void)_x; } else ((void)0)
	#define AT_TEST_ASSERTF(condition, msg, ...) if (!(condition)) { ATTestShouldBreak() ? __debugbreak() : throw ATTestAssertionException("Test assertion failed at line " AT_TEST_ASSERT_STRINGIFY(__LINE__) ": " msg, __VA_ARGS__); volatile int _x = 0; (void)_x; } else ((void)0)

	#define AT_TEST_NONFATAL_ASSERT(condition) \
		if (!(condition)) { \
			(ATTestShouldBreak() ? __debugbreak() : (void)0), \
			g_ATTestTracingEnabled \
					? (void)puts("Test assertion failed at line " AT_TEST_ASSERT_STRINGIFY(__LINE__) ": " #condition) \
					: throw ATTestAssertionException("%s", "Test assertion failed at line " AT_TEST_ASSERT_STRINGIFY(__LINE__) ": " #condition); \
		} else ((void)0)

	#define AT_TEST_NONFATAL_ASSERTF(condition, msg, ...) \
		if (!(condition)) { \
			(ATTestShouldBreak() ? __debugbreak() : (void)0), \
			g_ATTestTracingEnabled \
				? (void)printf("Test assertion failed at line " AT_TEST_ASSERT_STRINGIFY(__LINE__) ": " msg "\n", __VA_ARGS__) \
				: throw ATTestAssertionException("Test assertion failed at line " AT_TEST_ASSERT_STRINGIFY(__LINE__) ": " msg, __VA_ARGS__); \
		} else ((void)0)

	#define AT_TEST_TRACE(msg) if (!g_ATTestTracingEnabled); else ATTestTrace(msg);
	#define AT_TEST_TRACEF(format, ...) if (!g_ATTestTracingEnabled); else ATTestTraceF(format, __VA_ARGS__);
#endif

#endif
