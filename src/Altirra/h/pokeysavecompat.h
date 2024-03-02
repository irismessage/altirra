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

#ifndef f_AT_POKEYSAVECOMPAT_H
#define f_AT_POKEYSAVECOMPAT_H

#include <vd2/system/refcount.h>

class ATSaveStateReader;
class ATPokeyEmulator;
class IATObjectState;

class ATPokeyEmulatorOldStateLoader {
	ATPokeyEmulatorOldStateLoader(const ATPokeyEmulatorOldStateLoader&) = delete;
	ATPokeyEmulatorOldStateLoader& operator=(const ATPokeyEmulatorOldStateLoader&) = delete;
public:
	ATPokeyEmulatorOldStateLoader(ATPokeyEmulator& emu);
	~ATPokeyEmulatorOldStateLoader();

	void	BeginLoadState(ATSaveStateReader& reader);
	void	LoadStateArch(ATSaveStateReader& reader);
	void	LoadStatePrivate(ATSaveStateReader& reader);
	void	LoadStateResetPrivate(ATSaveStateReader& reader);
	void	EndLoadState(ATSaveStateReader& reader);

private:
	ATPokeyEmulator& mEmu;
	vdrefptr<IATObjectState> mpState;
};

#endif
