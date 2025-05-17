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
#include <vd2/system/time.h>
#include <vd2/system/math.h>
#include <vd2/system/vdalloc.h>
#include <at/atcore/fft.h>
#include "test.h"

AT_DEFINE_TEST(Core_FFT) {
	vdautoptr<ATFFT<16>> fft16(new ATFFT<16>);
	vdautoptr<ATFFT<32>> fft32(new ATFFT<32>);
	vdautoptr<ATFFT<64>> fft64(new ATFFT<64>);
	vdautoptr<ATFFT<2048>> fft2k(new ATFFT<2048>);
	vdautoptr<ATFFT<4096>> fft4k(new ATFFT<4096>);

	struct xdat {
		alignas(64) float x[4096];
		alignas(64) float y[4096];
		alignas(64) float ref[4096];
		alignas(64) float costab[4096];
	};
	vdautoptr<xdat> dat0(new xdat);
	xdat *dat = dat0;

	AT_TEST_TRACE("FFT16:");

	for(int i=0; i<16; ++i) {
		std::fill(dat->x, dat->x + 16, 0.0f);
		dat->x[i] = 1;

		// compute reference output (slowly)
		float ref[16] {};
		const float div = -nsVDMath::kfTwoPi / 16.0f;
		for(int j=0; j<=8; ++j) {
			float rsum = 0;
			float isum = 0;

			for(int k=0; k<16; ++k) {
				float w = (div * j) * k;
				rsum += dat->x[k] * cosf(w);
				isum += dat->x[k] * sinf(w);
			}

			if (j == 8)
				ref[1] = rsum;
			else {
				ref[j*2] = rsum;
				ref[j*2+1] = isum;
			}
		}

		fft16->Forward(dat->x);

		bool failfwd = false;
		for(int j=0; j<16; ++j) {
			if (fabsf(dat->x[j] - ref[j]) > 1e-3f)
				failfwd = true;
		}

		memcpy(dat->y, dat->x, sizeof dat->y);
		fft16->Inverse(dat->y);

		bool failinv = false;
		for(int j=0; j<16; ++j) {
			if (i == j) {
				if (fabsf(dat->y[j] - 8.0f) > 1e-3f)
					failinv = true;
			} else {
				if (fabsf(dat->y[j]) > 2e-3f)
					failinv = true;
			}
		}

		if (failfwd)
			printf("FFT16 forward fail (pos %d)\n", i);
		else if (failinv)
			printf("FFT16 inverse fail (pos %d)\n", i);

		if (g_ATTestTracingEnabled || failfwd || failinv) {
			for(int j=0; j<16; ++j)
				printf(" %4.1f", ref[j]);

			puts("  reference forward");

			for(int j=0; j<16; ++j)
				printf(" %4.1f", dat->x[j]);

			puts("  forward");

			for(int j=0; j<16; ++j)
				printf(" %4.1f", dat->y[j]);

			puts("  inverse");
		}

		if (failfwd || failinv)
			return 1;
	}

	AT_TEST_TRACE("FFT16 pass");

	AT_TEST_TRACE("FFT32:");

	for(int i=0; i<32; ++i) {
		std::fill(dat->x, dat->x + 32, 0.0f);
		dat->x[i] = 1;

		// compute reference output (slowly)
		float ref[32] {};
		const float div = -nsVDMath::kfTwoPi / 32.0f;
		for(int j=0; j<=16; ++j) {
			float rsum = 0;
			float isum = 0;

			for(int k=0; k<32; ++k) {
				float w = (div * j) * k;
				rsum += dat->x[k] * cosf(w);
				isum += dat->x[k] * sinf(w);
			}

			if (j == 16)
				ref[1] = rsum;
			else {
				ref[j*2] = rsum;
				ref[j*2+1] = isum;
			}
		}

		fft32->Forward(dat->x);

		bool failfwd = false;
		for(int j=0; j<32; ++j) {
			if (fabsf(dat->x[j] - ref[j]) > 1e-3f)
				failfwd = true;
		}

		memcpy(dat->y, dat->x, sizeof dat->y);
		fft32->Inverse(dat->y);

		bool failinv = false;
		for(int j=0; j<32; ++j) {
			if (i == j) {
				if (fabsf(dat->y[j] - 16.0f) > 1e-3f)
					failinv = true;
			} else {
				if (fabsf(dat->y[j]) > 2e-3f)
					failinv = true;
			}
		}

		if (failfwd)
			printf("FFT32 forward fail (pos %d)\n", i);
		else if (failinv)
			printf("FFT32 inverse fail (pos %d)\n", i);

		if (g_ATTestTracingEnabled || failfwd || failinv) {
			for(int j=0; j<32; ++j)
				printf(" %4.1f", ref[j]);

			puts("  reference forward");

			for(int j=0; j<32; ++j)
				printf(" %4.1f", dat->x[j]);

			puts("  forward");

			for(int j=0; j<32; ++j)
				printf(" %4.1f", dat->y[j]);

			puts("  inverse");
		}

		if (failfwd || failinv)
			return 1;
	}

	AT_TEST_TRACE("FFT32 pass");
	
	AT_TEST_TRACE("FFT64:");

	for(int i=0; i<64; ++i) {
		std::fill(dat->x, dat->x + 64, 0.0f);
		dat->x[i] = 1;

		fft64->Forward(dat->x);
		fft64->Inverse(dat->x);

		bool fail = false;
		for(int j=0; j<64; ++j) {
			if (i == j) {
				if (fabsf(dat->x[j] - 32.0f) > 1e-3f)
					fail = true;
			} else {
				if (fabsf(dat->x[j]) > 2e-3f)
					fail = true;
			}
		}

		if (fail) {
			printf("FFT64 fail at pos %d\n", i);
			return 1;
		}
	}

	AT_TEST_TRACE("FFT64 pass");

	// we only test a subset of the 4K positions for speed reasons
	for(int i=0; i<2048; i += 37) {
		std::fill(dat->x, dat->x + 2048, 0.0f);
		dat->x[i] = 1;

		// spot check only a single reference bucket, since it's very slow
		if (i == 37*13) {
			const float div = -nsVDMath::kfTwoPi / 2048;
			for(int j=0; j<2048; ++j) {
				dat->costab[j] = cosf(div * j);
			}

			for(int j=0; j<=1024; ++j) {
				float rsum = 0;
				float isum = 0;
				 
				for(int k=0; k<2048; ++k) {
					rsum += dat->x[k] * dat->costab[(j*k) & 2047];
					isum += dat->x[k] * dat->costab[(j*k + 512) & 2047];
				}

				if (j == 1024)
					dat->ref[1] = rsum;
				else {
					dat->ref[j*2] = rsum;
					dat->ref[j*2+1] = isum;
				}
			}
		}

		fft2k->Forward(dat->x);

		if (i == 37*13) {
			for(int j=0; j<2048; ++j) {
				if (fabsf(dat->x[j] - dat->ref[j]) > 1e-3f) {
					printf("FFT2K forward fail\n");
					return 1;
				}
			}
		}

		fft2k->Inverse(dat->x);

		bool fail = false;
		for(int j=0; j<2048; ++j) {
			if (i == j) {
				if (fabsf(dat->x[j] - 1024.0f) > 1e-3f)
					fail = true;
			} else {
				if (fabsf(dat->x[j]) > 2e-3f)
					fail = true;
			}
		}

		if (fail) {
			printf("FFT2K fail\n");
			return 1;
		}
	}

	AT_TEST_TRACE("FFT2K pass");

	// we only test a subset of the 4K positions for speed reasons
	for(int i=0; i<4096; i += 37) {
		std::fill(dat->x, dat->x + 4096, 0.0f);
		dat->x[i] = 1;

		// spot check only a single reference bucket, since it's very slow
		if (i == 37*13) {
			const float div = -nsVDMath::kfTwoPi / 4096.0f;
			for(int j=0; j<4096; ++j) {
				dat->costab[j] = cosf(div * j);
			}

			for(int j=0; j<=2048; ++j) {
				float rsum = 0;
				float isum = 0;
				 
				for(int k=0; k<4096; ++k) {
					rsum += dat->x[k] * dat->costab[(j*k) & 4095];
					isum += dat->x[k] * dat->costab[(j*k + 1024) & 4095];
				}

				if (j == 2048)
					dat->ref[1] = rsum;
				else {
					dat->ref[j*2] = rsum;
					dat->ref[j*2+1] = isum;
				}
			}
		}

		fft4k->Forward(dat->x);

		if (i == 37*13) {
			for(int j=0; j<4096; ++j) {
				if (fabsf(dat->x[j] - dat->ref[j]) > 1e-3f) {
					printf("FFT4K forward fail\n");
					return 1;
				}
			}
		}

		fft4k->Inverse(dat->x);

		bool fail = false;
		for(int j=0; j<4096; ++j) {
			if (i == j) {
				if (fabsf(dat->x[j] - 2048.0f) > 1e-3f)
					fail = true;
			} else {
				if (fabsf(dat->x[j]) > 2e-3f)
					fail = true;
			}
		}

		if (fail) {
			printf("FFT4K fail\n");
			return 1;
		}
	}

	AT_TEST_TRACE("FFT4K pass");

	return 0;
}

