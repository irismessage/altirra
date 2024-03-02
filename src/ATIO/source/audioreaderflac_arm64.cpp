//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2021 Avery Lee
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

#if VD_CPU_ARM64
#include <arm64_neon.h>

template<int Order>
void ATFLACReconstructLPC_Narrow_NEON_Impl(sint32 *__restrict y, uint32 n, const sint32 *__restrict lpcCoeffs, int qlpShift) {
	int16x8_t coeffz = vmovq_n_s16(0);
	int16x8_t coeff0 = coeffz;
	int16x8_t coeff1;
	
	// load coefficients and pack to s16
	const auto loadPack = [](const sint32 *p) {
		return vcombine_s16(vqmovn_s32(vld1q_s32(p)), vqmovn_s32(vld1q_s32(p + 4)));
	};

	coeff1 = loadPack(lpcCoeffs);

	if constexpr (Order > 8) {
		coeff0 = coeff1;
		coeff1 = loadPack(lpcCoeffs + 8);
	}

	// left pack the coefficients
	if constexpr ((Order & 7) != 0) {
		coeff1 = vextq_s16(coeff0, coeff1, Order & 7);

		if constexpr (Order > 8)
			coeff0 = vextq_s16(coeffz, coeff0, Order & 7);
	}

	// preload the pipeline with the warm-up samples
	int32x4_t pipez = vmovq_n_s32(0);
	int32x4_t pipe0 = pipez;
	int32x4_t pipe1 = pipez;
	int32x4_t pipe2 = pipez;
	int32x4_t pipe3 = pipez;

	for(uint32 i = 0; i < Order; ++i) {
		const sint16 v = y[i];

		                          pipe3 = vextq_s32(pipe2, pipe3, 3);
		if constexpr (Order >  4) pipe2 = vextq_s32(pipe1, pipe2, 3);
		if constexpr (Order >  8) pipe1 = vextq_s32(pipe0, pipe1, 3);
		if constexpr (Order > 12) pipe0 = vextq_s32(pipez, pipe0, 3);

		                          pipe3 = vmlal_high_n_s16(pipe3, coeff1, v);
		if constexpr (Order >  4) pipe2 = vmlal_n_s16(pipe2, vget_low_s16(coeff1), v);
		if constexpr (Order >  8) pipe1 = vmlal_high_n_s16(pipe1, coeff0, v);
		if constexpr (Order > 12) pipe0 = vmlal_n_s16(pipe0, vget_low_s16(coeff0), v);
	}

	int32x2_t vshift = vmov_n_s32(-qlpShift);

	// run the pipeline -- note that we must specifically NOT using saturating adds here,
	// as we can rely on temporary overflows getting corrected back into range for the final
	// sum pre-shift
	for(uint32 i = Order; i < n; ++i) {
		// apply quantization shift to get prediction, then add residual
		int32x2_t v = vadd_s32(vshl_s32(vget_high_s32(pipe3), vshift), vmov_n_s32(y[i]));

		// write reconstructed value
		y[i] = vget_lane_s32(v, 1);

		// shift pipeline
		                          pipe3 = vextq_s32(pipe2, pipe3, 3);
		if constexpr (Order >  4) pipe2 = vextq_s32(pipe1, pipe2, 3);
		if constexpr (Order >  8) pipe1 = vextq_s32(pipe0, pipe1, 3);
		if constexpr (Order > 12) pipe0 = vextq_s32(pipez, pipe0, 3);

		// add in new sample
		                          pipe3 = vmlal_high_lane_s16(pipe3, coeff1, v, 2);
		if constexpr (Order >  4) pipe2 = vmlal_lane_s16     (pipe2, vget_low_s16(coeff1), v, 2);
		if constexpr (Order >  8) pipe1 = vmlal_high_lane_s16(pipe1, coeff0, v, 2);
		if constexpr (Order > 12) pipe0 = vmlal_lane_s16     (pipe0, vget_low_s16(coeff0), v, 2);
	}
}

