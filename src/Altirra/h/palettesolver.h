//	Altirra - Atari 800/800XL/5200 emulator
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

#ifndef f_AT_PALETTESOLVER_H
#define f_AT_PALETTESOLVER_H

#include <optional>
#include <vd2/system/vecmath.h>

struct ATColorParams;

// IATColorPaletteSolver
//
// The inverse of the palette generator, it takes a palette and attempts to
// derive generation parameters from it.
//
class IATColorPaletteSolver {
public:
	virtual ~IATColorPaletteSolver() = default;

	enum class Status : uint32 {
		Finished,
		RunningNoImprovement,
		RunningImproved
	};

	virtual void Init(const ATColorParams& initialState, const uint32 palette[256], bool lockHueStart, bool lockGamma) = 0;
	virtual void Reinit(const ATColorParams& initialState) = 0;
	virtual Status Iterate() = 0;

	virtual std::optional<uint32> GetCurrentError() const = 0;
	virtual void GetCurrentSolution(ATColorParams& params) const = 0;
};

IATColorPaletteSolver *ATCreateColorPaletteSolver();

#endif