AT_DEFINE_TEST(Core_IMDCT) {
	float x[64];
	float costab[512];

	for(int i=0; i<512; ++i) {
		costab[i] = cosf(-nsVDMath::kfTwoPi / 512 * i);
	}

	ATFFTAllocator alloc;
	ATIMDCT imdct;
	imdct.Reserve(alloc, 64, true);
	alloc.Finalize();
	imdct.Bind(alloc);
	for(int i=0; i<64; ++i) {
		for(float& v : x)
			v = 0;

		x[i] = 1.0f;

		float ref[64];
		for(int j=0; j<32; ++j) {
			float sum = 0;

			for(int k = 0; k<64; ++k)
				sum += x[k] * costab[((j*2 + (1 + 64 + 64*2)) * (k*2 + 1)) & 511];

			ref[j] = sum;
		}

		for(int j=0; j<32; ++j) {
			float sum = 0;

			for(int k = 0; k<64; ++k)
				sum += x[k] * costab[((j*2 + (1 + 64 + 32*2)) * (k*2 + 1)) & 511];

			ref[j + 32] = sum;
		}

		imdct.Transform(x);

		float me = 0;
		float msqe = 0;
		float maxe = 0;
		for(int j=0; j<64; ++j) {
			const float e = x[j] - ref[j];
			me += e;
			msqe += e*e;
			maxe = std::max<float>(maxe, fabsf(e));
		}

		me /= 64.0f;
		msqe /= 64.0f;

		AT_TEST_TRACEF("%2u) mean error = %g, mean squared error = %g, max error = %g", i, me, msqe, maxe);

		AT_TEST_ASSERT(fabsf(me) < 1e-3f && msqe < 1e-3f);
	}

	return 0;
}

