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

#ifndef f_AT_SAVESTATEIO_H
#define f_AT_SAVESTATEIO_H

#include <vd2/system/vdtypes.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/serialization.h>

class VDStringSpanA;
class VDStringSpanW;
class IVDZipArchiveWriter;
class VDZipArchive;

class IATSaveStateSerializer : public IATSerializationOutput {
public:
	virtual ~IATSaveStateSerializer() = default;

	virtual void Serialize(IVDStream& stream, IATSerializable& object, const wchar_t *packageType) = 0;
	virtual void Serialize(IVDZipArchiveWriter& zip, IATSerializable& snapshot) = 0;
};

class IATSaveStateDeserializer : public IATSerializationInput {
public:
	virtual ~IATSaveStateDeserializer() = default;

	virtual void Deserialize(IVDRandomAccessStream& stream, IATSerializable **snapshot) = 0;
	virtual void Deserialize(VDZipArchive& zip, IATSerializable **snapshot) = 0;
};

IATSaveStateSerializer *ATCreateSaveStateSerializer();
IATSaveStateDeserializer *ATCreateSaveStateDeserializer();

#endif
