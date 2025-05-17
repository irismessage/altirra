#include <stdafx.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <windows.h>
#include <corecrt_startup.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/cpuaccel.h>
#include <vd2/system/VDString.h>
#include <vd2/system/strutil.h>
#include <vd2/system/text.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/bitmath.h>

#include "test.h"

#include <vector>
#include <utility>

#if defined(_M_IX86)
	#define BUILD L"80x86"
#elif defined(_M_AMD64)
	#define BUILD L"AMD64"
#elif defined(_M_ARM64)
	#define BUILD L"ARM64"
#endif

extern void ATTestInitBlobHandler();

namespace {
	struct TestInfo {
		ATTestFn	mpTestFn;
		const char	*mpName;
		bool		mbAutoRun;
	};

	typedef vdfastvector<TestInfo> Tests;

	Tests& GetTests() {
		static Tests g_tests;
		return g_tests;
	}
}

void ATTestAddTest(ATTestFn f, const char *name, bool autoRun) {
	TestInfo ti;
	ti.mpTestFn = f;
	ti.mpName = name;
	ti.mbAutoRun = autoRun;
	GetTests().push_back(ti);
}

void ATTestHelp() {
	wprintf(L"\n");
	wprintf(L"Usage: AltirraTest [options] tests... | all");
	wprintf(L"\n");
	wprintf(L"Options:\n");
	wprintf(L"    /allexts        Run tests with all possible CPU extension subsets\n");
	wprintf(L"    /big,/pcore     Run tests on big/performance cores\n");
	wprintf(L"    /little,/ecore  Run tests on LITTLE/efficiency cores\n");
	wprintf(L"    /ext            Select CPU extensions to use\n");
#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
	wprintf(L"        Tiers: sse2, sse3, ssse3, sse4.1, sse4.2, avx, avx2\n");
#endif
	wprintf(L"    /v              Enable verbose test output\n");
	wprintf(L"\n");

	wprintf(L"Available tests:\n");

	auto tests = GetTests();

	std::sort(tests.begin(), tests.end(),
		[](const TestInfo& x, const TestInfo& y) {
			return vdstricmp(x.mpName, y.mpName) < 0;
		}
	);

	for(const TestInfo& ent : tests) {

		wprintf(L"\t%hs%s\n", ent.mpName, ent.mbAutoRun ? L"" : L"*");
	}
	wprintf(L"\tAll\n");
}

int ATTestMain(int argc, wchar_t **argv);

int ATTestMain() {
	_configure_wide_argv(_crt_argv_unexpanded_arguments);

	return ATTestMain(__argc, __wargv);
}

