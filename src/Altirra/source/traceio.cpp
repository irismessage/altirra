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
#include <vd2/system/binary.h>
#include <vd2/system/memory.h>
#include <vd2/system/vdstl_vectorview.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <at/atcore/savestate.h>
#include <at/atcore/serializable.h>
#include "trace.h"
#include "tracecpu.h"
#include "tracefileencoding.h"
#include "tracefileformat.h"
#include "traceio.h"
#include "tracevideo.h"
#include "cputracer.h"
#include "savestateio.h"

namespace {
	static constexpr ATSavedTraceCPUColumnInfo kATSavedCPUTraceColumns[] {
		{ ATSavedTraceCPUColumnType::Cycle,				  0, 16 },
		{ ATSavedTraceCPUColumnType::UnhaltedCycle,		 16, 16 },
		{ ATSavedTraceCPUColumnType::A,					 32,  8 },
		{ ATSavedTraceCPUColumnType::X,					 40,  8 },
		{ ATSavedTraceCPUColumnType::Y,					 48,  8 },
		{ ATSavedTraceCPUColumnType::S,					 56,  8 },
		{ ATSavedTraceCPUColumnType::PC,				 64, 16 },
		{ ATSavedTraceCPUColumnType::P,					 80,  8 },
		{ ATSavedTraceCPUColumnType::Irq,				 88,  1 },
		{ ATSavedTraceCPUColumnType::Nmi,				 89,  1 },
		{ ATSavedTraceCPUColumnType::None,				 92,  4 },
		{ ATSavedTraceCPUColumnType::EffectiveAddress,	 96, 32 },
		{ ATSavedTraceCPUColumnType::Opcode,			128, 32 },
		{ ATSavedTraceCPUColumnType::GlobalPCBase,		160, 32 },
	};
}

////////////////////////////////////////////////////////////////////////////////

struct ATTraceFmtStandardPredictors {
	vdrefptr<IATTraceFmtPredictor> mPredictors[13] {
		// predict P[10] and flags[11]
		vdrefptr<IATTraceFmtPredictor>(new ATSavedTracePredictorXOR(10, 2)),

		// predict EA[12..15]
		vdrefptr<IATTraceFmtPredictor>(new ATSavedTracePredictorEA(12)),

		// predict PC[8..9]
		vdrefptr<IATTraceFmtPredictor>(new ATSavedTracePredictorPC(8)),

		// predict AXYS[4..7] deltas from previous PC
		vdrefptr<IATTraceFmtPredictor>(new ATSavedTracePredictorXor32TablePrev16(4, 8)),

		// predict AXYS[4..7] vertically
		vdrefptr<IATTraceFmtPredictor>(new ATSavedTracePredictorVertDelta8(4, 4)),

		// predict Insn[16...19] based on PC[8..9] with flags[11][4..7]
		vdrefptr<IATTraceFmtPredictor>(new ATSavedTracePredictorInsn(16, 4, 8, 11*8+4)),

		// decode cycle[0..1] and unhaltedCycle[2..3] deltas to 1's complement
		vdrefptr<IATTraceFmtPredictor>(new ATSavedTracePredictorSignMag16(0)),
		vdrefptr<IATTraceFmtPredictor>(new ATSavedTracePredictorSignMag16(2)),

		// predict unhaltedCycle[2..3] deltas based on opcode[16]
		vdrefptr<IATTraceFmtPredictor>(new ATSavedTracePredictorDelta16TablePrev8(2, 16)),

		// predict cycle[0..1] from unhaltedCycle[2..3] horizontally
		vdrefptr<IATTraceFmtPredictor>(new ATSavedTracePredictorHorizDelta16(0, 2)),

		// predict cycle[0..1] and unhaltedCycle[2..3] vertically with +2 bias
		vdrefptr<IATTraceFmtPredictor>(new ATSavedTracePredictorVertDelta16(0, 2)),
		vdrefptr<IATTraceFmtPredictor>(new ATSavedTracePredictorVertDelta16(2, 2)),

		// predict globalPCBase[20..23] based on current opcode[16]
		vdrefptr<IATTraceFmtPredictor>(new ATSavedTracePredictorXor32Table8(20, 16)),
	};
};

////////////////////////////////////////////////////////////////////////////////

double ATSaveTraceFrameChannel(ATSavedTraceRoot& root, ATTraceChannelTickBased& ch) {
	vdrefptr frameGroup(new ATSavedTraceGroup);
	root.mGroups.push_back(frameGroup);

	vdrefptr frameChannel(new ATSavedTraceChannel);
	frameGroup->mName = L"Frames";
	frameGroup->mChannels.push_back(frameChannel);

	vdrefptr frameDetail(new ATSavedTraceFrameChannelDetail);
	frameChannel->mName = L"Frames";
	frameChannel->mpDetail = frameDetail;

	ch.StartIteration(-1e+10, ch.GetDuration(), 0);

	const double ticksPerSecond = 1.0 / ch.GetSecondsPerTick();
	frameDetail->mTicksPerSecond = ticksPerSecond;

	uint64 lastEndTick = 0;
	uint64 lastPeriod = ~(uint64)0;

	ATTraceEvent ev;
	while(ch.GetNextEvent(ev)) {
		uint64 startTick = (uint64)(0.5 + ev.mEventStart * ticksPerSecond);
		uint64 endTick = (uint64)(0.5 + ev.mEventStop * ticksPerSecond);

		if (endTick < lastEndTick)
			continue;

		uint64 deltaTick = endTick - startTick;
				
		if (startTick == lastEndTick && deltaTick == lastPeriod)
			++frameDetail->mFrameBoundaries.back().mCount;
		else {
			auto& fb = frameDetail->mFrameBoundaries.emplace_back();
			fb.mOffset = startTick - lastEndTick;
			fb.mPeriod = deltaTick;
			fb.mCount = 1;
		}

		lastPeriod = deltaTick;
		lastEndTick = endTick;
	}

	return ticksPerSecond;
}