void ATFLACReconstructLPC_Narrow_NEON(sint32 *y, uint32 n, const sint32 *lpcCoeffs, int qlpShift, int order) {
	switch(order) {
		case  2: ATFLACReconstructLPC_Narrow_NEON_Impl< 2>(y, n, lpcCoeffs, qlpShift); break;
		case  3: ATFLACReconstructLPC_Narrow_NEON_Impl< 3>(y, n, lpcCoeffs, qlpShift); break;
		case  4: ATFLACReconstructLPC_Narrow_NEON_Impl< 4>(y, n, lpcCoeffs, qlpShift); break;
		case  5: ATFLACReconstructLPC_Narrow_NEON_Impl< 5>(y, n, lpcCoeffs, qlpShift); break;
		case  6: ATFLACReconstructLPC_Narrow_NEON_Impl< 6>(y, n, lpcCoeffs, qlpShift); break;
		case  7: ATFLACReconstructLPC_Narrow_NEON_Impl< 7>(y, n, lpcCoeffs, qlpShift); break;
		case  8: ATFLACReconstructLPC_Narrow_NEON_Impl< 8>(y, n, lpcCoeffs, qlpShift); break;
		case  9: ATFLACReconstructLPC_Narrow_NEON_Impl< 9>(y, n, lpcCoeffs, qlpShift); break;
		case 10: ATFLACReconstructLPC_Narrow_NEON_Impl<10>(y, n, lpcCoeffs, qlpShift); break;
		case 11: ATFLACReconstructLPC_Narrow_NEON_Impl<11>(y, n, lpcCoeffs, qlpShift); break;
		case 12: ATFLACReconstructLPC_Narrow_NEON_Impl<12>(y, n, lpcCoeffs, qlpShift); break;
		case 13: ATFLACReconstructLPC_Narrow_NEON_Impl<13>(y, n, lpcCoeffs, qlpShift); break;
		case 14: ATFLACReconstructLPC_Narrow_NEON_Impl<14>(y, n, lpcCoeffs, qlpShift); break;
		case 15: ATFLACReconstructLPC_Narrow_NEON_Impl<15>(y, n, lpcCoeffs, qlpShift); break;
		case 16: ATFLACReconstructLPC_Narrow_NEON_Impl<16>(y, n, lpcCoeffs, qlpShift); break;
	}
}