int ATTestMain(int argc, wchar_t **argv) {
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_CHECK_ALWAYS_DF | _CRTDBG_LEAK_CHECK_DF);
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
	_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
	_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
	_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
	
	signal(SIGABRT,
		[](int) {
			if (IsDebuggerPresent())
				__debugbreak();

			static constexpr char str[] = "Fatal error: abort() has been called\n";

			DWORD actual = 0;
			WriteFile(GetStdHandle(STD_ERROR_HANDLE), str, (sizeof str) - 1, &actual, nullptr);
			TerminateProcess(GetCurrentProcess(), 20);
		}
	);

	set_terminate(
		[] {
			if (IsDebuggerPresent())
				__debugbreak();

			static constexpr char str[] = "Fatal error: terminate() has been called\n";

			DWORD actual = 0;
			WriteFile(GetStdHandle(STD_ERROR_HANDLE), str, (sizeof str) - 1, &actual, nullptr);
			TerminateProcess(GetCurrentProcess(), 20);
		}
	);

	_set_abort_behavior(_CALL_REPORTFAULT, _CALL_REPORTFAULT);

	wprintf(L"Altirra test harness utility for " BUILD L"\n");
	wprintf(L"Copyright (C) 2016-2023 Avery Lee. Licensed under GNU General Public License, version 2 or later\n\n");

	ATTestInitBlobHandler();

	struct SelectedTest {
		const TestInfo *testInfo = nullptr;
		VDStringW mArgs;
	};

	vdvector<SelectedTest> selectedTests;

	bool useBigCores = false;
	bool useLittleCores = false;
	bool testAllCpuExts = false;
	uint32 selectedExts = 0;

	if (argc <= 1) {
		ATTestHelp();
		exit(0);
	} else {
		for(int i=1; i<argc; ++i) {
			VDStringSpanW test(argv[i]);

			if (test == L"/v") {
				g_ATTestTracingEnabled = true;
				continue;
			}

			if (test == L"/allexts") {
				testAllCpuExts = true;
				continue;
			}

			if (test == L"/ext") {
				if (i + 1 == argc) {
					puts("Error: CPU extension required after /exts");
					return 10;
				}

				VDStringSpanW extName(argv[++i]);

#if defined(VD_CPU_X86) || defined(VD_CPU_X64)
				if (extName == L"sse2") {
					selectedExts
						= CPUF_SUPPORTS_MMX
						| CPUF_SUPPORTS_INTEGER_SSE
						| CPUF_SUPPORTS_SSE
						| CPUF_SUPPORTS_SSE2
						;
				} else if (extName == L"sse3") {
					selectedExts
						= CPUF_SUPPORTS_MMX
						| CPUF_SUPPORTS_INTEGER_SSE
						| CPUF_SUPPORTS_SSE
						| CPUF_SUPPORTS_SSE2
						| CPUF_SUPPORTS_SSE3
						;
				} else if (extName == L"ssse3") {
					selectedExts
						= CPUF_SUPPORTS_MMX
						| CPUF_SUPPORTS_INTEGER_SSE
						| CPUF_SUPPORTS_SSE
						| CPUF_SUPPORTS_SSE2
						| CPUF_SUPPORTS_SSE3
						| CPUF_SUPPORTS_SSSE3
						;
				} else if (extName == L"sse4.1") {
					selectedExts
						= CPUF_SUPPORTS_MMX
						| CPUF_SUPPORTS_INTEGER_SSE
						| CPUF_SUPPORTS_SSE
						| CPUF_SUPPORTS_SSE2
						| CPUF_SUPPORTS_SSE3
						| CPUF_SUPPORTS_SSSE3
						| CPUF_SUPPORTS_SSE41
						;
				} else if (extName == L"sse4.2") {
					selectedExts
						= CPUF_SUPPORTS_MMX
						| CPUF_SUPPORTS_INTEGER_SSE
						| CPUF_SUPPORTS_SSE
						| CPUF_SUPPORTS_SSE2
						| CPUF_SUPPORTS_SSE3
						| CPUF_SUPPORTS_SSSE3
						| CPUF_SUPPORTS_SSE41
						| CPUF_SUPPORTS_SSE42
						| VDCPUF_SUPPORTS_POPCNT
						;
				} else if (extName == L"avx") {
					selectedExts
						= CPUF_SUPPORTS_MMX
						| CPUF_SUPPORTS_INTEGER_SSE
						| CPUF_SUPPORTS_SSE
						| CPUF_SUPPORTS_SSE2
						| CPUF_SUPPORTS_SSE3
						| CPUF_SUPPORTS_SSSE3
						| CPUF_SUPPORTS_SSE41
						| CPUF_SUPPORTS_SSE42
						| VDCPUF_SUPPORTS_POPCNT
						| VDCPUF_SUPPORTS_AVX
						;
				} else if (extName == L"avx2") {
					selectedExts
						= CPUF_SUPPORTS_MMX
						| CPUF_SUPPORTS_INTEGER_SSE
						| CPUF_SUPPORTS_SSE
						| CPUF_SUPPORTS_SSE2
						| CPUF_SUPPORTS_SSE3
						| CPUF_SUPPORTS_SSSE3
						| CPUF_SUPPORTS_SSE41
						| CPUF_SUPPORTS_SSE42
						| CPUF_SUPPORTS_LZCNT
						| VDCPUF_SUPPORTS_AVX
						| VDCPUF_SUPPORTS_AVX2
						| VDCPUF_SUPPORTS_FMA
						;
				} else
#endif
				{
					printf("Error: Unknown CPU extension: %ls\n", argv[i]);
					return 20;
				}

				continue;
			}

			if (test == L"/big" || test == L"/pcore") {
				useBigCores = true;
				continue;
			}

			if (test == L"/little" || test == L"/ecore") {
				useLittleCores = true;
				continue;
			}

			if (test[0] == L'/') {
				wprintf(L"Unknown switch: %ls\n", argv[i]);
				exit(5);
			}

			if (test.comparei(L"all") == 0) {
				for(const TestInfo& ent : GetTests()) {
					if (ent.mbAutoRun)
						selectedTests.emplace_back(&ent);
				}
				break;
			}

			VDStringW testName = test;
			VDStringW testArgs;
			auto splitPt = test.find(L':');

			if (splitPt != VDStringW::npos) {
				testArgs = testName.subspan(splitPt + 1);
				testName.erase(splitPt, VDStringW::npos);
			}

			for(const TestInfo& ent : GetTests()) {
				if (VDTextAToW(ent.mpName).comparei(testName) == 0) {
					selectedTests.emplace_back(&ent, testArgs);
					goto next;
				}
			}

			wprintf(L"\nUnknown test: %ls\n", testName.c_str());
			ATTestHelp();
			exit(5);
next:
			;
		}
	}

	if (useLittleCores || useBigCores) {
		HMODULE hmodKernel32 = GetModuleHandle(L"kernel32");
		const auto pGetSystemCpuSetInformation = (decltype(GetSystemCpuSetInformation) *)GetProcAddress(hmodKernel32, "GetSystemCpuSetInformation");

		if (!pGetSystemCpuSetInformation) {
			puts("CPU sets not available.");
		} else {
			ULONG len = 0;
			pGetSystemCpuSetInformation(nullptr, 0, &len, GetCurrentProcess(), 0);
			if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && len) {
				char *buf = (char *)malloc(len);
				if (buf) {
					ULONG len2;
					if (pGetSystemCpuSetInformation((PSYSTEM_CPU_SET_INFORMATION)buf, len, &len2, GetCurrentProcess(), 0)) {
						ULONG offset = 0;
						BYTE minEfficiency = 0xFF;
						BYTE maxEfficiency = 0x00;

						DWORD_PTR affinityMask = 0;

						printf("--- begin CPU set information ---\n\n");

						while(offset + 8 <= len2) {
							SYSTEM_CPU_SET_INFORMATION *info = (SYSTEM_CPU_SET_INFORMATION *)(buf + offset);
							const DWORD infoSize = info->Size;

							if (len2 - offset < infoSize || info->Size < 8)
								break;

							ULONG maxStructSize = infoSize - 8;
							if (info->Type == CpuSetInformation) {
								if (maxStructSize >= sizeof(info->CpuSet)) {
									printf("    ID %08X | Group %04X | LPI %02X | CoreIndex %02X | LLCI %02X | Numa %02X | EfficiencyClass %02X\n"
										, info->CpuSet.Id
										, info->CpuSet.Group
										, info->CpuSet.LogicalProcessorIndex
										, info->CpuSet.CoreIndex
										, info->CpuSet.LastLevelCacheIndex
										, info->CpuSet.NumaNodeIndex
										, info->CpuSet.EfficiencyClass
									);

									if (useLittleCores) {
										if (minEfficiency > info->CpuSet.EfficiencyClass) {
											minEfficiency = info->CpuSet.EfficiencyClass;

											affinityMask = 0;
										}

										if (info->CpuSet.EfficiencyClass == minEfficiency)
											affinityMask |= ((DWORD_PTR)1 << info->CpuSet.LogicalProcessorIndex);
									} else {
										if (maxEfficiency < info->CpuSet.EfficiencyClass) {
											maxEfficiency = info->CpuSet.EfficiencyClass;

											affinityMask = 0;
										}

										if (info->CpuSet.EfficiencyClass == maxEfficiency)
											affinityMask |= ((DWORD_PTR)1 << info->CpuSet.LogicalProcessorIndex);
									}
								}
							}

							offset += infoSize;
						}

						printf("--- end CPU set information ---\n\n");

						if (affinityMask && SetProcessAffinityMask(GetCurrentProcess(), affinityMask))
							printf("Successfully set affinity mask %08llX.\n", (unsigned long long)affinityMask);
						else
							printf("Failed to set affinity mask %08llX.\n", (unsigned long long)affinityMask);
					}
				}

				free(buf);
			}
		}
	}

	long exts = CPUCheckForExtensions();
	int failedTests = 0;

	if (selectedExts) {
		if ((selectedExts & exts) != selectedExts) {
			printf("Error: Cannot run tests with selected CPU extensions due to missing CPU extension: %08X\n", selectedExts & ~exts);
			return 10;
		}

		exts = selectedExts;
	}

	for(;;) {
		CPUEnableExtensions(exts);

		if (testAllCpuExts)
			wprintf(L"\n=== Setting CPU extensions: %08X ===\n\n", exts);

		for(const SelectedTest& selTest : selectedTests) {

			wprintf(L"Running test: %hs\n", selTest.testInfo->mpName);

			try {
				ATTestSetArguments(selTest.mArgs.c_str());
				if (selTest.testInfo->mpTestFn())
					throw AssertionException();
			} catch(const AssertionException& e) {
				wprintf(L"    TEST FAILED: %ls\n", e.wc_str());
				++failedTests;
			}
		}

		if (!exts || !testAllCpuExts)
			break;

		exts &= ~(1 << VDFindHighestSetBitFast(exts));
	}

	printf("Tests complete. Failures: %u\n", failedTests);

	return failedTests;
}
