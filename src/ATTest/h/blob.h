//	Altirra - Atari 800/800XL/5200 emulator
//	Test module
//	Copyright (C) 2009-2020 Avery Lee
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

#ifndef f_BLOB_H
#define f_BLOB_H

#include <tuple>
#include <vd2/system/vdstl_vectorview.h>

void ATTestInitBlobHandler();
std::tuple<vdvector_view<const uint8>, uint32, uint32> ATTestGetBlob(const wchar_t *s);

#endif
