//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2019 Avery Lee
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

#include <at/atcore/serialization.h>

class ATSaveState;
class ATSaveStateCPU;
class ATSaveStateCPU816;
class ATSaveStateMemoryBuffer;
class ATSaveStateFirmwareReference;
class ATSaveStateAntic;
class ATSaveStateAnticInternal;
class ATSaveStateGtia;
class ATSaveStateGtiaInternal;
class ATSaveStatePokey;
class ATSaveStatePokeyInternal;
class ATSaveStatePia;
class ATSaveStateCartridge;
class ATSaveStateGtiaRenderer;
class ATSaveStateDisk;
class ATSaveStateSioActiveCommand;
class ATSaveStateSioCommandStep;
class ATSaveStateColorSettings;
class ATSaveStateColorParameters;

void ATSaveRegisterTypes() {
	ATSERIALIZATION_REGISTER(ATSaveState);
	ATSERIALIZATION_REGISTER(ATSaveStateMemoryBuffer);
	ATSERIALIZATION_REGISTER(ATSaveStateFirmwareReference);
	ATSERIALIZATION_REGISTER(ATSaveStateCPU);
	ATSERIALIZATION_REGISTER(ATSaveStateCPU816);
	ATSERIALIZATION_REGISTER(ATSaveStateAntic);
	ATSERIALIZATION_REGISTER(ATSaveStateAnticInternal);
	ATSERIALIZATION_REGISTER(ATSaveStateGtia);
	ATSERIALIZATION_REGISTER(ATSaveStateGtiaInternal);
	ATSERIALIZATION_REGISTER(ATSaveStatePokey);
	ATSERIALIZATION_REGISTER(ATSaveStatePokeyInternal);
	ATSERIALIZATION_REGISTER(ATSaveStatePia);
	ATSERIALIZATION_REGISTER(ATSaveStateCartridge);
	ATSERIALIZATION_REGISTER(ATSaveStateGtiaRenderer);
	ATSERIALIZATION_REGISTER(ATSaveStateDisk);
	ATSERIALIZATION_REGISTER(ATSaveStateSioActiveCommand);
	ATSERIALIZATION_REGISTER(ATSaveStateSioCommandStep);
	ATSERIALIZATION_REGISTER(ATSaveStateColorSettings);
	ATSERIALIZATION_REGISTER(ATSaveStateColorParameters);
}
