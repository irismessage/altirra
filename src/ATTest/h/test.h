#ifndef f_TEST_H
#define f_TEST_H

#include <vd2/system/error.h>

typedef int (*TestFn)();

extern void AddTest(TestFn, const char *, bool autoRun);

#define AT_DEFINE_TEST(name) int Test##name(); namespace { struct TestAutoInit_##name { TestAutoInit_##name() { AddTest(Test##name, #name, true); } } g_testAutoInit_##name; } int Test##name()
#define AT_DEFINE_TEST_NONAUTO(name) int Test##name(); namespace { struct TestAutoInit_##name { TestAutoInit_##name() { AddTest(Test##name, #name, false); } } g_testAutoInit_##name; } int Test##name()

#define DEFINE_TEST AT_DEFINE_TEST
#define DEFINE_TEST_NONAUTO AT_DEFINE_TEST_NONAUTO

class AssertionException : public MyError {
public:
	AssertionException(const char *s, ...) {
		va_list val;
		va_start(val, s);
		vsetf(s, val);
		va_end(val);
	}
};

bool ShouldBreak();

#define AT_TEST_ASSERT_STRINGIFY(x) AT_TEST_ASSERT_STRINGIFY1(x)
#define AT_TEST_ASSERT_STRINGIFY1(x) #x

#define AT_TEST_ASSERT(condition) if (!(condition)) { ShouldBreak() ? __debugbreak() : throw AssertionException("%s", "Test assertion failed at line " AT_TEST_ASSERT_STRINGIFY(__LINE__) ": " #condition); volatile int _x = 0; _x = 1; } else ((void)0)
#define AT_TEST_ASSERTF(condition, msg, ...) if (!(condition)) { ShouldBreak() ? __debugbreak() : throw AssertionException("Test assertion failed at line " AT_TEST_ASSERT_STRINGIFY(__LINE__) ": " msg, __VA_ARGS__); volatile int _x = 0; _x = 1; } else ((void)0)

#define TEST_ASSERT AT_TEST_ASSERT
#define TEST_ASSERTF AT_TEST_ASSERTF

#endif
