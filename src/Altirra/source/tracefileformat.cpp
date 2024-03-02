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

#include <stdafx.h>
#include <at/atcore/enumparseimpl.h>
#include <at/atcore/savestate.h>
#include "tracefileformat.h"

AT_DEFINE_ENUM_TABLE_BEGIN(ATSavedTraceCPUColumnType)
	{ ATSavedTraceCPUColumnType::None,				"none" },
	{ ATSavedTraceCPUColumnType::A,					"a" },
	{ ATSavedTraceCPUColumnType::X,					"x" },
	{ ATSavedTraceCPUColumnType::Y,					"y" },
	{ ATSavedTraceCPUColumnType::S,					"s" },
	{ ATSavedTraceCPUColumnType::P,					"p" },
	{ ATSavedTraceCPUColumnType::PC,				"pc" },
	{ ATSavedTraceCPUColumnType::Opcode,			"opcode" },
	{ ATSavedTraceCPUColumnType::Cycle,				"cycle" },
	{ ATSavedTraceCPUColumnType::UnhaltedCycle,		"unhalted_cycle" },
	{ ATSavedTraceCPUColumnType::Irq,				"irq" },
	{ ATSavedTraceCPUColumnType::Nmi,				"nmi" },
	{ ATSavedTraceCPUColumnType::EffectiveAddress,	"effective_address" },
	{ ATSavedTraceCPUColumnType::GlobalPCBase,		"global_pc_base" },
AT_DEFINE_ENUM_TABLE_END(ATSavedTraceCPUColumnType, ATSavedTraceCPUColumnType::None)

////////////////////////////////////////////////////////////////////////////////

template<ATExchanger T>
void ATSavedTraceRoot::Exchange(T& rw) {
	rw.Transfer("groups", &mGroups);
}

template void ATSavedTraceRoot::Exchange(ATSerializer&);
template void ATSavedTraceRoot::Exchange(ATDeserializer&);

////////////////////////////////////////////////////////////////////////////////

template<ATExchanger T>
void ATSavedTraceGroup::Exchange(T& rw) {
	rw.Transfer("name", &mName);
	rw.Transfer("channels", &mChannels);
}

template void ATSavedTraceGroup::Exchange(ATSerializer&);
template void ATSavedTraceGroup::Exchange(ATDeserializer&);

////////////////////////////////////////////////////////////////////////////////

template<ATExchanger T>
void ATSavedTraceChannel::Exchange(T& rw) {
	rw.Transfer("name", &mName);
	rw.Transfer("detail", &mpDetail);
}

template void ATSavedTraceChannel::Exchange(ATSerializer&);
template void ATSavedTraceChannel::Exchange(ATDeserializer&);

////////////////////////////////////////////////////////////////////////////////

template<typename T_Self, ATExchanger T_Ex>
void ATSavedTraceFrameChannelDetail::FrameBoundaryInfo::Exchange(T_Self& self, T_Ex& rw) {
	rw.Transfer("offset", &self.mOffset);
	rw.Transfer("period", &self.mPeriod);
	rw.Transfer("count", &self.mCount);

	if constexpr(rw.IsReader) {
		if (self.mCount == 0 || self.mPeriod == 0)
			throw ATInvalidSaveStateException();
	}
}

template<ATExchanger T>
void ATSavedTraceFrameChannelDetail::Exchange(T& rw) {
	rw.Transfer("ticks_per_second", &mTicksPerSecond);
	rw.Transfer("frame_boundaries", &mFrameBoundaries);

	if constexpr(rw.IsReader) { 
		if (mTicksPerSecond < 1770000.0 || mTicksPerSecond > 1790000.0)
			throw ATUnsupportedSaveStateException();
	}
}

template void ATSavedTraceFrameChannelDetail::Exchange(ATSerializer&);
template void ATSavedTraceFrameChannelDetail::Exchange(ATDeserializer&);

////////////////////////////////////////////////////////////////////////////////

