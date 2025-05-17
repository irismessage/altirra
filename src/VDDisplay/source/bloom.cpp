//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2024 Avery Lee
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
#include <numeric>
#include <vd2/VDDisplay/display.h>
#include <vd2/VDDisplay/internal/bloom.h>
#include <vd2/VDDisplay/internal/options.h>

uint32 g_VDDispBloomCoeffsChanged = 0;
VDDBloomV2Settings g_VDDispBloomV2Settings;

void VDDSetBloomV2Settings(const VDDBloomV2Settings& settings) {
	g_VDDispBloomV2Settings = settings;
	++g_VDDispBloomCoeffsChanged;
}

VDDBloomV2RenderParams VDDComputeBloomV2Parameters(const VDDBloomV2ControlParams& controlParams) {
	VDDBloomV2RenderParams renderParams {};

	float wid = g_VDDispBloomV2Settings.mCoeffWidthBase
		+ g_VDDispBloomV2Settings.mCoeffWidthBaseSlope * controlParams.mBaseRadius
		+ g_VDDispBloomV2Settings.mCoeffWidthAdjustSlope * controlParams.mAdjustRadius
		;

	float pyramidWeights[6] {
		powf( 2.0f, 0*wid),
		powf( 2.0f, 1*wid),
		powf( 2.0f, 2*wid),
		powf( 2.0f, 3*wid),
		powf( 2.0f, 4*wid),
		powf( 2.0f, 5*wid),
	};

	// normalize weights
	float pyramidWeightScale = controlParams.mIndirectIntensity / std::accumulate(std::begin(pyramidWeights), std::end(pyramidWeights), 0.0f);

	for(float& weight : pyramidWeights)
		weight *= pyramidWeightScale;

	float runningScale = 1.0f;

	for(int i=4; i>=0; --i) {
		vdfloat2 blendFactors {
			(i == 4 ? pyramidWeights[i + 1] : 1.0f) * runningScale,
			pyramidWeights[i]
		};

		float blendFactorSum = blendFactors.x + blendFactors.y;

		runningScale = std::clamp<float>(
			blendFactorSum,
			0.01f, 100.0f);

		renderParams.mPassBlendFactors[4 - i] = blendFactors / runningScale;
	}

	renderParams.mPassBlendFactors[5] = vdfloat2 {
		runningScale,
		controlParams.mDirectIntensity
	};

	const auto cubic =
		[](float x1, float m1, float y1, float x2, float m2, float y2) -> vdfloat4 {
			if (x2 - x1 < 1e-5f)
				return vdfloat4{0, 0, 0, y1};

			float dx = x2 - x1;

			m1 *= dx;
			m2 *= dx;

			// compute cubic spline over 0-1
			vdfloat4 curve {
				2*(y1 - y2) + (m1 + m2),
				3*(y2 - y1) - 2*m1 - m2,
				m1,
				y1
			};

			// scale by dx (substitute t = s/dx)
			float idx = 1.0f / dx;
			float idx2 = idx*idx;
			float idx3 = idx2*idx;

			curve.x *= idx3;
			curve.y *= idx2;
			curve.z *= idx;

			// translate by x1 (substitute s = q - x1)
			// f(x) = A(x-x1)^3 + B(x-x1)^2 + C(x-x1) + D
			//      = A(x^3 - 3x^2*x1 + 3x*x1^2 - x1^3) + B(x^2-2x*x1+x1^2) + C(x-x1) + D
			//      = Ax^3 - 3Ax^2*x1 + 3Ax*x1^2 - Ax1^3 + Bx^2 - 2Bx*x1 + Bx1^2) + Cx - Cx1 + D
			//      = Ax^3 + (-3Ax1 + B)x^2 + (3Ax1^2 - 2Bx1 + C)x + (-Ax1^3 + Bx1^2 - Cx1 + D)
			float x1_2 = x1*x1;
			float x1_3 = x1_2*x1;
			return vdfloat4 {
				curve.x,
				-3*curve.x*x1 + curve.y,
				3*curve.x*x1_2 - 2*curve.y*x1 + curve.z,
				-curve.x*x1_3 + curve.y*x1_2 - curve.z*x1 + curve.w
			};
		};

	// Shoulder conditions:
	//	f(0) = 0 --> D = 0
	//	f'(0) = m2 --> C = m2
	//	f(1) = 1 --> A+B = -m2
	//	f'(1) = 0 --> 3A+2B = -m2
	//
	// A = m2
	// B = -m2
	// C = m2
	// D = 0
	//
	// t = (x - shoulderX)/(limitX - shoulderX)

	// f(x1) = y1	=> Ax1^3 + Bx1^2 + Cx1 + D = y1
	// f(x2) = y2	=> Ax2^3 + Bx2^2 + Cx2 + D = y2
	// f'(x1) = m1	=> 3Ax1^2 + 2Bx + C = m1
	// f'(x2) = m2	=> 3Ax1^2 + 2Bx + C = m2

	float limitX = std::max<float>(g_VDDispBloomV2Settings.mLimitX, 0.1f);
	float limitSlope = g_VDDispBloomV2Settings.mLimitSlope;
	float shoulderX = std::clamp<float>(g_VDDispBloomV2Settings.mShoulderX, 0.0f, limitX);
	float shoulderY = std::clamp<float>(g_VDDispBloomV2Settings.mShoulderY, 0.0f, 1.0f);
	float midSlope = shoulderX > 0 ? shoulderY / shoulderX : 1.0f;

	if (controlParams.mbRenderLinear) {
		renderParams.mShoulder = vdfloat4{};
		renderParams.mThresholds = vdfloat4 { midSlope, 100.0f, 100.0f, 0.0f };
	} else {
		renderParams.mShoulder = cubic(shoulderX, midSlope, shoulderY, limitX, limitSlope, 1.0f);
		renderParams.mThresholds = vdfloat4 { midSlope, shoulderX, limitX, 0.0f };
	}

	return renderParams;
}
