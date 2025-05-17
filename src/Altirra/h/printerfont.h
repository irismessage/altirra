//	Altirra - Atari 800/800XL emulator
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#ifndef f_AT_PRINTERFONT_H
#define f_AT_PRINTERFONT_H

#include <vd2/system/vdtypes.h>

struct ATPrinterFontDesc {
	uint8 mWidth;
	uint8 mHeight;
	uint8 mCharFirst;
	uint8 mCharLast;
};

template<uint8 T_Width, uint8 T_Height, uint8 T_CharFirst, uint32 T_CharLast>
struct ATPrinterFont {
	static constexpr uint8 kWidth = T_Width;
	static constexpr uint8 kHeight = T_Height;
	static constexpr uint8 kCharFirst = T_CharFirst;
	static constexpr uint8 kCharLast = T_CharLast;

	ATPrinterFontDesc mDesc;
	uint8 mColumns[(T_CharLast - T_CharFirst + 1) * T_Width] {};

	constexpr ATPrinterFont()
		: mDesc { T_Width, T_Height, T_CharFirst, T_CharLast }
	{
	}
};

struct ATPrinterFont820 final : public ATPrinterFont<5, 7, 32, 126> {
	consteval ATPrinterFont820();
};

struct ATPrinterFont820S final : public ATPrinterFont<7, 7, 47, 95> {
	consteval ATPrinterFont820S();
};

struct ATPrinterFont1025 final : public ATPrinterFont<9, 7, 0, 127> {
	consteval ATPrinterFont1025();
};

struct ATPrinterFont1029 final : public ATPrinterFont<5, 7, 0, 127> {
	consteval ATPrinterFont1029();
};

template<uint8 T_Width, uint8 T_Height, uint8 T_CharFirst, uint32 T_CharLast>
struct ATPrinterPropFont {
	static constexpr uint8 kWidth = T_Width;
	static constexpr uint8 kHeight = T_Height;
	static constexpr uint8 kCharFirst = T_CharFirst;
	static constexpr uint8 kCharLast = T_CharLast;

	ATPrinterFontDesc mDesc;
	uint16 mColumns[(T_CharLast - T_CharFirst + 1) * T_Width] {};
	uint8 mAdvanceWidths[T_CharLast - T_CharFirst + 1] {};

	constexpr ATPrinterPropFont()
		: mDesc { T_Width, T_Height, T_CharFirst, T_CharLast }
	{
	}
};

struct ATPrinterFont825Prop final : public ATPrinterPropFont<17, 9, 32, 126> {
	consteval ATPrinterFont825Prop();
};

struct ATPrinterFont825Mono final : public ATPrinterFont<7, 8, 32, 126> {
	consteval ATPrinterFont825Mono();
};

extern const ATPrinterFont820 g_ATPrinterFont820;
extern const ATPrinterFont820S g_ATPrinterFont820S;
extern const ATPrinterFont1025 g_ATPrinterFont1025;
extern const ATPrinterFont1029 g_ATPrinterFont1029;
extern const ATPrinterFont825Prop g_ATPrinterFont825Prop;
extern const ATPrinterFont825Mono g_ATPrinterFont825Mono;

#endif
