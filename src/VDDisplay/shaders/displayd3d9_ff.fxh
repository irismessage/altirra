//	VirtualDub - Video processing and capture application
//	A/V interface library
//	Copyright (C) 1998-2008 Avery Lee
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

#ifndef DISPLAYDX9_FF_FXH
#define DISPLAYDX9_FF_FXH

#define FF_STAGE(n, ca1, cop, ca2, aa1, aop, aa2)	\
	ColorOp[n] = cop;	\
	ColorArg1[n] = ca1;	\
	ColorArg2[n] = ca2;	\
	AlphaOp[n] = aop;	\
	AlphaArg1[n] = aa1;	\
	AlphaArg2[n] = aa2

#define FF_STAGE_DISABLE(n)	\
	ColorOp[n] = Disable;	\
	AlphaOp[n] = Disable

////////////////////////////////////////////////////////////////////////////////
technique point_ff {
	pass p0 <
		bool vd_clippos = true;
	> {
		FF_STAGE(0, Texture, SelectArg1, Diffuse, Texture, SelectArg1, Diffuse);
		FF_STAGE_DISABLE(1);
		FF_STAGE_DISABLE(2);
		MinFilter[0] = Point;
		MagFilter[0] = Point;
		MipFilter[0] = Point;
		AddressU[0] = Clamp;
		AddressV[0] = Clamp;
		Texture[0] = <vd_srctexture>;
		AlphaBlendEnable = false;
	}
}

////////////////////////////////////////////////////////////////////////////////
technique bilinear_ff {
	pass p0 <
		bool vd_clippos = true;
	> {
		FF_STAGE(0, Texture, SelectArg1, Diffuse, Texture, SelectArg1, Diffuse);
		FF_STAGE_DISABLE(1);
		FF_STAGE_DISABLE(2);
		MinFilter[0] = Linear;
		MagFilter[0] = Linear;
		MipFilter[0] = Linear;
		AddressU[0] = Clamp;
		AddressV[0] = Clamp;
		Texture[0] = <vd_srctexture>;
		AlphaBlendEnable = false;
	}
}

#endif