void ATSaveTraceCPUChannel(ATSavedTraceRoot& root, ATTraceChannelCPUHistory& cpuHistory, IATSaveStateSerializer& serializer, const vdfunction<void(int, int)>& progressFn) {
	vdrefptr cpuGroup(new ATSavedTraceGroup);
	root.mGroups.push_back(cpuGroup);

	vdrefptr cpuChannel(new ATSavedTraceChannel);
	cpuGroup->mName = L"CPU";
	cpuGroup->mChannels.push_back(cpuChannel);

	vdrefptr cpuDetail(new ATSavedTraceCPUChannelDetail);
	cpuChannel->mName = L"CPU History";
	cpuChannel->mpDetail = cpuDetail;

	cpuDetail->mRowCount = cpuHistory.GetEventCount();
	cpuDetail->mRowGroupSize = 512*1024;

	static constexpr uint32 kRowSize = 24;
	cpuDetail->mRowSize = kRowSize;

	vdrefptr codec(new ATSavedTraceCodecSparse);
	cpuDetail->mpCodec = codec;
	cpuDetail->mColumns.assign(std::begin(kATSavedCPUTraceColumns), std::end(kATSavedCPUTraceColumns));

	const ATCPUHistoryEntry *heptrs[256] {};

	uint32 baseCycle = 0;
	uint32 baseUnhaltedCycle = 0;
	bool firstInsn = true;

	ATTraceFmtStandardPredictors predictors;

	for(const auto& predictor : predictors.mPredictors)
		cpuDetail->mPredictors.emplace_back(vdpoly_cast<IATSerializable *>(predictor.get()));

	cpuHistory.StartHistoryIteration(0, 0);

	for(uint32 row = 0; row < cpuDetail->mRowCount; row += cpuDetail->mRowGroupSize) {
		uint32 tc = std::min<uint32>(cpuDetail->mRowGroupSize, cpuDetail->mRowCount - row);

		if (progressFn)
			progressFn(row / cpuDetail->mRowGroupSize, (cpuDetail->mRowCount - 1) / cpuDetail->mRowGroupSize + 1);

		vdblock<uint8> rawBuf(tc * kRowSize);
		std::fill(rawBuf.begin(), rawBuf.end(), 0);

		uint32 offset = 0;
		while(offset < tc) {
			uint32 tc2 = std::min<uint32>(tc - offset, (uint32)vdcountof(heptrs));
			const uint32 actual = cpuHistory.ReadHistoryEvents(heptrs, row + offset, tc2);
			if (actual < tc2)
				throw MyError("Unexpected end of stream while reading CPU history events.");

			if (firstInsn && actual) {
				firstInsn = false;

				baseCycle = cpuHistory.GetHistoryBaseCycle();
				baseUnhaltedCycle = heptrs[0]->mUnhaltedCycle;
			}

			// pack
			for(uint32 i = 0; i < actual; ++i) {
				const ATCPUHistoryEntry& VDRESTRICT he = *heptrs[i];
				uint8 *VDRESTRICT dst = &rawBuf[(offset + i) * kRowSize];

				VDWriteUnalignedLEU16(&dst[0], he.mCycle - baseCycle);
				VDWriteUnalignedLEU16(&dst[2], he.mUnhaltedCycle - baseUnhaltedCycle);
				dst[4] = he.mA;
				dst[5] = he.mX;
				dst[6] = he.mY;
				dst[7] = he.mS;
				VDWriteUnalignedLEU16(&dst[8], he.mPC);
				dst[10] = he.mP;
				dst[11] = (he.mbIRQ ? 1 : 0) + (he.mbNMI ? 2 : 0);
				VDWriteUnalignedLEU32(&dst[12], he.mEA);
				dst[16] = he.mOpcode[0];
				dst[17] = he.mOpcode[1];
				dst[18] = he.mOpcode[2];
				dst[19] = he.mOpcode[3];
				VDWriteUnalignedLEU32(&dst[20], he.mGlobalPCBase);
			}

			offset += actual;
		}

		// run predictors
		for(size_t i = vdcountof(predictors.mPredictors); i; --i) {
			const auto& predictor = predictors.mPredictors[i-1];

			predictor->Reset();
			predictor->Encode(rawBuf.data(), kRowSize, tc);
		}

		// run sparse encoding codec
		ATSaveStateMemoryBuffer& buf = *cpuDetail->mBlocks.emplace_back(new ATSaveStateMemoryBuffer);

		buf.mpDirectName = L"cpu-history-block.bin";

		auto& writeBuffer = buf.GetWriteBuffer();
		writeBuffer.resize(tc * kRowSize);
		codec->Encode(buf, rawBuf.data(), kRowSize, tc);

		serializer.PreSerializeDirect(buf);
	}
}

