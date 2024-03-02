#include <stdafx.h>
#include <vd2/system/math.h>
#include <test.h>

namespace {
	template<typename T_Src, typename T_Dst, typename T_Fn>
	void TestFunction(T_Fn&& function, std::initializer_list<std::pair<T_Src, T_Dst>> testVectors) {
		for(const auto& testVector : testVectors) {
			TEST_ASSERT(function(testVector.first) == testVector.second);
		}
	}
}

DEFINE_TEST(System_Math) {
	TestFunction<float, int>(static_cast<int(*)(float)>(VDRoundToInt), {
		{ 0.00f, 0 },
		{ 0.45f, 0 },
		{ 0.55f, 1 },
		{ 1.00f, 1 },
		{ 1.45f, 1 },
		{ 1.55f, 2 },
		{ 2.00f, 2 },
		{ -0.45f, 0 },
		{ -0.55f, -1 },
		{ -1.45f, -1 },
		{ -1.55f, -2 },
		{ -2.0f, -2 },
		{ 16777216.0f, 16777216 },
		{ -16777216.0f, -16777216 },
		{ 0x1.0p30f, (int)(1 << 30) },
		{ -0x1.0p30f, -(int)(1 << 30) },
	});

	TestFunction<double, int>(static_cast<int(*)(double)>(VDRoundToInt), {
		{ 0.00, 0 },
		{ 0.45, 0 },
		{ 0.55, 1 },
		{ 1.00, 1 },
		{ 1.45, 1 },
		{ 1.55, 2 },
		{ 2.00, 2 },
		{ -0.45, 0 },
		{ -0.55, -1 },
		{ -1.45, -1 },
		{ -1.55, -2 },
		{ -2.00, -2 },
		{ 16777216.0, 16777216 },
		{ -16777216.0, -16777216 },
		{ 0x1.2345678p28, 0x12345678 },
		{ -0x1.2345678p28, -0x12345678 },
	});

	TestFunction<float, sint32>(static_cast<sint32(*)(float)>(VDRoundToInt32), {
		{ 0.00f, 0 },
		{ 0.45f, 0 },
		{ 0.55f, 1 },
		{ 1.00f, 1 },
		{ 1.45f, 1 },
		{ 1.55f, 2 },
		{ 2.00f, 2 },
		{ -0.45f, 0 },
		{ -0.55f, -1 },
		{ -1.45f, -1 },
		{ -1.55f, -2 },
		{ -2.0f, -2 },
		{ 16777216.0f, 16777216 },
		{ -16777216.0f, -16777216 },
		{ 0x1.0p30f, (int)(1 << 30) },
		{ -0x1.0p30f, -(int)(1 << 30) },
		});

	TestFunction<double, sint32>(static_cast<sint32(*)(double)>(VDRoundToInt32), {
		{ 0.00, 0 },
		{ 0.45, 0 },
		{ 0.55, 1 },
		{ 1.00, 1 },
		{ 1.45, 1 },
		{ 1.55, 2 },
		{ 2.00, 2 },
		{ -0.45, 0 },
		{ -0.55, -1 },
		{ -1.45, -1 },
		{ -1.55, -2 },
		{ -2.00, -2 },
		{ 16777216.0, 16777216 },
		{ -16777216.0, -16777216 },
		{ 0x1.2345678p28, 0x12345678 },
		{ -0x1.2345678p28, -0x12345678 },
		});

	TestFunction<float, sint64>(static_cast<sint64(*)(float)>(VDRoundToInt64), {
		{ 0.00f, 0 },
		{ 0.45f, 0 },
		{ 0.55f, 1 },
		{ 1.00f, 1 },
		{ 1.45f, 1 },
		{ 1.55f, 2 },
		{ 2.00f, 2 },
		{ -0.45f, 0 },
		{ -0.55f, -1 },
		{ -1.45f, -1 },
		{ -1.55f, -2 },
		{ -2.0f, -2 },
		{ 16777216.0f, 16777216 },
		{ -16777216.0f, -16777216 },
		{ 0x1.0p48f, ((sint64)1 << 48) },
		{ -0x1.0p48f, -((sint64)1 << 48) },
		});

	TestFunction<double, sint64>(static_cast<sint64(*)(double)>(VDRoundToInt64), {
		{ 0.00, 0 },
		{ 0.45, 0 },
		{ 0.55, 1 },
		{ 1.00, 1 },
		{ 1.45, 1 },
		{ 1.55, 2 },
		{ 2.00, 2 },
		{ -0.45, 0 },
		{ -0.55, -1 },
		{ -1.45, -1 },
		{ -1.55, -2 },
		{ -2.00, -2 },
		{ 16777216.0, 16777216 },
		{ -16777216.0, -16777216 },
		{ 0x1.23456789ABCDEp52, INT64_C(0x123456789ABCDE) },
		{ -0x1.23456789ABCDEp52, -INT64_C(0x123456789ABCDE) },
		});

	return 0;
}