AT_DEFINE_TEST_NONAUTO(Core_FFTBench) {
	auto testSize = [](auto N) {
		ATFFT<N> fft;
		vdblock<float> indat(N);
		vdblock<float> outdat(N);

		std::fill(indat.begin(), indat.end(), 0.0f);
		std::fill(outdat.begin(), outdat.end(), 0.0f);

		int iterations = 256'000'000 / N;

		{
			auto t0 = VDGetPreciseTick();
			for(int i=0; i<iterations; ++i) {
				fft.Forward(outdat.data(), indat.data());
			}

			auto dt = VDGetPreciseTick() - t0;
			const double usecs = (double)(sint64)dt * VDGetPreciseSecondsPerTick() / (double)iterations * 1000000.0;
			const double mflops = 5.0 * (double)N * log2((double)N) / usecs;

			printf("%7.3fus  %9.1f MFLOPS  %4d-point forward FFT\n", usecs, mflops, (int)N);
		}

		{
			auto t0 = VDGetPreciseTick();
			for(int i=0; i<iterations; ++i) {
				fft.Inverse(outdat.data(), indat.data());
			}

			auto dt = VDGetPreciseTick() - t0;
			const double usecs = (double)(sint64)dt * VDGetPreciseSecondsPerTick() / (double)iterations * 1000000.0;
			const double mflops = 5.0 * (double)N * log2((double)N) / usecs;

			printf("%7.3fus  %9.1f MFLOPS  %4d-point inverse FFT\n", usecs, mflops, (int)N);
		}
	};

	testSize(std::integral_constant<int, 64>());
	testSize(std::integral_constant<int, 128>());
	testSize(std::integral_constant<int, 256>());
	testSize(std::integral_constant<int, 512>());
	testSize(std::integral_constant<int, 1024>());
	testSize(std::integral_constant<int, 2048>());
	testSize(std::integral_constant<int, 4096>());
	testSize(std::integral_constant<int, 8192>());

	return 0;
}

AT_DEFINE_TEST_NONAUTO(Core_FFTBench2) {
	{
		ATFFT<4096> fft;
		vdblock<float> indat(4096);
		vdblock<float> outdat(4096);

		std::fill(indat.begin(), indat.end(), 0.0f);
		std::fill(outdat.begin(), outdat.end(), 0.0f);

		if (*ATTestGetArguments()) {
			for(;;)
				fft.Inverse(outdat.data(), indat.data());
		} else {
			for(;;)
				fft.Forward(outdat.data(), indat.data());
		}
	}

	return 0;
}

AT_DEFINE_TEST_NONAUTO(Core_IMDCTBench) {
	ATFFTAllocator alloc;
	ATIMDCT imdct64;
	ATIMDCT imdct2K;
	imdct64.Reserve(alloc, 64, true);
	imdct2K.Reserve(alloc, 2048, true);
	alloc.Finalize();
	imdct64.Bind(alloc);
	imdct2K.Bind(alloc);

	{
		vdblock<float> dat(64);

		std::fill(dat.begin(), dat.end(), 0.0f);

		auto t0 = VDGetPreciseTick();
		for(int i=0; i<1000000; ++i) {
			imdct64.Transform(dat.data());
		}

		auto dt = VDGetPreciseTick() - t0;
		const double usecs = (sint64)dt * VDGetPreciseSecondsPerTick() / 1000000.0 * 1000000.0;

		printf("%7.3fus  64-point inverse MDCT\n", usecs);
	}
	{
		vdblock<float> dat(2048);

		std::fill(dat.begin(), dat.end(), 0.0f);

		auto t0 = VDGetPreciseTick();
		for(int i=0; i<100000; ++i) {
			imdct2K.Transform(dat.data());
		}

		auto dt = VDGetPreciseTick() - t0;
		const double usecs = (sint64)dt * VDGetPreciseSecondsPerTick() / 100000.0 * 1000000.0;

		printf("%7.3fus  2048-point inverse MDCT\n", usecs);
	}

	return 0;
}
