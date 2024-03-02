//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2023 Avery Lee
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

#ifndef f_AT_TRACEFILEFORMAT_H
#define f_AT_TRACEFILEFORMAT_H

#include <at/atcore/enumparse.h>
#include <at/atcore/snapshotimpl.h>

class ATSavedTraceChannel;
class ATSavedTraceGroup;
class ATSaveStateMemoryBuffer;

enum class ATSavedTraceCPUColumnType : uint8 {
	None,
	A,
	X,
	Y,
	S,
	P,
	PC,
	Opcode,
	Cycle,
	UnhaltedCycle,
	Irq,
	Nmi,
	EffectiveAddress,
	GlobalPCBase,
};

AT_DECLARE_ENUM_TABLE(ATSavedTraceCPUColumnType);

class ATSavedTraceRoot final : public ATSnapExchangeObject<ATSavedTraceRoot, "ATSavedTraceRoot"> {
public:
	template<ATExchanger T>
	void Exchange(T& rw);

	vdvector<vdrefptr<ATSavedTraceGroup>> mGroups;
};

class ATSavedTraceGroup final : public ATSnapExchangeObject<ATSavedTraceGroup, "ATSavedTraceGroup"> {
public:
	template<ATExchanger T>
	void Exchange(T& rw);

	VDStringW mName;
	vdvector<vdrefptr<ATSavedTraceChannel>> mChannels;
};

class ATSavedTraceChannel final : public ATSnapExchangeObject<ATSavedTraceChannel, "ATSavedTraceChannel"> {
public:
	template<ATExchanger T>
	void Exchange(T& rw);

	VDStringW mName;
	vdrefptr<IATSerializable> mpDetail;
};

class ATSavedTraceFrameChannelDetail final : public ATSnapExchangeObject<ATSavedTraceFrameChannelDetail, "ATSavedTraceFrameChannelDetail"> {
public:
	template<ATExchanger T>
	void Exchange(T& rw);

	double mTicksPerSecond = 0;

	struct FrameBoundaryInfo {
		uint32 mOffset = 0;
		uint32 mPeriod = 0;
		uint32 mCount = 0;

		template<typename T_Self, ATExchanger T_Ex>
		static void Exchange(T_Self& self, T_Ex& rw);

		void Exchange(ATSerializer& ser) const { Exchange(*this, ser); }
		void Exchange(ATDeserializer& deser) { Exchange(*this, deser); }
	};

	vdfastvector<FrameBoundaryInfo> mFrameBoundaries;
};

////////////////////////////////////////////////////////////////////////////////

struct ATSavedTraceCPUColumnInfo {
	ATSavedTraceCPUColumnType mType = {};
	uint32 mBitOffset = 0;
	uint8 mBitWidth = 0;

	bool operator==(const ATSavedTraceCPUColumnInfo&) const = default;

	template<typename T_Self, ATExchanger T_Ex>
	static void Exchange(T_Self& self, T_Ex& rw);

	void Exchange(ATSerializer& ser) const { Exchange(*this, ser); }
	void Exchange(ATDeserializer& deser) { Exchange(*this, deser); }
};

////////////////////////////////////////////////////////////////////////////////

class ATSavedTraceCPUChannelDetail final : public ATSnapExchangeObject<ATSavedTraceCPUChannelDetail, "ATSavedTraceCPUChannelDetail"> {
public:
	template<ATExchanger T>
	void Exchange(T& rw);

	// total number of rows in trace
	uint32 mRowCount = 0;

	// number of rows in each fixed-size group (except the last, which can be shorter)
	uint32 mRowGroupSize = 0;
	
	// row size/width in bytes
	uint32 mRowSize = 0;

	vdrefptr<IATSerializable> mpCodec;
	vdfastvector<ATSavedTraceCPUColumnInfo> mColumns;

	vdvector<vdrefptr<ATSaveStateMemoryBuffer>> mBlocks;
	vdvector<vdrefptr<IATSerializable>> mPredictors;
};

////////////////////////////////////////////////////////////////////////////////

class ATSavedTraceEventChannelDetail final : public ATSnapExchangeObject<ATSavedTraceEventChannelDetail, "ATSavedTraceEventChannelDetail"> {
public:
	template<ATExchanger T>
	void Exchange(T& rw);

	vdvector<VDStringW> mStringTable;
	vdfastvector<uint32> mColorTable;
	vdrefptr<ATSaveStateMemoryBuffer> mpEventBuffer;
};

////////////////////////////////////////////////////////////////////////////////

class ATSavedTraceVideoChannelDetail final : public ATSnapExchangeObject<ATSavedTraceVideoChannelDetail, "ATSavedTraceVideoChannelDetail"> {
public:
	template<ATExchanger T>
	void Exchange(T& rw);

	VDStringW mFormat;
	uint32 mFrameCount = 0;
	uint32 mFrameBufferCount = 0;
	uint32 mFrameBuffersPerGroup = 0;

	vdrefptr<ATSaveStateMemoryBuffer> mpFrameInfo;
	vdrefptr<ATSaveStateMemoryBuffer> mpFrameBufferInfo;
	vdrefptr<ATSaveStateMemoryBuffer> mpFrameBufferInfo2;
	vdvector<vdrefptr<ATSaveStateMemoryBuffer>> mFrameBufferGroups;
	vdvector<vdrefptr<ATSaveStateMemoryBuffer>> mFrameBufferGroups2;
};

#endif