template<typename T_Self, ATExchanger T_Ex>
void ATSavedTraceCPUColumnInfo::Exchange(T_Self& self, T_Ex& rw) {
	rw.Transfer("type", &self.mType);
	rw.Transfer("bit_offset", &self.mBitOffset);
	rw.Transfer("bit_width", &self.mBitWidth);
}

template<ATExchanger T>
void ATSavedTraceCPUChannelDetail::Exchange(T& rw) {
	rw.Transfer("row_count", &mRowCount);
	rw.Transfer("row_group_size", &mRowGroupSize);
	rw.Transfer("predictors", &mPredictors);

	rw.Transfer("codec", &mpCodec);
	rw.Transfer("columns", &mColumns);
	rw.Transfer("byte_count", &mRowSize);
	rw.Transfer("blocks", &mBlocks);

	if constexpr(rw.IsReader) {
		// stripe must be at least one byte wide
		if (mRowSize == 0)
			throw ATInvalidSaveStateException();

		if (mRowSize > 4096)
			throw ATUnsupportedSaveStateException();

		// there must be at least one column
		if (mColumns.empty())
			throw ATInvalidSaveStateException();

		const uint32 bitCount = mRowSize * 8;
		uint32 lastColumnEnd = 0;

		for(const ATSavedTraceCPUColumnInfo& col : mColumns) {
			// columns must be non-overlapping and ascending (though there MAY be
			// gaps)
			if (col.mBitOffset < lastColumnEnd)
				throw ATInvalidSaveStateException();

			// columns must be non-zero width
			if (!col.mBitWidth)
				throw ATInvalidSaveStateException();

			// columns must be fully contained
			if (col.mBitOffset >= bitCount || bitCount - col.mBitOffset < col.mBitWidth)
				throw ATInvalidSaveStateException();

			// most column types must be byte-aligned
			switch(col.mType) {
				case ATSavedTraceCPUColumnType::None:
				case ATSavedTraceCPUColumnType::Irq:
				case ATSavedTraceCPUColumnType::Nmi:
					break;

				default:
					if ((col.mBitOffset & 7) || (col.mBitWidth & 7))
						throw ATInvalidSaveStateException();
					break;
			}

			lastColumnEnd = col.mBitOffset + col.mBitWidth;
		}
	}
}

template void ATSavedTraceCPUChannelDetail::Exchange(ATSerializer&);
template void ATSavedTraceCPUChannelDetail::Exchange(ATDeserializer&);

////////////////////////////////////////////////////////////////////////////////

template<ATExchanger T>
void ATSavedTraceEventChannelDetail::Exchange(T& rw) {
	rw.Transfer("string_table", &mStringTable);
	rw.Transfer("color_table", &mColorTable);
	rw.Transfer("event_buffer", &mpEventBuffer);
}

template void ATSavedTraceEventChannelDetail::Exchange(ATSerializer&);
template void ATSavedTraceEventChannelDetail::Exchange(ATDeserializer&);

////////////////////////////////////////////////////////////////////////////////

template<ATExchanger T>
void ATSavedTraceVideoChannelDetail::Exchange(T& rw) {
	rw.Transfer("format", &mFormat);
	rw.Transfer("frame_count", &mFrameCount);
	rw.Transfer("frame_buffer_count", &mFrameBufferCount);
	rw.Transfer("frame_buffers_per_group", &mFrameBuffersPerGroup);
	rw.Transfer("frame_info", &mpFrameInfo);
	rw.Transfer("frame_buffer_info", &mpFrameBufferInfo);
	rw.Transfer("frame_buffer_groups", &mFrameBufferGroups);

	if constexpr(rw.IsReader) {
		if (!mFrameBuffersPerGroup || mFrameBuffersPerGroup > 65536)
			throw ATInvalidSaveStateException();
	}
}

template void ATSavedTraceVideoChannelDetail::Exchange(ATSerializer&);
template void ATSavedTraceVideoChannelDetail::Exchange(ATDeserializer&);
