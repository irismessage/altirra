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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#ifndef f_AT_ATCORE_SERIALIZABLE_H
#define f_AT_ATCORE_SERIALIZABLE_H

#include <vd2/system/refcount.h>

class IVDStream;
class ATDeserializer;
class ATSerializer;
struct ATSerializationTypeDef;

enum class ATSerializationTypeToken : uint32 {
	Invalid = 0
};

// IATSerializable
//
// Object holding captured state from a live object. A save state is a
// graph of these objects, which may either be reapplied to a runtime
// object or serialized for persistence on disk.
//
class IATSerializable : public IVDRefCount {
public:
	virtual const ATSerializationTypeDef& GetSerializationType() const = 0;

	// Return path if this object should be packaged as a top-level resource. The path may
	// include subdirectories with slashes. The implementation will modify the name as
	// necessary to avoid conflicts. A direct packaged object is 
	virtual const wchar_t *GetDirectPackagingPath() const = 0;

	virtual void Deserialize(ATDeserializer& reader) = 0;
	virtual void DeserializeDirect(IVDStream& stream, uint32 len) = 0;

	// Write the contents of the snap object to 
	virtual void Serialize(ATSerializer& writer) const = 0;
	virtual void SerializeDirect(IVDStream& stream) const = 0;
};

#endif
