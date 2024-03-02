//	VirtualDub - Video processing and capture application
//	A/V interface library
//	Copyright (C) 1998-2011 Avery Lee
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

extern Texture2D<float4> srct : register(t0);
extern SamplerState srcs : register(s0);

void VS(
	float2 pos : POSITION,
	out float4 oPos : SV_Position,
	out float2 oT0 : TEXCOORD0)
{
	oPos = float4(pos * float2(2, -2) + float2(-1, 1), 0, 1);
	oT0 = pos;
}

float4 PS(float4 pos : SV_Position, float2 t0 : TEXCOORD0) : SV_Target {
	return srct.Sample(srcs, t0).bgra;
}
