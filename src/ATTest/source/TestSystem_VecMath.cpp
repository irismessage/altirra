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
#include <vd2/system/vecmath.h>
#include "test.h"

DEFINE_TEST(System_VecMath) {
	const auto approxnear = [](auto a, auto b) {
		return dot(a - b, a - b) < 1e-10f;
	};

	AT_TEST_ASSERT( (vdmask32x3::set(true, false, true) == vdmask32x3::set(true, false, true)));
	AT_TEST_ASSERT(!(vdmask32x3::set(true, false, true) == vdmask32x3::set(true, false, false)));
	AT_TEST_ASSERT(!(vdmask32x3::set(true, false, true) == vdmask32x3::set(true, true, true)));
	AT_TEST_ASSERT(!(vdmask32x3::set(true, false, true) == vdmask32x3::set(false, false, true)));

	AT_TEST_ASSERT(!(vdmask32x3::set(true, false, true) != vdmask32x3::set(true, false, true)));
	AT_TEST_ASSERT( (vdmask32x3::set(true, false, true) != vdmask32x3::set(true, false, false)));
	AT_TEST_ASSERT( (vdmask32x3::set(true, false, true) != vdmask32x3::set(true, true, true)));
	AT_TEST_ASSERT( (vdmask32x3::set(true, false, true) != vdmask32x3::set(false, false, true)));

	AT_TEST_ASSERT(~vdmask32x3::set(true, false, true) == vdmask32x3::set(false, true, false));
	AT_TEST_ASSERT((vdmask32x3::set(true, false, true) & vdmask32x3::set(true, true, false)) == vdmask32x3::set(true, false, false));
	AT_TEST_ASSERT((vdmask32x3::set(true, false, true) | vdmask32x3::set(true, true, false)) == vdmask32x3::set(true, true, true));
	AT_TEST_ASSERT((vdmask32x3::set(true, false, true) ^ vdmask32x3::set(true, true, false)) == vdmask32x3::set(false, true, true));

	////////////////////////////////////////////////////////////////////////////

	AT_TEST_ASSERT( (vdmask32x4::set(true, false, true, false) == vdmask32x4::set(true, false, true, false)));
	AT_TEST_ASSERT(!(vdmask32x4::set(true, false, true, false) == vdmask32x4::set(true, false, true, true)));
	AT_TEST_ASSERT(!(vdmask32x4::set(true, false, true, false) == vdmask32x4::set(true, false, false, false)));
	AT_TEST_ASSERT(!(vdmask32x4::set(true, false, true, false) == vdmask32x4::set(true, true, true, false)));
	AT_TEST_ASSERT(!(vdmask32x4::set(true, false, true, false) == vdmask32x4::set(false, false, true, false)));

	AT_TEST_ASSERT(!(vdmask32x4::set(true, false, true, false) != vdmask32x4::set(true, false, true, false)));
	AT_TEST_ASSERT( (vdmask32x4::set(true, false, true, false) != vdmask32x4::set(true, false, true, true)));
	AT_TEST_ASSERT( (vdmask32x4::set(true, false, true, false) != vdmask32x4::set(true, false, false, false)));
	AT_TEST_ASSERT( (vdmask32x4::set(true, false, true, false) != vdmask32x4::set(true, true, true, false)));
	AT_TEST_ASSERT( (vdmask32x4::set(true, false, true, false) != vdmask32x4::set(false, false, true, false)));

	AT_TEST_ASSERT(~vdmask32x4::set(true, false, true, false) == vdmask32x4::set(false, true, false, true));
	AT_TEST_ASSERT((vdmask32x4::set(true, false, true, false) & vdmask32x4::set(true, true, false, false)) == vdmask32x4::set(true, false, false, false));
	AT_TEST_ASSERT((vdmask32x4::set(true, false, true, false) | vdmask32x4::set(true, true, false, false)) == vdmask32x4::set(true, true, true, false));
	AT_TEST_ASSERT((vdmask32x4::set(true, false, true, false) ^ vdmask32x4::set(true, true, false, false)) == vdmask32x4::set(false, true, true, false));

	////////////////////////////////////////////////////////////////////////////

	AT_TEST_ASSERT( (vdfloat32x3::set(1, 2, 3) == vdfloat32x3::set(1, 2, 3)));
	AT_TEST_ASSERT(!(vdfloat32x3::set(1, 2, 3) == vdfloat32x3::set(1, 2, 4)));
	AT_TEST_ASSERT(!(vdfloat32x3::set(1, 2, 3) == vdfloat32x3::set(1, 4, 3)));
	AT_TEST_ASSERT(!(vdfloat32x3::set(1, 2, 3) == vdfloat32x3::set(4, 2, 3)));

	AT_TEST_ASSERT( (vdfloat32x3::set(1, 2, 3) != vdfloat32x3::set(1, 2, 4)));
	AT_TEST_ASSERT( (vdfloat32x3::set(1, 2, 3) != vdfloat32x3::set(1, 4, 3)));
	AT_TEST_ASSERT( (vdfloat32x3::set(1, 2, 3) != vdfloat32x3::set(4, 2, 3)));
	AT_TEST_ASSERT(!(vdfloat32x3::set(1, 2, 3) != vdfloat32x3::set(1, 2, 3)));

	AT_TEST_ASSERT( (+vdfloat32x3::set(1, 2, 3) == vdfloat32x3::set(1, 2, 3)));
	AT_TEST_ASSERT( (-vdfloat32x3::set(1, 2, 3) == vdfloat32x3::set(-1, -2, -3)));
	AT_TEST_ASSERT( (vdfloat32x3::set(1, 2, 3) + vdfloat32x3::set(4, 5, 6) == vdfloat32x3::set(5, 7, 9)));
	AT_TEST_ASSERT( (vdfloat32x3::set(1, 2, 3) - vdfloat32x3::set(4, 5, 6) == vdfloat32x3::set(-3, -3, -3)));
	AT_TEST_ASSERT( (vdfloat32x3::set(1, 2, 3) * vdfloat32x3::set(4, 5, 6) == vdfloat32x3::set(4, 10, 18)));
	AT_TEST_ASSERT( approxnear(vdfloat32x3::set(1, 2, 3) / vdfloat32x3::set(4, 5, 6), vdfloat32x3::set(0.25f, 0.4f, 0.5f)));
	AT_TEST_ASSERT( (vdfloat32x3::set(1, 2, 3) + 2 == vdfloat32x3::set(3, 4, 5)));
	AT_TEST_ASSERT( (vdfloat32x3::set(1, 2, 3) - 2 == vdfloat32x3::set(-1, 0, 1)));
	AT_TEST_ASSERT( (vdfloat32x3::set(1, 2, 3) * 2 == vdfloat32x3::set(2, 4, 6)));
	AT_TEST_ASSERT( (vdfloat32x3::set(1, 2, 3) / 2 == vdfloat32x3::set(0.5f, 1.0f, 1.5f)));
	AT_TEST_ASSERT( (6 + vdfloat32x3::set(1, 2, 3) == vdfloat32x3::set(7, 8, 9)));
	AT_TEST_ASSERT( (6 - vdfloat32x3::set(1, 2, 3) == vdfloat32x3::set(5, 4, 3)));
	AT_TEST_ASSERT( (6 * vdfloat32x3::set(1, 2, 3) == vdfloat32x3::set(6, 12, 18)));
	AT_TEST_ASSERT( approxnear(6 / vdfloat32x3::set(1, 2, 3), vdfloat32x3::set(6.0f, 3.0f, 2.0f)));

	vdfloat32x3 v3;
	v3 = vdfloat32x3::set(1, 2, 3); AT_TEST_ASSERT( ((v3 += vdfloat32x3::set(4, 5, 6)) == vdfloat32x3::set(5, 7, 9)));
	v3 = vdfloat32x3::set(1, 2, 3); AT_TEST_ASSERT( ((v3 -= vdfloat32x3::set(4, 5, 6)) == vdfloat32x3::set(-3, -3, -3)));
	v3 = vdfloat32x3::set(1, 2, 3); AT_TEST_ASSERT( ((v3 *= vdfloat32x3::set(4, 5, 6)) == vdfloat32x3::set(4, 10, 18)));
	v3 = vdfloat32x3::set(1, 2, 3); AT_TEST_ASSERT( approxnear((v3 /= vdfloat32x3::set(4, 5, 6)), vdfloat32x3::set(0.25f, 0.4f, 0.5f)));
	v3 = vdfloat32x3::set(1, 2, 3); AT_TEST_ASSERT( ((v3 += 2) == vdfloat32x3::set(3, 4, 5)));
	v3 = vdfloat32x3::set(1, 2, 3); AT_TEST_ASSERT( ((v3 -= 2) == vdfloat32x3::set(-1, 0, 1)));
	v3 = vdfloat32x3::set(1, 2, 3); AT_TEST_ASSERT( ((v3 *= 2) == vdfloat32x3::set(2, 4, 6)));
	v3 = vdfloat32x3::set(1, 2, 3); AT_TEST_ASSERT( approxnear((v3 /= 2), vdfloat32x3::set(0.5f, 1.0f, 1.5f)));

	AT_TEST_ASSERT(cmplt(vdfloat32x3::set(1,0,1), vdfloat32x3::set(1,1,0)) == vdmask32x3::set(false, true, false));
	AT_TEST_ASSERT(cmple(vdfloat32x3::set(1,0,1), vdfloat32x3::set(1,1,0)) == vdmask32x3::set(true, true, false));
	AT_TEST_ASSERT(cmpgt(vdfloat32x3::set(1,0,1), vdfloat32x3::set(1,1,0)) == vdmask32x3::set(false, false, true));
	AT_TEST_ASSERT(cmpge(vdfloat32x3::set(1,0,1), vdfloat32x3::set(1,1,0)) == vdmask32x3::set(true, false, true));
	AT_TEST_ASSERT(cmpeq(vdfloat32x3::set(1,0,1), vdfloat32x3::set(1,1,0)) == vdmask32x3::set(true, false, false));
	AT_TEST_ASSERT(cmpne(vdfloat32x3::set(1,0,1), vdfloat32x3::set(1,1,0)) == vdmask32x3::set(false, true, true));

	////////////////////////////////////////////////////////////////////////////

	AT_TEST_ASSERT( (vdfloat32x4::set(1, 2, 3, 4) == vdfloat32x4::set(1, 2, 3, 4)));
	AT_TEST_ASSERT(!(vdfloat32x4::set(1, 2, 3, 4) == vdfloat32x4::set(1, 2, 3, 5)));
	AT_TEST_ASSERT(!(vdfloat32x4::set(1, 2, 3, 4) == vdfloat32x4::set(1, 2, 5, 4)));
	AT_TEST_ASSERT(!(vdfloat32x4::set(1, 2, 3, 4) == vdfloat32x4::set(1, 5, 3, 4)));
	AT_TEST_ASSERT(!(vdfloat32x4::set(1, 2, 3, 4) == vdfloat32x4::set(5, 2, 3, 4)));

	AT_TEST_ASSERT( (vdfloat32x4::set(1, 2, 3, 4) != vdfloat32x4::set(1, 2, 3, 5)));
	AT_TEST_ASSERT( (vdfloat32x4::set(1, 2, 3, 4) != vdfloat32x4::set(1, 2, 5, 4)));
	AT_TEST_ASSERT( (vdfloat32x4::set(1, 2, 3, 4) != vdfloat32x4::set(1, 5, 3, 4)));
	AT_TEST_ASSERT( (vdfloat32x4::set(1, 2, 3, 4) != vdfloat32x4::set(5, 2, 3, 4)));
	AT_TEST_ASSERT(!(vdfloat32x4::set(1, 2, 3, 4) != vdfloat32x4::set(1, 2, 3, 4)));

	AT_TEST_ASSERT( (+vdfloat32x4::set(1, 2, 3, 4) == vdfloat32x4::set(1, 2, 3, 4)));
	AT_TEST_ASSERT( (-vdfloat32x4::set(1, 2, 3, 4) == vdfloat32x4::set(-1, -2, -3, -4)));
	AT_TEST_ASSERT( (vdfloat32x4::set(1, 2, 3, 4) + vdfloat32x4::set(5, 6, 7, 8) == vdfloat32x4::set(6, 8, 10, 12)));
	AT_TEST_ASSERT( (vdfloat32x4::set(1, 2, 3, 4) - vdfloat32x4::set(5, 6, 7, 8) == vdfloat32x4::set(-4, -4, -4, -4)));
	AT_TEST_ASSERT( (vdfloat32x4::set(1, 2, 3, 4) * vdfloat32x4::set(5, 6, 7, 8) == vdfloat32x4::set(5, 12, 21, 32)));
	AT_TEST_ASSERT( approxnear(vdfloat32x4::set(1, 4, 2, 32) / vdfloat32x4::set(2, 4, 8, 16), vdfloat32x4::set(0.5f, 1.0f, 0.25f, 2.0f)));
	AT_TEST_ASSERT( (vdfloat32x4::set(1, 2, 3, 4) + 2 == vdfloat32x4::set(3, 4, 5, 6)));
	AT_TEST_ASSERT( (vdfloat32x4::set(1, 2, 3, 4) - 2 == vdfloat32x4::set(-1, 0, 1, 2)));
	AT_TEST_ASSERT( (vdfloat32x4::set(1, 2, 3, 4) * 2 == vdfloat32x4::set(2, 4, 6, 8)));
	AT_TEST_ASSERT( approxnear(vdfloat32x4::set(1, 2, 3, 4) / 2, vdfloat32x4::set(0.5f, 1.0f, 1.5f, 2.0f)));
	AT_TEST_ASSERT( (6 + vdfloat32x4::set(1, 2, 3, 4) == vdfloat32x4::set(7, 8, 9, 10)));
	AT_TEST_ASSERT( (6 - vdfloat32x4::set(1, 2, 3, 4) == vdfloat32x4::set(5, 4, 3, 2)));
	AT_TEST_ASSERT( (6 * vdfloat32x4::set(1, 2, 3, 4) == vdfloat32x4::set(6, 12, 18, 24)));
	AT_TEST_ASSERT( approxnear(6 / vdfloat32x4::set(1, 2, 3, 4), vdfloat32x4::set(6.0f, 3.0f, 2.0f, 1.5f)));

	vdfloat32x4 v4;
	v4 = vdfloat32x4::set(1, 2, 3, 4); AT_TEST_ASSERT( ((v4 += vdfloat32x4::set(5, 6, 7, 8)) == vdfloat32x4::set(6, 8, 10, 12)));
	v4 = vdfloat32x4::set(1, 2, 3, 4); AT_TEST_ASSERT( ((v4 -= vdfloat32x4::set(5, 6, 7, 8)) == vdfloat32x4::set(-4, -4, -4, -4)));
	v4 = vdfloat32x4::set(1, 2, 3, 4); AT_TEST_ASSERT( ((v4 *= vdfloat32x4::set(5, 6, 7, 8)) == vdfloat32x4::set(5, 12, 21, 32)));
	v4 = vdfloat32x4::set(1, 4, 2, 32); AT_TEST_ASSERT( approxnear((v4 /= vdfloat32x4::set(2, 4, 8, 16)), vdfloat32x4::set(0.5f, 1.0f, 0.25f, 2.0f)));
	v4 = vdfloat32x4::set(1, 2, 3, 4); AT_TEST_ASSERT( ((v4 += 2) == vdfloat32x4::set(3, 4, 5, 6)));
	v4 = vdfloat32x4::set(1, 2, 3, 4); AT_TEST_ASSERT( ((v4 -= 2) == vdfloat32x4::set(-1, 0, 1, 2)));
	v4 = vdfloat32x4::set(1, 2, 3, 4); AT_TEST_ASSERT( ((v4 *= 2) == vdfloat32x4::set(2, 4, 6, 8)));
	v4 = vdfloat32x4::set(1, 2, 3, 4); AT_TEST_ASSERT( ((v4 /= 2) == vdfloat32x4::set(0.5f, 1.0f, 1.5f, 2.0f)));

	AT_TEST_ASSERT(cmplt(vdfloat32x4::set(1,0,1,0), vdfloat32x4::set(1,1,0,0)) == vdmask32x4::set(false, true, false, false));
	AT_TEST_ASSERT(cmple(vdfloat32x4::set(1,0,1,0), vdfloat32x4::set(1,1,0,0)) == vdmask32x4::set(true, true, false, true));
	AT_TEST_ASSERT(cmpgt(vdfloat32x4::set(1,0,1,0), vdfloat32x4::set(1,1,0,0)) == vdmask32x4::set(false, false, true, false));
	AT_TEST_ASSERT(cmpge(vdfloat32x4::set(1,0,1,0), vdfloat32x4::set(1,1,0,0)) == vdmask32x4::set(true, false, true, true));
	AT_TEST_ASSERT(cmpeq(vdfloat32x4::set(1,0,1,0), vdfloat32x4::set(1,1,0,0)) == vdmask32x4::set(true, false, false, true));
	AT_TEST_ASSERT(cmpne(vdfloat32x4::set(1,0,1,0), vdfloat32x4::set(1,1,0,0)) == vdmask32x4::set(false, true, true, false));

	////////////////////////////////////////////////////////////////////////////

	AT_TEST_ASSERT(all_bool(vdmask32x3::set(false, false, false)) == false);
	AT_TEST_ASSERT(all_bool(vdmask32x3::set(true, true, false)) == false);
	AT_TEST_ASSERT(all_bool(vdmask32x3::set(true, false, true)) == false);
	AT_TEST_ASSERT(all_bool(vdmask32x3::set(false, true, true)) == false);
	AT_TEST_ASSERT(all_bool(vdmask32x3::set(true, true, true)) == true);

	AT_TEST_ASSERT(all_bool(vdmask32x4::set(false, false, false, false)) == false);
	AT_TEST_ASSERT(all_bool(vdmask32x4::set(true, true, true, false)) == false);
	AT_TEST_ASSERT(all_bool(vdmask32x4::set(true, true, false, true)) == false);
	AT_TEST_ASSERT(all_bool(vdmask32x4::set(true, false, true, true)) == false);
	AT_TEST_ASSERT(all_bool(vdmask32x4::set(false, true, true, true)) == false);
	AT_TEST_ASSERT(all_bool(vdmask32x4::set(true, true, true, true)) == true);

	AT_TEST_ASSERT(abs(vdfloat32x3::set(-1.0f, 0.0f, 1.0f)) == vdfloat32x3::set(1.0f, 0.0f, 1.0f));
	AT_TEST_ASSERT(abs(vdfloat32x4::set(-1.0f, 0.0f, 1.0f, -2.0f)) == vdfloat32x4::set(1.0f, 0.0f, 1.0f, 2.0f));

	AT_TEST_ASSERT(maxcomponent(vdfloat32x3::set(-1.0f, -2.0f, -3.0f)) == -1.0f);
	AT_TEST_ASSERT(maxcomponent(vdfloat32x3::set(1.0f, 2.0f, 3.0f)) == 3.0f);
	AT_TEST_ASSERT(maxcomponent(vdfloat32x4::set(-1.0f, -2.0f, -3.0f, -4.0f)) == -1.0f);
	AT_TEST_ASSERT(maxcomponent(vdfloat32x4::set(1.0f, 2.0f, 3.0f, 4.0f)) == 4.0f);

	AT_TEST_ASSERT(dot(vdfloat32x3::set(1.0f, 2.0f, 3.0f), vdfloat32x3::set(5.0f, 6.0f, 7.0f)) == 38.0f);
	AT_TEST_ASSERT(dot(vdfloat32x4::set(1.0f, 2.0f, 3.0f, 4.0f), vdfloat32x4::set(5.0f, 6.0f, 7.0f, 8.0f)) == 70.0f);

	AT_TEST_ASSERT(saturate(vdfloat32x3::set(-0.5f, 0.0f, 0.5f)) == vdfloat32x3::set(0.0f, 0.0f, 0.5f));
	AT_TEST_ASSERT(saturate(vdfloat32x3::set(0.5f, 1.0f, 1.5f)) == vdfloat32x3::set(0.5f, 1.0f, 1.0f));
	AT_TEST_ASSERT(saturate(vdfloat32x4::set(-0.5f, 0.0f, 0.5f, 1.0f)) == vdfloat32x4::set(0.0f, 0.0f, 0.5f, 1.0f));
	AT_TEST_ASSERT(saturate(vdfloat32x4::set(0.5f, 1.0f, 1.5f, 2.0f)) == vdfloat32x4::set(0.5f, 1.0f, 1.0f, 1.0f));

	AT_TEST_ASSERT(packus8(vdfloat32x3::set(-1, 0, 1)) == 0x010000);
	AT_TEST_ASSERT(packus8(vdfloat32x3::set(254.4f, 254.6f, 255)) == 0xFFFFFE);

	AT_TEST_ASSERT(packus8(vdfloat32x4::set(-1, 0, 1, 2)) == 0x02010000);
	AT_TEST_ASSERT(packus8(vdfloat32x4::set(254.4f, 254.6f, 255, 256)) == 0xFFFFFFFE);

	AT_TEST_ASSERT(ceilint(vdfloat32x4::set(-2.0f, -1.9f, -1.1f, -1.0f)) == vdint32x4::set(-2, -1, -1, -1));
	AT_TEST_ASSERT(ceilint(vdfloat32x4::set(-1.0f, -0.9f, -0.1f, -0.0f)) == vdint32x4::set(-1,  0,  0,  0));
	AT_TEST_ASSERT(ceilint(vdfloat32x4::set( 0.0f,  0.1f,  0.9f,  1.0f)) == vdint32x4::set( 0,  1,  1,  1));
	AT_TEST_ASSERT(ceilint(vdfloat32x4::set( 1.0f,  1.1f,  1.9f,  2.0f)) == vdint32x4::set( 1,  2,  2,  2));
	
	////////////////////////////////////////////////////////////////////////////

	return 0;
}