VDNOINLINE uint16 ATFLACUpdateCRC16_Crypto(uint16 crc16v, const void *buf, size_t n) {
	const uint8 *__restrict p = (const uint8 *)buf;

	const auto updateSmall = [=](uint8x16_t crc16, const uint8 *__restrict p, size_t n) -> uint8x16_t {
		alignas(16) uint8_t buf[16] {};

		memcpy(buf + 16 - n, p, n);

		uint8x16_t v = vrev64q_u8(vld1q_u8(buf));
		v = vextq_u8(v, v, 8);

		// table of x^8^(i+1) mod P
		static constexpr uint32 k[23] {
			0x0100, 0x8005, 0x8603, 0x8017, 0x9403, 0x807B, 0xF803, 0x8113, 0x1006, 0x8663, 0xE017, 0x9543, 0x407E, 0xFF83, 0x8102, 0x0106, 0x8605, 0x8617, 0x9417, 0x947B, 0xF87B, 0xF913, 0x1116
		};

		//    crc16*x^bits mod P
		//	= loquad(crc16)*x^bits + hiquad(crc16)*x^(bits+64) mod P
		poly64x2_t factors = vreinterpretq_p64_u64(vcombine_u64(vmov_n_u64(k[n-1]), vmov_n_u64(k[n+7])));
		uint8x16_t crc16_lo = vmullq_p64(vreinterpretq_p64_u8(crc16), factors);
		uint8x16_t crc16_hi = vmull_high_p64(vreinterpretq_p64_u8(crc16), factors);
		return veorq_u8(veorq_u8(v, crc16_lo), crc16_hi);
	};

	// load CRC16 to vector reg
	uint8x16_t crc16 = vreinterpretq_u8_u16(vsetq_lane_u16(crc16v, vmovq_n_u16(0), 0));

	// align the start
	const uint32 startOffset = (uint32)((uintptr_t)p & 15);
	if (startOffset) {
		uint32 preAlign = (uint32)std::min<size_t>(16 - startOffset, n);

		crc16 = updateSmall(crc16, p, preAlign);

		p += preAlign;
		n -= preAlign;
	}

	// fold 128 bits (16 bytes) at a time
	static constexpr alignas(16) uint64_t kFoldConst[2] = { 0x0106, 0x1666 };		// x^128 mod P, x^192 mod P
	poly64x2_t k16 = vreinterpretq_p64_u64(vld1q_u64(kFoldConst));
	size_t nv = n >> 4;
	while(nv--) {
		uint8x16_t v = vrev64q_u8(vld1q_u8(p));
		v = vextq_u8(v, v, 8);
		p += 16;

		uint8x16_t crc16_lo = vmullq_p64(vreinterpretq_p64_u8(crc16), k16);
		uint8x16_t crc16_hi = vmull_high_p64(vreinterpretq_p64_u8(crc16), k16);

		crc16 = veorq_u8(veorq_u8(v, crc16_lo), crc16_hi);
	}

	// handle leftover bytes
	if (n & 15)
		crc16 = updateSmall(crc16, p, n & 15);

	const poly64x2_t x64modP = vreinterpretq_p64_u64(vmovq_n_u64(0x8113));
	const poly64x1_t x48modP = vreinterpret_p64_u64(vmov_n_u64(0x807B));
	const poly64x1_t x32modP = vreinterpret_p64_u64(vmov_n_u64(0x8017));

	// fold from 128-bit to 80-bit: crc16[127..64]*(x^64 mod P) + crc16[63..0]
	uint8x16_t crc16_80 = veorq_u8(vreinterpret_u8_u64(vsetq_lane_u64(0, vreinterpret_u64_u8(crc16), 1)), vmull_high_p64(crc16, x64modP));

	// fold from 80-bit to 48-bit: crc16[79..48]*(x^48 mod P) + crc16[47..0]
	poly64x1_t crc16_80_high = vreinterpret_p64_u64(vshr_n_u64(vreinterpret_u64_u8(vget_low_u8(vextq_u8(crc16_80, crc16_80, 2))), 32));
	uint8x8_t crc16_48 = veor_u8(vget_low_u8(crc16_80), vget_low_u8(vmull_p64(crc16_80_high, x48modP)));

	// fold from 48-bit to 32-bit: crc16[47..32]*(x^32 mod P) + crc16[31..0]
	poly64x1_t crc16_48_high = vreinterpret_p64_u16(vset_lane_u16(0, vreinterpret_u16_u64(vshr_n_u64(vreinterpret_u64_u8(crc16_48), 32)), 1));
	uint8x8_t crc16_32 = veor_u8(crc16_48, vget_low_u8(vmull_p64(crc16_48_high, x32modP)));

	// do Barrett reduction from 32-bit to 16-bit
	// T1 = (R / x^16) * (x^32/P)
	// T2 = (T1 / x^16) * P
	// C = R + T2 mod x^16
	poly64x1_t mu = vreinterpret_p64_u64(vmov_n_u64(0x1FFFB));
	poly64x1_t P = vreinterpret_p64_u64(vmov_n_u64(0x18005));
	auto T1 = vget_low_p64(vmull_p64(vreinterpret_p64_u32(vset_lane_u32(0, vshr_n_u32(vreinterpret_u32_u8(crc16_32), 16), 1)), mu));
	auto T2 = vget_low_p64(vmull_p64(vreinterpret_p64_u64(vshr_n_u64(vreinterpret_u64_p64(T1), 16)), P));

	auto crc16final = veor_u16(vreinterpret_u16_u8(crc16_32), vreinterpret_u16_p64(T2));
	return vget_lane_u16(crc16final, 0);
}
#endif
