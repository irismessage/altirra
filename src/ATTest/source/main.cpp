#include <stdafx.h>
#include <stdio.h>
#include <signal.h>
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

#ifdef VD_CPU_ARM64
#include <windows.h>
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
	wprintf(L"    /allexts   Run tests with all possible CPU extension subsets\n");
	wprintf(L"    /big       Run tests on big cores (ARM64 only)\n");
	wprintf(L"    /little    Run tests on LITTLE cores (ARM64 only)\n");
	wprintf(L"    /v         Enable verbose test output\n");
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

	wprintf(L"Altirra test harness utility for " BUILD L"\n");
	wprintf(L"Copyright (C) 2016-2022 Avery Lee. Licensed under GNU General Public License, version 2 or later\n\n");

	ATTestInitBlobHandler();

	Tests selectedTests;

#ifdef VD_CPU_ARM64
	bool useBigCores = false;
	bool useLittleCores = false;
#endif

	bool testAllCpuExts = false;

	if (argc <= 1) {
		ATTestHelp();
		exit(0);
	} else {
		for(int i=1; i<argc; ++i) {
			const wchar_t *test = argv[i];

			if (!wcscmp(test, L"/v")) {
				g_ATTestTracingEnabled = true;
				continue;
			}

			if (!wcscmp(test, L"/allexts")) {
				testAllCpuExts = true;
				continue;
			}

#ifdef VD_CPU_ARM64
			if (!wcscmp(test, L"/big")) {
				useBigCores = true;
				continue;
			}

			if (!wcscmp(test, L"/little")) {
				useLittleCores = true;
				continue;
			}
#endif

			if (test[0] == L'/') {
				wprintf(L"Unknown switch: %ls\n", test);
				exit(5);
			}

			if (!_wcsicmp(test, L"all")) {
				for(const TestInfo& ent : GetTests()) {
					if (ent.mbAutoRun)
						selectedTests.push_back(ent);
				}
				break;
			}

			for(const TestInfo& ent : GetTests()) {
				if (!vdwcsicmp(VDTextAToW(ent.mpName).c_str(), test)) {
					selectedTests.push_back(ent);
					goto next;
				}
			}

			wprintf(L"\nUnknown test: %ls\n", test);
			ATTestHelp();
			exit(5);
next:
			;
		}
	}

#ifdef VD_CPU_ARM64
	if (useLittleCores || useBigCores) {
		ULONG len = 0;
		GetSystemCpuSetInformation(nullptr, 0, &len, GetCurrentProcess(), 0);
		if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && len) {
			char *buf = (char *)malloc(len);
			if (buf) {
				ULONG len2;
				if (GetSystemCpuSetInformation((PSYSTEM_CPU_SET_INFORMATION)buf, len, &len2, GetCurrentProcess(), 0)) {
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
#endif

	long exts = CPUCheckForExtensions();
	int failedTests = 0;

	for(;;) {
		CPUEnableExtensions(exts);

		if (testAllCpuExts)
			wprintf(L"\n=== Setting CPU extensions: %08X ===\n\n", exts);

		for(Tests::const_iterator it(selectedTests.begin()), itEnd(selectedTests.end()); it!=itEnd; ++it) {
			const Tests::value_type& ent = *it;

			wprintf(L"Running test: %hs\n", ent.mpName);

			try {
				ent.mpTestFn();
			} catch(const AssertionException& e) {
				wprintf(L"    TEST FAILED: %hs\n", e.gets());
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