void ATSaveTraceEventChannel(ATSavedTraceGroup& savedGroup, IATTraceChannel& ch, double ticksPerSecond) {
	vdrefptr savedChannel(new ATSavedTraceChannel);
	savedGroup.mChannels.push_back(savedChannel);

	savedChannel->mName = ch.GetName();

	vdrefptr savedEventDetail(new ATSavedTraceEventChannelDetail);
	savedChannel->mpDetail = savedEventDetail;

	ATTraceEvent ev;
	ch.StartIteration(-1e+10, 1e+10, 0);

	vdfastvector<uint8> buf;
	uint8 eventBuf[20] {};

	vdhashmap<VDStringW, uint32, vdhash<VDStringW>> stringLookup;
	vdhashmap<uint32, uint32> colorLookup;

	sint64 tickBase = 0;
	uint32 lastStringId = 0;
	uint32 lastColorId = 0;
	while(ch.GetNextEvent(ev)) {
		const uint64 tickStart = (uint64)(sint64)(ev.mEventStart * ticksPerSecond + 0.5);
		const uint64 tickEnd = (uint64)(sint64)(ev.mEventStop * ticksPerSecond + 0.5);

		uint64 tickStartDelta = tickStart - tickBase;

		if (tickStartDelta &  UINT64_C(0x80000000'00000000))
			tickStartDelta ^= UINT64_C(0x7FFFFFFF'FFFFFFFF);

		tickStartDelta = (tickStartDelta >> 63) | (tickStartDelta << 1);

		auto rc = colorLookup.insert(ev.mBgColor);
		if (rc.second) {
			rc.first->second = (uint32)savedEventDetail->mColorTable.size();
			savedEventDetail->mColorTable.push_back(ev.mBgColor & 0xFFFFFF);
		}

		const uint32 colorId = rc.first->second;
		uint32 colorIdDelta = colorId - lastColorId;
		lastColorId = colorId;

		if (colorIdDelta & 0x80000000)
			colorIdDelta ^= 0x7FFFFFFF;

		colorIdDelta = (colorIdDelta >> 31) | (colorIdDelta << 1);

		auto r = stringLookup.insert_as(VDStringSpanW(ev.mpName));
		if (r.second) {
			r.first->second = (uint32)savedEventDetail->mStringTable.size();
			savedEventDetail->mStringTable.push_back(r.first->first);
		}

		const uint32 stringId = r.first->second;
		uint32 stringIdDelta = stringId - lastStringId;
		lastStringId = stringId;

		if (stringIdDelta & 0x80000000)
			stringIdDelta ^= 0x7FFFFFFF;

		stringIdDelta = (stringIdDelta >> 31) | (stringIdDelta << 1);

		const uint32 idDeltas = (stringIdDelta & 0xFFFFFF) + (colorIdDelta << 24);

		VDWriteUnalignedLEU64(eventBuf + 0, tickStartDelta);
		VDWriteUnalignedLEU64(eventBuf + 8, tickEnd - tickStart);
		VDWriteUnalignedLEU32(eventBuf + 16, idDeltas);

		tickBase = tickEnd;

		buf.insert(buf.end(), std::begin(eventBuf), std::end(eventBuf));
	}

	if (!buf.empty()) {
		vdrefptr eventMemBuf(new ATSaveStateMemoryBuffer);
		eventMemBuf->GetWriteBuffer().assign(buf.begin(), buf.end());
		eventMemBuf->mpDirectName = L"eventdata.bin";

		savedEventDetail->mpEventBuffer = eventMemBuf;
	}
}

void ATSaveTraceVideoChannel(ATSavedTraceRoot& root, IATTraceChannelVideo& videoChannel, double ticksPerSecond, IATSaveStateSerializer& serializer) {
	vdrefptr<ATSavedTraceGroup> savedGroup(new ATSavedTraceGroup);
	root.mGroups.push_back(savedGroup);
	savedGroup->mName = L"Video";

	vdrefptr<ATSavedTraceChannel> savedChannel(new ATSavedTraceChannel);
	savedGroup->mChannels.push_back(savedChannel);
	savedChannel->mName = L"Video";

	IATTraceChannel& traceChannel = *videoChannel.AsTraceChannel();
	const uint32 frameBufferCount = videoChannel.GetFrameBufferCount();
	const uint32 frameBufferGroupSize = 64;
	const uint32 frameCount = traceChannel.GetEventCount();

	vdrefptr<ATSavedTraceVideoChannelDetail> savedVideo(new ATSavedTraceVideoChannelDetail);
	savedChannel->mpDetail = savedVideo;
	savedVideo->mFrameBufferCount = frameBufferCount;
	savedVideo->mFrameBuffersPerGroup = frameBufferGroupSize;
	savedVideo->mFrameCount = frameCount;
	savedVideo->mFormat = L"yv12_delta";

	vdrefptr<ATSaveStateMemoryBuffer> framePointerBuffer(new ATSaveStateMemoryBuffer);
	savedVideo->mpFrameInfo = framePointerBuffer;
	framePointerBuffer->mpDirectName = L"frameinfo.bin";

	auto& pointerWriteBuffer = framePointerBuffer->GetWriteBuffer();
	pointerWriteBuffer.resize(frameCount * 12);

	uint32 lastFbIdx = 0;
	uint64 lastTick = 0;
	for(uint32 i = 0; i < frameCount; ++i) {
		uint32 fbIdx = (uint32)videoChannel.GetFrameBufferIndexForFrame(i);
		uint8 *dst = &pointerWriteBuffer[12*i];
		double frameTime = videoChannel.GetTimeForFrame(i);

		const uint64 frameTick = (uint64)(sint64)(0.5 + frameTime * ticksPerSecond);

		VDWriteUnalignedLEU64(dst, frameTick - lastTick);
		lastTick = frameTick;

		VDWriteUnalignedLEU32(dst + 8, fbIdx - lastFbIdx);
		lastFbIdx = fbIdx;
	}

	vdrefptr<ATSaveStateMemoryBuffer> frameInfoBuffer(new ATSaveStateMemoryBuffer);
	savedVideo->mpFrameBufferInfo = frameInfoBuffer;
	frameInfoBuffer->mpDirectName = L"framebufferinfo.bin";

	auto& infoWriteBuffer = frameInfoBuffer->GetWriteBuffer();
	infoWriteBuffer.resize(16 * frameBufferCount);

	vdrefptr<ATSaveStateMemoryBuffer> frameDataBuffer;

	VDPixmapBuffer prev;

	for(uint32 i = 0; i < frameBufferCount; ++i) {
		const VDPixmap& px = videoChannel.GetFrameBufferByIndex(i);
		uint8 *dstInfo = &infoWriteBuffer[16 * i];

		if (frameDataBuffer && i % frameBufferGroupSize == 0) {
			serializer.PreSerializeDirect(*frameDataBuffer);
			frameDataBuffer = nullptr;
		}

		if (!frameDataBuffer) {
			frameDataBuffer = new ATSaveStateMemoryBuffer;
			frameDataBuffer->mpDirectName = L"framedata.bin";

			savedVideo->mFrameBufferGroups.push_back(frameDataBuffer);
		}

		auto& frameWriteBuffer = frameDataBuffer->GetWriteBuffer();

		const uint32 w = px.w;
		const uint32 h = px.h;
		const uint32 w2 = w >> 1;
		const uint32 h2 = h >> 1;
		const uint32 rawOffset = (uint32)frameWriteBuffer.size();

		const auto diffPlane = [](const uint8 *src1, ptrdiff_t srcPitch1, const uint8 *src2, ptrdiff_t srcPitch2, uint32 w, uint32 h) -> vdrect32 {
			vdrect32 r { (sint32)w, (sint32)h, 0, 0 };

			for(uint32 y = 0; y < h; ++y) {
				uint32 x1 = 0;
				uint32 x2 = w;

				while(x1 < x2 && src1[x1] == src2[x1])
					++x1;

				while(x1 < x2 && src1[x2-1] == src2[x2-1])
					--x2;

				if (x1 != x2) {
					if (r.top > (sint32)y)
						r.top = (sint32)y;

					if (r.bottom <= (sint32)y)
						r.bottom = (sint32)y+1;

					if (r.left > (sint32)x1)
						r.left = (sint32)x1;

					if (r.right < (sint32)x2)
						r.right = (sint32)x2;
				}

				src1 += srcPitch1;
				src2 += srcPitch2;
			}

			if (r.empty())
				r = vdrect32(0, 0, 0, 0);

			return r;
		};

		if (w != prev.w || h != prev.h)
			prev.clear();

		vdrect32 r { 0, 0, (sint32)w, (sint32)h };

		if (prev.base()) {
			vdrect32 yrect = diffPlane((const uint8 *)prev.data, prev.pitch, (const uint8 *)px.data, px.pitch, w, h);
			vdrect32 cbrect = diffPlane((const uint8 *)prev.data2, prev.pitch2, (const uint8 *)px.data2, px.pitch2, w2, h2);
			vdrect32 crrect = diffPlane((const uint8 *)prev.data3, prev.pitch3, (const uint8 *)px.data3, px.pitch3, w2, h2);

			if (yrect.empty()) {
				r = { (sint32)w, (sint32)h, 0, 0 };
			} else {
				r = {
					yrect.left & ~1,
					yrect.top & ~1,
					(yrect.right + 1) & ~1,
					(yrect.bottom + 1) & ~1
				};
			}

			if (!cbrect.empty()) {
				r.left = std::min<sint32>(r.left, cbrect.left * 2);
				r.top = std::min<sint32>(r.top, cbrect.top * 2);
				r.right = std::max<sint32>(r.right, cbrect.right * 2);
				r.bottom = std::max<sint32>(r.bottom, cbrect.bottom * 2);
			}

			if (!crrect.empty()) {
				r.left = std::min<sint32>(r.left, crrect.left * 2);
				r.top = std::min<sint32>(r.top, crrect.top * 2);
				r.right = std::max<sint32>(r.right, crrect.right * 2);
				r.bottom = std::max<sint32>(r.bottom, crrect.bottom * 2);
			}

			if (r.empty())
				r = { 0, 0, 0, 0 };
		}

		VDWriteUnalignedLEU16(dstInfo + 0, w);
		VDWriteUnalignedLEU16(dstInfo + 2, h);
		VDWriteUnalignedLEU32(dstInfo + 4, rawOffset);
		VDWriteUnalignedLEU16(dstInfo + 8, r.left);
		VDWriteUnalignedLEU16(dstInfo + 10, r.top);
		VDWriteUnalignedLEU16(dstInfo + 12, r.right);
		VDWriteUnalignedLEU16(dstInfo + 14, r.bottom);

		uint32 encodedSize = ((r.area() * 3) >> 1) + 1;

		frameWriteBuffer.resize(rawOffset + encodedSize);

		const auto copyPlane = [](uint8 *dst, const uint8 *src, ptrdiff_t srcPitch, uint32 w, uint32 h) -> uint8 * {
			VDMemcpyRect(dst, w, src, srcPitch, w, h);
			dst += w*h;

			return dst;
		};

		const auto deltaPlane = [](uint8 *VDRESTRICT dst, const uint8 *src, ptrdiff_t srcPitch, const uint8 *ref, ptrdiff_t refPitch, uint32 w, uint32 h) -> uint8 * {
			for(uint32 y = 0; y < h; ++y) {
				for(uint32 x = 0; x < w; ++x) {
					*dst++ = src[x] ^ ref[x];
				}

				src += srcPitch;
				ref += refPitch;
			}

			return dst;
		};

		uint8 *dst0 = &frameWriteBuffer[rawOffset];
		uint8 *dst = dst0;
		const uint32 encw = r.width();
		const uint32 ench = r.height();
		const uint32 encw2 = encw >> 1;
		const uint32 ench2 = ench >> 1;

		const uint8 *VDRESTRICT srcy = (const uint8 *)px.data + r.left + r.top * px.pitch;
		const uint8 *VDRESTRICT srccb = (const uint8 *)px.data2 + (r.left >> 1) + (r.top >> 1) * px.pitch2;
		const uint8 *VDRESTRICT srccr = (const uint8 *)px.data3 + (r.left >> 1) + (r.top >> 1) * px.pitch3;

		if (prev.base()) {
			*dst++ = 1;
			dst = deltaPlane(dst,
				srcy,
				px.pitch,
				(const uint8 *)prev.data + r.left + r.top * prev.pitch,
				prev.pitch,
				encw,
				ench);
			dst = deltaPlane(dst,
				srccb,
				px.pitch2,
				(const uint8 *)prev.data2 + (r.left >> 1) + (r.top >> 1) * prev.pitch2,
				prev.pitch2,
				encw2,
				ench2);
			dst = deltaPlane(dst,
				srccr,
				px.pitch3,
				(const uint8 *)prev.data3 + (r.left >> 1) + (r.top >> 1) * prev.pitch3,
				prev.pitch3,
				encw2,
				ench2);
		} else {
			*dst++ = 0;
			dst = copyPlane(dst, (const uint8 *)px.data + r.left + r.top * px.pitch, px.pitch, encw, ench);
			dst = copyPlane(dst, (const uint8 *)px.data2 + (r.left >> 1) + (r.top >> 1) * px.pitch2, px.pitch2, encw2, ench2);
			dst = copyPlane(dst, (const uint8 *)px.data3 + (r.left >> 1) + (r.top >> 1) * px.pitch3, px.pitch3, encw2, ench2);
		}

		prev.assign(px);
	}

	if (frameDataBuffer)
		serializer.PreSerializeDirect(*frameDataBuffer);
}

vdrefptr<IATSerializable> ATSaveTrace(const ATTraceCollection& coll, IATSaveStateSerializer& serializer, const vdfunction<void(int, int)>& progressFn) {
	vdrefptr root(new ATSavedTraceRoot);
	double ticksPerSecond = 0;

	ATTraceGroup *frameGroup = coll.GetGroupByType(kATTraceGroupType_Frames);
	if (frameGroup) {
		ATTraceChannelTickBased *ch = vdpoly_cast<ATTraceChannelTickBased *>(frameGroup->GetChannel(0));
		if (ch) {
			ticksPerSecond = ATSaveTraceFrameChannel(*root, *ch);
		}
	}

	if (ticksPerSecond == 0)
		throw MyError("Cannot save trace: no frame channel found.");

	ATTraceGroup *cpuGroup = coll.GetGroupByType(kATTraceGroupType_CPUHistory);
	if (cpuGroup) {
		ATTraceChannelCPUHistory *cpuHistory = vdpoly_cast<ATTraceChannelCPUHistory *>(cpuGroup->GetChannel(0));
		if (cpuHistory) {
			ATSaveTraceCPUChannel(*root, *cpuHistory, serializer, progressFn);
		}
	}

	ATTraceGroup *videoGroup = coll.GetGroupByType(kATTraceGroupType_Video);
	if (videoGroup) {
		IATTraceChannelVideo *videoChannel = vdpoly_cast<IATTraceChannelVideo *>(videoGroup->GetChannel(0));
		if (videoChannel) {
			ATSaveTraceVideoChannel(*root, *videoChannel, ticksPerSecond, serializer);
		}
	}

	for(size_t groupIdx = 0, groupCount = coll.GetGroupCount(); groupIdx < groupCount; ++groupIdx) {
		ATTraceGroup *group = coll.GetGroup(groupIdx);

		if (group->GetType() != kATTraceGroupType_Normal)
			continue;

		// drop the CPU group, as it's redundant with and regenerated from history
		if (VDStringSpanW(group->GetName()) == L"CPU")
			continue;
		
		vdrefptr savedGroup(new ATSavedTraceGroup);
		root->mGroups.push_back(savedGroup);

		savedGroup->mName = group->GetName();

		for(size_t channelIdx = 0, channelCount = group->GetChannelCount(); channelIdx < channelCount; ++channelIdx) {
			IATTraceChannel *channel = group->GetChannel(channelIdx);

			ATSaveTraceEventChannel(*savedGroup, *channel, ticksPerSecond);
		}
	}

	return root;
}

////////////////////////////////////////////////////////////////////////////////

void ATLoadTraceFrameChannel(const ATSavedTraceFrameChannelDetail& savedFrameCh, ATTraceContext& ctx, ATCPUTimestampDecoder& tsdecoder) {
	// adjust base tick scale to actual timing
	ctx.mBaseTickScale = 1.0f / savedFrameCh.mTicksPerSecond;

	ATTraceChannelSimple *ch = ctx.mpCollection->AddGroup(L"Frames", kATTraceGroupType_Frames)->AddSimpleChannel(ctx.mBaseTime, ctx.mBaseTickScale, L"Frames");
			
	uint64 tick = 0;

	for(const auto& fb : savedFrameCh.mFrameBoundaries) {
		tick += fb.mOffset;

		for(uint32 i = 0; i < fb.mCount; ++i) {
			ch->AddTickEvent(tick, tick + fb.mPeriod, L"Frame", 0xFFFFFF);
			tick += fb.mPeriod;
		}
	}

	if (!savedFrameCh.mFrameBoundaries.empty()) {
		const auto& fb = savedFrameCh.mFrameBoundaries.back();

		tsdecoder.mCyclesPerFrame = fb.mPeriod;
		tsdecoder.mFrameCountBase = (tick - 1) / fb.mPeriod + 1;
		tsdecoder.mFrameTimestampBase = tick;
	}
}

template<typename T>
void ATOptimizeTracePredictorTypes(
	vdvector<vdrefptr<IATTraceFmtPredictor>>& predictors,
	vdvector<ATTraceFmtAccessMask>& predictorAccessMasks,
	uint32 rowSize
) {
	for(;;) {
		const size_t n = predictors.size();
		bool updated = false;

		for(size_t i = 0; i + 1 < n; ++i) {
			auto *vd16a = atser_cast<T *>(vdpoly_cast<IATSerializable *>(predictors[i]));

			if (!vd16a)
				continue;

			ATTraceFmtAccessMask betweenAccessMask(rowSize);

			for(size_t j = i + 1; j < n; ++j) {
				auto *vd16b = atser_cast<T *>(vdpoly_cast<IATSerializable *>(predictors[j]));
				if (!vd16b)
					continue;

				if (betweenAccessMask.CanSwapWith(predictorAccessMasks[j]) && vd16a->TryMerge(*vd16b)) {
					predictorAccessMasks[i].Merge(predictorAccessMasks[j]);
					predictorAccessMasks.erase(predictorAccessMasks.begin() + j);
					predictors.erase(predictors.begin() + j);
					updated = true;
					break;
				}
			}

			if (updated)
				break;
		}

		if (!updated)
			break;
	}
}

void ATOptimizeTracePredictors(
	vdvector<vdrefptr<IATTraceFmtPredictor>>& predictors,
	vdvector<ATTraceFmtAccessMask>& predictorAccessMasks,
	uint32 rowSize
) {
	ATOptimizeTracePredictorTypes<ATSavedTracePredictorSignMag16>(predictors, predictorAccessMasks, rowSize);

	// fuse HorizDelta16(n) + [VertDelta16(n, bias), VertDelta16(n+2, bias)]
	for(;;) {
		const size_t n = predictors.size();
		bool updated = false;

		for(size_t i = 0; i + 1 < n; ++i) {
			auto *hd16 = atser_cast<ATSavedTracePredictorHorizDelta16 *>(vdpoly_cast<IATSerializable *>(predictors[i]));
			if (!hd16)
				continue;

			const uint32 offset1 = hd16->GetDstOffset();
			const uint32 offset2 = offset1 + 2;
			if (offset2 == hd16->GetPredOffset()) {
				ATTraceFmtAccessMask betweenAccessMask(rowSize);

				for(size_t j = i + 1; j < n; ++j) {
					auto *vd16a = atser_cast<ATSavedTracePredictorVertDelta16 *>(vdpoly_cast<IATSerializable *>(predictors[j]));

					if (vd16a && (vd16a->GetOffset() == offset1 || vd16a->GetOffset() == offset2)) {
						const uint32 vertOffset1 = vd16a->GetOffset();
						const uint32 vertOffset2 = vertOffset1 ^ (offset1 ^ offset2);
						const sint16 vertBias = vd16a->GetBias();

						ATTraceFmtAccessMask betweenAccessMask2(betweenAccessMask);

						for(size_t k = j + 1; k < n; ++k) {
							auto *vd16b = atser_cast<ATSavedTracePredictorVertDelta16 *>(vdpoly_cast<IATSerializable *>(predictors[k]));
							if (vd16b &&
								vd16b->GetOffset() == vertOffset2 &&
								vd16b->GetBias() == vertBias &&
								betweenAccessMask2.CanSwapWith(predictorAccessMasks[j]))
							{
								predictorAccessMasks[i].Merge(predictorAccessMasks[j]);
								predictorAccessMasks[i].Merge(predictorAccessMasks[k]);

								predictorAccessMasks.erase(predictorAccessMasks.begin() + k);
								predictorAccessMasks.erase(predictorAccessMasks.begin() + j);
								predictors.erase(predictors.begin() + k);
								predictors.erase(predictors.begin() + j);

								predictors[i] = new ATSavedTracePredictorHVDelta16x2(offset1, vertBias);

								updated = true;
								break;
							}

							betweenAccessMask.Merge(predictorAccessMasks[k]);
						}

						if (updated)
							break;
					}

					betweenAccessMask.Merge(predictorAccessMasks[j]);
				}

				if (updated)
					break;
			}
		}

		if (!updated)
			break;
	}

	// fuse Xor32TablePrev16(a, b) + VertDelta8(a, 4)
	for(;;) {
		const size_t n = predictors.size();
		bool updated = false;

		for(size_t i = 0; i + 1 < n; ++i) {
			auto *tableStage = atser_cast<ATSavedTracePredictorXor32TablePrev16*>(vdpoly_cast<IATSerializable *>(predictors[i]));
			if (!tableStage)
				continue;

			const uint32 valueOffset = tableStage->GetValueOffset();
			const uint32 pcOffset = tableStage->GetPCOffset();
			ATTraceFmtAccessMask betweenAccessMask(rowSize);

			for(size_t j = i + 1; j < n; ++j) {
				auto *vertNode = atser_cast<ATSavedTracePredictorVertDelta8 *>(vdpoly_cast<IATSerializable *>(predictors[j]));

				if (vertNode &&
					(vertNode->GetOffset() == valueOffset || vertNode->GetCount() == 4) &&
					betweenAccessMask.CanSwapWith(predictorAccessMasks[j]))
				{
					predictorAccessMasks.erase(predictorAccessMasks.begin() + j);
					predictors.erase(predictors.begin() + j);

					predictors[i] = new ATSavedTracePredictorXor32VertDeltaTablePrev16(valueOffset, pcOffset);

					updated = true;
					break;
				}

				betweenAccessMask.Merge(predictorAccessMasks[j]);
			}

			if (updated)
				break;
		}

		if (!updated)
			break;
	}
}

struct ATSavedTraceCPURowGroupProcessor {
public:
	ATSavedTraceCPURowGroupProcessor(IATTraceFmtCodec& codec, vdvector_view<const vdrefptr<IATTraceFmtPredictor>> predictors, uint32 rowSize, uint32 rowGroupSize);

	void Process(const ATSaveStateMemoryBuffer& buffer, uint32 rowCount);

	const uint32 mRowSize;
	const uint32 mRowGroupSize;
	uint32 mCurrentRowCount;
	vdblock<uint8, vdaligned_alloc<uint8>> mDecodeBuf;
	vdrefptr<IATTraceFmtCodec> mpCodec;
	vdvector<vdrefptr<IATTraceFmtPredictor>> mPredictors;
};

ATSavedTraceCPURowGroupProcessor::ATSavedTraceCPURowGroupProcessor(IATTraceFmtCodec& codec, vdvector_view<const vdrefptr<IATTraceFmtPredictor>> predictors, uint32 rowSize, uint32 rowGroupSize)
	: mRowSize(rowSize)
	, mRowGroupSize(rowGroupSize)
	, mDecodeBuf(rowSize * rowGroupSize + 32)		// +32 is to make decoder's job easier with vectorization
	, mpCodec(&codec)
{
	mPredictors.resize(predictors.size());

	std::transform(predictors.begin(), predictors.end(), mPredictors.begin(),
		[](const vdrefptr<IATTraceFmtPredictor>& predictor) {
			return predictor->Clone();
		}
	);
}

void ATSavedTraceCPURowGroupProcessor::Process(const ATSaveStateMemoryBuffer& buffer, uint32 rowCount) {
	mCurrentRowCount = rowCount;

	mpCodec->Decode(buffer, mDecodeBuf.data(), mRowSize, rowCount);

	// reset predictors
	for(const auto& predictor : mPredictors)
		predictor->Reset();

	// run converter on bands at a time
	for(uint32 row = 0; row < rowCount; row += 1024) {
		const uint32 tc = std::min<uint32>(1024, rowCount - row);
		uint8 *src = &mDecodeBuf[row * mRowSize];

		// run predictors
		for(const auto& predictor : mPredictors)
			predictor->Decode(src, mRowSize, tc);
	}
}

void ATLoadTraceCPUChannel(const ATSavedTraceCPUChannelDetail& cpuDetail, ATTraceContext& ctx, const ATCPUTimestampDecoder& tsdecoder,
	const vdfunction<void(int, int)>& progressFn)
{
	const uint32 rowCount = cpuDetail.mRowCount;
	if (!rowCount)
		return;

	const uint32 rowGroupSize = cpuDetail.mRowGroupSize;
	const uint32 rowGroupCount = (rowCount - 1) / rowGroupSize + 1;

	// validate row group size -- can't be zero, we allow just about any other
	// (as the last group is allowed to be short anyway)
	if (!rowGroupSize)
		throw ATInvalidSaveStateException();

	// validate predictors
	if (cpuDetail.mPredictors.size() > 100)
		throw ATUnsupportedSaveStateException();

	const uint32 rowSize = cpuDetail.mRowSize;
	if (rowSize > 256)
		throw ATUnsupportedSaveStateException();

	const uint32 rowMaskSize = (rowSize + 31) >> 5;
	const uint32 numPredictors = (uint32)cpuDetail.mPredictors.size();

	vdfastvector<uint32> rowMaskTable(numPredictors * rowMaskSize * 2, 0);
	vdvector<ATTraceFmtAccessMask> predictorAccessMasks(numPredictors);

	vdvector<vdrefptr<IATTraceFmtPredictor>> predictors;
	uint32 predictorIdx = 0;
	for(const auto& predObj : cpuDetail.mPredictors) {
		IATTraceFmtPredictor *pred = vdpoly_cast<IATTraceFmtPredictor *>(predObj.get());
		if (!pred)
			throw ATInvalidSaveStateException();

		predictors.emplace_back(pred);

		auto& predAccMask = predictorAccessMasks[predictorIdx];

		predAccMask.mRowSize = rowSize;
		predAccMask.mReadMask.mMask.resize(rowMaskSize, 0);
		predAccMask.mWriteMask.mMask.resize(rowMaskSize, 0);

		pred->Validate(predAccMask);

		++predictorIdx;
	}

	ATOptimizeTracePredictors(predictors, predictorAccessMasks, rowSize);

	// create decoder (which will also do some validation)
	ATSavedTraceCPUHistoryDecoder decoder;
	decoder.Init(cpuDetail);

	// validate that block count is correct
	if (cpuDetail.mBlocks.size() != rowGroupCount)
		throw ATInvalidSaveStateException();

	// create CPU history channel in trace collection
	vdautoptr<ATCPUTraceProcessor> proc(new ATCPUTraceProcessor);
	bool procInit = true;

	vdautoarrayptr<ATCPUHistoryEntry> hebuf(new ATCPUHistoryEntry[1025]);
	memset(&hebuf[0], 0, sizeof(ATCPUHistoryEntry) * 1025);

	vdautoarrayptr<const ATCPUHistoryEntry *> hebufptrs(new const ATCPUHistoryEntry *[1025]);
	for(uint32 i = 0; i < 1024; ++i)
		hebufptrs[i] = &hebuf[i];

	IATTraceFmtCodec *codec = vdpoly_cast<IATTraceFmtCodec *>(cpuDetail.mpCodec);
	if (!codec)
		throw ATInvalidSaveStateException();

	uint64 tickBase = 0;
	uint32 lastCycle = 0;
	for(uint32 rowGroupIndex = 0; rowGroupIndex < rowGroupCount; ++rowGroupIndex) {
		if (!cpuDetail.mBlocks[rowGroupIndex])
			throw ATInvalidSaveStateException();

		if (progressFn)
			progressFn(rowGroupIndex, rowGroupCount);

		ATSaveStateMemoryBuffer& buf = *cpuDetail.mBlocks[rowGroupIndex];
		const uint32 rowsInGroup = std::min<uint32>(rowGroupSize, rowCount - rowGroupIndex * rowGroupSize);

		// run codec (+32 is to make decoder's job easier with vectorization)
		vdblock<uint8> decodeBuf(rowSize * rowsInGroup + 32);

		codec->Decode(buf, decodeBuf.data(), rowSize, rowsInGroup);

		buf.ReleaseReadBuffer();

		if (rowGroupIndex + 1 < rowGroupCount) {
			cpuDetail.mBlocks[rowGroupIndex + 1]->PrefetchReadBuffer();
		}

		// reset predictors
		for(const auto& predictor : predictors)
			predictor->Reset();

		// run converter
		for(uint32 row = 0; row < rowsInGroup; row += 1024) {
			const uint32 tc = std::min<uint32>(1024, rowsInGroup - row);
			uint8 *src = &decodeBuf[row * rowSize];

			// run predictors
			for(const auto& predictor : predictors)
				predictor->Decode(src, rowSize, tc);

			// run decoder
			decoder.Decode(src, &hebuf[1], tc);

			uint32 nextCycle = hebuf[tc].mCycle;
			if (nextCycle < lastCycle)
				tickBase += UINT64_C(0x100000000);
			lastCycle = nextCycle;

			if (procInit) {
				procInit = false;

				proc->Init(hebuf[1].mS, kATDebugDisasmMode_6502, 1, &ctx, true, false, nullptr, tsdecoder, true);

				if (tc)
					proc->ProcessInsns(tickBase + hebuf[tc].mCycle, &hebufptrs[1], tc - 1, tsdecoder);
			} else
				proc->ProcessInsns(tickBase + hebuf[tc].mCycle, hebufptrs.get(), tc, tsdecoder);

			hebuf[0] = hebuf[tc];
		}
	}

	proc->Shutdown();
}

void ATLoadTraceSimpleChannels(const ATSavedTraceRoot& root, ATTraceContext& ctx) {
	for(const auto& grp : root.mGroups) {
		if (!grp)
			continue;

		ATTraceGroup *traceGroup = nullptr;

		for(const auto& ch : grp->mChannels) {
			if (!ch)
				continue;

			ATSavedTraceEventChannelDetail *eventChannelDetail = atser_cast<ATSavedTraceEventChannelDetail *>(ch->mpDetail);
			if (!eventChannelDetail)
				continue;

			if (!traceGroup)
				traceGroup = ctx.mpCollection->AddGroup(grp->mName.c_str());

			vdrefptr traceChannel(new ATTraceChannelStringTable(0, ctx.mBaseTickScale, ch->mName.c_str()));
			traceGroup->AddChannel(traceChannel);

			for(const VDStringW& s : eventChannelDetail->mStringTable)
				traceChannel->AddString(s.c_str());

			if (!eventChannelDetail->mpEventBuffer)
				continue;

			const auto& buf = eventChannelDetail->mpEventBuffer->GetReadBuffer();
			const size_t numEvents = buf.size() / 20;
			if (buf.size() % 20)
				throw ATInvalidSaveStateException();

			const uint8 *src = buf.data();
			uint64 tickBase = 0;
			uint32 stringId = 0;
			size_t stringCount = eventChannelDetail->mStringTable.size();
			size_t colorCount = eventChannelDetail->mColorTable.size();
			uint8 colorId = 0;
			for(size_t i = 0; i < numEvents; ++i) {
				uint64 tickStartDelta = VDReadUnalignedLEU64(src);
				uint64 tickLen = VDReadUnalignedLEU64(src+8);
				uint32 indexDelta = VDReadUnalignedLEU32(src+16);
				src += 20;

				tickStartDelta = (tickStartDelta << 63) | (tickStartDelta >> 1);

				if (tickStartDelta &  UINT64_C(0x80000000'00000000))
					tickStartDelta ^= UINT64_C(0x7FFFFFFF'FFFFFFFF);

				uint8 colorIdDelta = indexDelta >> 24;
				colorIdDelta = (colorIdDelta << 7) | (colorIdDelta >> 1);
				if (colorIdDelta & 0x80)
					colorIdDelta ^= 0x7F;

				colorId += colorIdDelta;
				if (colorId >= colorCount)
					throw ATInvalidSaveStateException();

				uint32 stringIdDelta = indexDelta & 0xFFFFFF;
				stringIdDelta = (stringIdDelta << 31) | (stringIdDelta >> 1);
				if (stringIdDelta & 0x80000000)
					stringIdDelta ^= 0x7FFFFFFF;

				stringId += stringIdDelta;

				if (stringId >= stringCount)
					throw ATInvalidSaveStateException();

				tickBase += tickStartDelta;
				traceChannel->AddTickEvent(tickBase, tickBase + tickLen, stringId, eventChannelDetail->mColorTable[colorId] & 0xFFFFFF);
				tickBase += tickLen;
			}
		}
	}
}

void ATLoadTraceVideoChannel(const ATSavedTraceVideoChannelDetail& videoDetail, ATTraceContext& ctx) {
	vdrefptr videoChannel = ATCreateTraceChannelVideo(L"Video", &ctx.mMemTracker);

	ctx.mpCollection->AddGroup(L"Video", kATTraceGroupType_Video)->AddChannel(videoChannel->AsTraceChannel());

	if (!videoDetail.mpFrameInfo || !videoDetail.mpFrameBufferInfo)
		throw ATInvalidSaveStateException();

	const uint32 frameCount = videoDetail.mFrameCount;
	const uint32 fbCount = videoDetail.mFrameBufferCount;
	const uint32 fbsPerGroup = videoDetail.mFrameBuffersPerGroup;
	const uint32 fbGroupCount = (fbCount + (fbsPerGroup - 1)) / fbsPerGroup;
	bool usingV2Format = false;

	if (videoDetail.mFormat == L"yv12_delta")
		usingV2Format = true;
	else if (!videoDetail.mFormat.empty())
		throw ATInvalidSaveStateException();

	if (videoDetail.mFrameBufferGroups.size() != fbGroupCount)
		throw ATInvalidSaveStateException();

	const auto& frameInfoReadBuffer = videoDetail.mpFrameInfo->GetReadBuffer();
	const vdspan<const uint8> frameInfo(frameInfoReadBuffer.data(), frameInfoReadBuffer.size());
	if (frameInfo.size() != 12 * frameCount)
		throw ATInvalidSaveStateException();

	const auto& frameBufferInfoReadBuffer = videoDetail.mpFrameBufferInfo->GetReadBuffer();
	const vdspan<const uint8> fbInfo(frameBufferInfoReadBuffer.data(), frameBufferInfoReadBuffer.size());
	if (fbInfo.size() != (usingV2Format ? 16 : 12) * fbCount)
		throw ATInvalidSaveStateException();

	VDPixmapBuffer deltaBuffer;

	const uint8 *fbInfoSrc = fbInfo.data();
	for(uint32 fbGroupIdx = 0; fbGroupIdx < fbGroupCount; ++fbGroupIdx) {
		ATSaveStateMemoryBuffer *fbGroupDataBuf = videoDetail.mFrameBufferGroups[fbGroupIdx];
		if (!fbGroupDataBuf)
			throw ATInvalidSaveStateException();

		const auto& frameGroupReadBuffer = fbGroupDataBuf->GetReadBuffer();
		const uint8 *srcData = frameGroupReadBuffer.data();
		const size_t srcSize = frameGroupReadBuffer.size();
		const uint32 fbsInGroup = std::min<uint32>(fbsPerGroup, fbCount - fbsPerGroup * fbGroupIdx);

		for(uint32 i = 0; i < fbsInGroup; ++i) {
			const uint32 w = VDReadUnalignedLEU16(fbInfoSrc + 0);
			const uint32 h = VDReadUnalignedLEU16(fbInfoSrc + 2);

			if (!w || !h || (w & 1) || (h & 1))
				throw ATInvalidSaveStateException();

			if (w > 1024 || h > 1024)
				throw ATUnsupportedSaveStateException();

			const uint32 w2 = w >> 1;
			const uint32 h2 = h >> 1;

			const uint32 rawOffset = VDReadUnalignedLEU32(fbInfoSrc + 4);
			if (rawOffset >= srcSize)
				throw ATInvalidSaveStateException();

			fbInfoSrc += 8;

			vdrect32 r;
			if (usingV2Format) {
				r.left = VDReadUnalignedLEU16(fbInfoSrc + 0);
				r.top = VDReadUnalignedLEU16(fbInfoSrc + 2);
				r.right = VDReadUnalignedLEU16(fbInfoSrc + 4);
				r.bottom = VDReadUnalignedLEU16(fbInfoSrc + 6);
				fbInfoSrc += 8;

				if (r.left > r.right || r.top > r.bottom)
					throw ATInvalidSaveStateException();

				if (r.right > (sint32)w || r.bottom > (sint32)h)
					throw ATInvalidSaveStateException();

				if ((r.left | r.top | r.right | r.bottom) & 1)
					throw ATInvalidSaveStateException();

				const uint32 encw = r.width();
				const uint32 ench = r.height();
				const uint32 encw2 = encw >> 1;
				const uint32 ench2 = ench >> 1;

				const uint32 rawSize = 6*encw2*ench2 + 1;
				if (srcSize - rawOffset < rawSize)
					throw ATInvalidSaveStateException();

				const uint8 *VDRESTRICT src = srcData + rawOffset;
				const uint8 mode = *src++;

				if (mode >= 2)
					throw ATInvalidSaveStateException();

				// check if full frame is being updated
				const bool fullRect = r.left == 0
					&& r.top == 0
					&& r.right == w
					&& r.bottom == h;

				// partial and delta frame updates are only valid if the frame
				// size is not changing
				if (deltaBuffer.w != w || deltaBuffer.h != h) {
					if (!fullRect || mode)
						throw ATInvalidSaveStateException();
					
					deltaBuffer.init(w, h, nsVDPixmap::kPixFormat_YUV420_Planar_Centered);
				}

				if (encw && ench) {
					const uint8 *srcy = src;
					const uint8 *srccb = srcy + encw * ench;
					const uint8 *srccr = srccb + encw2 * ench2;
					uint8 *VDRESTRICT dsty = (uint8 *)deltaBuffer.data + r.left + r.top * deltaBuffer.pitch;
					uint8 *VDRESTRICT dstcb = (uint8 *)deltaBuffer.data2 + (r.left >> 1) + (r.top >> 1) * deltaBuffer.pitch2;
					uint8 *VDRESTRICT dstcr = (uint8 *)deltaBuffer.data3 + (r.left >> 1) + (r.top >> 1) * deltaBuffer.pitch3;

					if (mode) {
						// delta
						for(uint32 y = 0; y < ench; ++y) {
							for(uint32 x = 0; x < encw; ++x)
								dsty[x] ^= srcy[x];

							srcy += encw;
							dsty += deltaBuffer.pitch;
						}

						for(uint32 y = 0; y < ench2; ++y) {
							for(uint32 x = 0; x < encw2; ++x)
								dstcb[x] ^= srccb[x];

							srccb += encw2;
							dstcb += deltaBuffer.pitch2;
						}

						for(uint32 y = 0; y < ench2; ++y) {
							for(uint32 x = 0; x < encw2; ++x)
								dstcr[x] ^= srccr[x];

							srccr += encw2;
							dstcr += deltaBuffer.pitch3;
						}
					} else {
						// copy
						VDMemcpyRect(dsty, deltaBuffer.pitch, srcy, encw, encw, ench);
						VDMemcpyRect(dstcb, deltaBuffer.pitch2, srccb, encw2, encw2, ench2);
						VDMemcpyRect(dstcr, deltaBuffer.pitch3, srccr, encw2, encw2, ench2);
					}
				}

				videoChannel->AddRawFrameBuffer(deltaBuffer);
			} else {
				const uint32 rawSize = VDReadUnalignedLEU32(fbInfoSrc);
				fbInfoSrc += 4;

				if (srcSize - rawOffset < rawSize)
					throw ATInvalidSaveStateException();

				if (rawSize != w*h + 2*w2*h2)
					throw ATInvalidSaveStateException();

				VDPixmap px {};
				px.format = nsVDPixmap::kPixFormat_YUV420_Planar_Centered;
				px.w = w;
				px.h = h;
				px.data = (void *)(srcData + rawOffset);
				px.data2 = (void *)(srcData + rawOffset + w*h);
				px.data3 = (void *)(srcData + rawOffset + w*h + w2*h2);
				px.pitch = w;
				px.pitch2 = w2;
				px.pitch3 = w2;

				videoChannel->AddRawFrameBuffer(px);
			}
		}

		fbGroupDataBuf->ReleaseReadBuffer();
	}

	const uint8 *frameInfoSrc = frameInfo.data();
	uint64 tick = 0;
	uint32 frameBufferIndex = 0;
	for(uint32 i = 0; i < frameCount; ++i) {
		const uint64 tickDelta = VDReadUnalignedLEU64(frameInfoSrc);
		const uint32 frameBufferIndexDelta = VDReadUnalignedLEU32(frameInfoSrc + 8);
		frameInfoSrc += 12;

		tick += tickDelta;
		frameBufferIndex += frameBufferIndexDelta;

		if (frameBufferIndex >= fbCount)
			throw ATInvalidSaveStateException();

		videoChannel->AddFrame((double)tick * ctx.mBaseTickScale, frameBufferIndex);
	}
}

vdrefptr<ATTraceCollection> ATLoadTrace(const IATSerializable& ser, const vdfunction<void(int, int)>& progressFn) {
	const auto& root = atser_cast<const ATSavedTraceRoot&>(ser);

	if (root.mGroups.empty())
		throw ATInvalidSaveStateException();

	vdrefptr<ATTraceCollection> traceColl(new ATTraceCollection);

	ATTraceContext ctx {};
	ctx.mBaseTime = 0;
	ctx.mBaseTickScale = 4.0f / 7159090.0f;		// will be fixed up from frames channel
	ctx.mpCollection = traceColl;

	// will be fixed up from frame channel
	ATCPUTimestampDecoder tsdecoder;
	tsdecoder.mCyclesPerFrame = 114 * 262;

	// search for the frame channel first -- this is critical to setting timing
	for(const auto& grp : root.mGroups) {
		if (!grp)
			continue;

		for(const auto& ch : grp->mChannels) {
			if (!ch)
				continue;

			const auto *frameDetail = atser_cast<ATSavedTraceFrameChannelDetail *>(ch->mpDetail);
			if (frameDetail) {
				ATLoadTraceFrameChannel(*frameDetail, ctx, tsdecoder);
			}
		}
	}

	// process CPU and video channels
	for(const auto& grp : root.mGroups) {
		if (!grp)
			continue;

		for(const auto& ch : grp->mChannels) {
			if (!ch)
				continue;

			const auto *cpuDetail = atser_cast<ATSavedTraceCPUChannelDetail *>(ch->mpDetail);
			if (cpuDetail) {
				ATLoadTraceCPUChannel(*cpuDetail, ctx, tsdecoder, progressFn);
				continue;
			}

			const auto *videoDetail = atser_cast<ATSavedTraceVideoChannelDetail *>(ch->mpDetail);
			if (videoDetail) {
				ATLoadTraceVideoChannel(*videoDetail, ctx);
			}
		}
	}

	// process all normal channels
	ATLoadTraceSimpleChannels(root, ctx);

	return traceColl;
}
