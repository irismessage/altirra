//	Altirra - Atari 800/800XL/5200 emulator
//	Compiler - relocatable module creator
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include <stdafx.h>

class ATCompileCmdMakeReloc4 {
public:
	int Run(int argc, const char *const *argv);

private:
	void BuildRelocs();

	using File = std::unique_ptr<FILE, decltype([](FILE *f) { fclose(f); })>;
	File mOutputFile;

	using VecU8 = std::vector<uint8>;
	using SpanU8 = std::span<uint8>;

	struct Segment {
		uint32 mBaseAddr;
		VecU8 mData;
	};

	struct SegList {
		std::vector<Segment> mAbsSegments;
	};

	SegList mRelocSeg[4];

	std::vector<uint32> mLowRelocOffsets;
	std::vector<std::pair<uint32, uint8>> mHighRelocOffsets;
	std::vector<uint32> mWordRelocOffsets;

	struct Symbol {
		uint8 mSymbol[8];

		struct SymNameStr {
			char s[9];

			const char *c_str() const { return s; }
		};

		SymNameStr str() const {
			SymNameStr s;
			memcpy(s.s, mSymbol, 8);
			int len = 8;
			while(len && s.s[len-1] == ' ')
				--len;
			s.s[len] = 0;
			return s;
		}
	};

	struct SymDef : public Symbol {
		uint32 mOffset;
	};

	struct SymbolHashPred {
		size_t operator()(const Symbol& def) const {
			uint64_t hash;
			memcpy(&hash, def.mSymbol, 8);

			if constexpr(sizeof(size_t) > 4)
				return (size_t)hash;
			else
				return (size_t)(hash + (hash >> 32));
		}

		bool operator()(const Symbol& a, const Symbol& b) const {
			return !memcmp(a.mSymbol, b.mSymbol, 8);
		}
	};

	std::unordered_set<SymDef, SymbolHashPred, SymbolHashPred> mSymbolDefs;

	struct SymRef : public Symbol{
		std::vector<uint32> mOffsets;
	};

	std::unordered_set<SymRef, SymbolHashPred, SymbolHashPred> mSymbolRefs;
};

int cmd_makereloc4(int argc, const char *const *argv) {
	ATCompileCmdMakeReloc4 cmd;
	return cmd.Run(argc, argv);
}

int ATCompileCmdMakeReloc4::Run(int argc, const char *const *argv) {
	if (argc < 6)
		fail("makereloc4 requires 4x input and an output filename");

	VecU8 objs[4];

	static constexpr uint32 kLoadAddrs[4]={
		0x2800,
		0x2801,
		0xa800,
		0x2800
	};

	for(int i=0; i<4; ++i) {
		VecU8 obj = ATCReadFileContents(argv[i+1]);
		SpanU8 data(obj);

		uint32 segstart = 0;
		uint32 segend = 0;
		while(!data.empty()) {
			if (data.size() < 2)
				fail("error while parsing SpartaDOS X obj: %s", argv[i+1]);

			uint16 code = data[0] + data[1]*256;
			data = data.subspan(2);

			if (code == 0xFFFA) {			// SDX absolute segment
				if (data.size() < 4)
					fail("error while parsing SpartaDOS X obj: %s", argv[i+1]);

				segstart = data[0] + data[1]*256;
				segend = data[2] + data[3]*256;
				data = data.subspan(4);

				if (segend < segstart)
					fail("invalid SDX absolute segment: %s", argv[i+1]);

				const uint32 seglen = (uint32)segend - segstart + 1;
				if (data.size() < seglen)
					fail("invalid SDX absolute segment: %s", argv[i+1]);

				if (mRelocSeg[i].mAbsSegments.empty() && segstart != kLoadAddrs[i])
					fail("first segment has unexpected load address of %04X: %s", segstart, argv[i+1]);

				Segment& seg = mRelocSeg[i].mAbsSegments.emplace_back();
				seg.mBaseAddr = segstart;
				seg.mData.assign(data.begin(), data.begin() + seglen);

				data = data.subspan(seglen);
			} else if (code == 0xFFFB) {	// SDX symbol reference segment
				if (data.size() < 11)
					fail("invalid symbol reference: %s", argv[i+1]);

				SymRef *ref = nullptr;

				if (i == 0) {
					Symbol sym;
					memcpy(sym.mSymbol, data.data(), 8);
					auto r = mSymbolRefs.emplace(sym);
					ref = const_cast<SymRef *>(&*r.first);
				}

				const uint16 offset = data[8] + data[9]*256;
				if (offset)
					fail("non-zero offset found in symbol reference data: %s", argv[i+1]);

				data = data.subspan(10);

				bool addrset = false;
				uint32 reladdr = 0;
				for(;;) {
					if (data.empty())
						fail("invalid symbol reference data: %s", argv[i+1]);

					uint8 code = data[0];
					data = data.subspan(1);

					if (code < 0xFA) {
						if (!addrset)
							fail("invalid symbol reference data (no target segment set): %s", argv[i+1]);

						reladdr += code;

						if (ref)
							ref->mOffsets.push_back(reladdr);
					} else if (code == 0xFC)
						break;
					else if (code == 0xFD) {
						if (data.size() < 2)
							fail("invalid symbol reference data (not enough data): %s", argv[i+1]);

						addrset = true;
						reladdr = data[0] + data[1]*256;
						data = data.subspan(2);
						
						if (ref)
							ref->mOffsets.push_back(reladdr);
					} else if (code == 0xFE)
						fail("invalid symbol reference data (relocatable target found): %s", argv[i+1]);
					else if (code == 0xFF)
						reladdr += 0xF0;
					else
						fail("invalid symbol reference data (invalid code): %s", argv[i+1]);
				}

				if (ref)
					std::sort(ref->mOffsets.begin(), ref->mOffsets.end());
			} else if (code == 0xFFFC) {	// SDX symbol definition segment
				if (data.size() < 10)
					fail("invalid symbol definition: %s", argv[i+1]);

				if (!i) {
					SymDef def;
					memcpy(def.mSymbol, data.data(), 8);
					def.mOffset = data[9] + data[10] * 256;

					auto r = mSymbolDefs.emplace(def);
					if (!r.second)
						fail("duplicate symbol definition: %s", argv[i+1]);
				}

				data = data.subspan(10);
			} else if (code == 0xFFFD) {	// SDX relocation fixup segment
				fail("unexpected relocation fixup segment: %s", argv[i+1]);
			} else if (code == 0xFFFE) {	// SDX relocatable segment
				fail("unexpected relocatable segment: %s", argv[i+1]);
			} else if (code == 0xFFFF) {
				fail("unexpected DOS executable segment: %s", argv[i+1]);
			} else {
				fail("error parsing obj: %s", argv[i+1]);
			}
		}

		// check segment consistency
		if (i) {
			SegList& baseList = mRelocSeg[0];
			SegList& curList = mRelocSeg[i];

			if (baseList.mAbsSegments.size() != curList.mAbsSegments.size())
				fail("segment counts don't match with base: %s", argv[i+1]);

			size_t numSegs = baseList.mAbsSegments.size();
			for(size_t j=0; j<numSegs; ++j) {
				const Segment& baseSeg = baseList.mAbsSegments[j];
				const Segment& curSeg = curList.mAbsSegments[j];

				if ((j && baseSeg.mBaseAddr != curSeg.mBaseAddr) || baseSeg.mData.size() != curSeg.mData.size()) {
					fail("segment mismatch: %s, %04X-%04X != %04X-%04X"
						, argv[i+1]
						, baseSeg.mBaseAddr
						, baseSeg.mBaseAddr + (unsigned)baseSeg.mData.size() - 1
						, curSeg.mBaseAddr
						, curSeg.mBaseAddr + (unsigned)curSeg.mData.size() - 1
					);
				}
			}
		} else {
			if (mRelocSeg[0].mAbsSegments.empty())
				fail("base obj has no absolute segments: %s", argv[i+1]);
		}
	}

	// add a special symbol for the relocation data start, using the original size before reldata
	mSymbolDefs.emplace(SymDef { { '_', '_', 'R', 'E', 'L', 'D', 'A', 'T' }, 0x2800 + (uint32)mRelocSeg[0].mAbsSegments[0].mData.size() });

	// compute relocations
	BuildRelocs();

	// compute rel range, with reldata
	const uint32 relbase = 0x2800;
	const uint32 relsize = (uint32)mRelocSeg[0].mAbsSegments[0].mData.size();

	// match special refs to defs
	for(const SymRef& ref : mSymbolRefs) {
		if (ref.mSymbol[0] != '_' || ref.mSymbol[1] != '_')
			continue;

		if (ref.mOffsets.empty())
			continue;

		SymDef lookupDef(ref);
		auto it = mSymbolDefs.find(lookupDef);
		if (it != mSymbolDefs.end()) {
			const SymDef& def = *it;
			bool reloc = false;

			if (def.mOffset >= relbase && (def.mOffset - relbase) <= relsize) {
				reloc = true;
				mWordRelocOffsets.insert(mWordRelocOffsets.end(), ref.mOffsets.begin(), ref.mOffsets.end());
			}

			for(uint32 offset : ref.mOffsets) {
				bool found = false;

				for(Segment& seg : mRelocSeg[0].mAbsSegments) {
					if (offset >= seg.mBaseAddr) {
						const uint32 segRelOffset = offset - seg.mBaseAddr;
						if (segRelOffset + 1 < seg.mData.size()) {
							uint16 v = seg.mData[segRelOffset] + seg.mData[segRelOffset+1]*256;

							if (reloc)
								v += def.mOffset - 0x2800;
							else
								v += def.mOffset;

							seg.mData[segRelOffset] = (uint8)v;
							seg.mData[segRelOffset+1] = (uint8)(v >> 8);

							found = true;
							break;
						}
					}
				}

				if (!found)
					fail("invalid relocation offset %04X for segment '%s'", offset, ref.str().c_str());
			}
		}
	}

	// build SDX executable - start with relocatable segment
	VecU8 exebin;

	// emit any remaining absolute segments
	bool isRelSeg = true;
	for(const Segment& seg : mRelocSeg[0].mAbsSegments) {
		const uint32 segstart = seg.mBaseAddr;
		const uint32 segend = segstart + (uint32)seg.mData.size() - 1;

		if (isRelSeg) {
			exebin.push_back(0xFE);		// SDX relocatable segment
			exebin.push_back(0xFF);
			exebin.push_back(0x01);		// segment number
			exebin.push_back(0x00);		// flags: load to main memory
			exebin.push_back(0x00);
			exebin.push_back(0x00);
			exebin.push_back((uint8)seg.mData.size());
			exebin.push_back((uint8)(seg.mData.size() >> 8));
			exebin.insert(exebin.end(), seg.mData.begin(), seg.mData.end());
		} else {
			exebin.push_back(0xFA);		// SDX relocatable segment
			exebin.push_back(0xFF);
			exebin.push_back((uint8)segstart);
			exebin.push_back((uint8)(segstart >> 8));
			exebin.push_back((uint8)segend);
			exebin.push_back((uint8)(segend >> 8));
			exebin.insert(exebin.end(), seg.mData.begin(), seg.mData.end());
		}

		// look for and emit any symbol definitions for this segment
		for(const SymDef& def : mSymbolDefs) {
			if (def.mSymbol[0] == '_' && def.mSymbol[1] == '_')
				continue;

			if (def.mOffset >= segstart && def.mOffset <= segend) {
				const uint32 symoff = def.mOffset - segstart;

				exebin.push_back(0xFC);
				exebin.push_back(0xFF);
				exebin.insert(exebin.end(), std::begin(def.mSymbol), std::end(def.mSymbol));
				exebin.push_back((uint8)symoff);
				exebin.push_back((uint8)(symoff >> 8));
			}
		}

		isRelSeg = false;
	}

	// emit word relocations
	const auto emitRelocs = [relbase, relsize, &exebin](const auto& relocs) {
		uint32 lastOffset = 0xFFFF0000;
		bool lastRel = false;

		for(uint32 offset : relocs) {
			const bool isRel = offset >= relbase && (offset - relbase) < relsize;

			if (isRel) {
				if (!lastRel) {
					lastOffset = relbase;

					exebin.push_back(0xFE);
					exebin.push_back(0x01);
				}
			} else {
				if (lastRel || offset - lastOffset >= 500) {
					lastOffset = offset;
					lastRel = isRel;
					exebin.push_back(0xFD);
					exebin.push_back((uint8)offset);
					exebin.push_back((uint8)(offset >> 8));
					continue;
				}
			}

			lastRel = isRel;

			while(offset - lastOffset >= 250) {
				lastOffset += 250;
				exebin.push_back(0xFF);
			}

			exebin.push_back((uint8)(offset - lastOffset));
			lastOffset = offset;
		}

		exebin.push_back(0xFC);
	};

	if (!mWordRelocOffsets.empty()) {
		std::sort(mWordRelocOffsets.begin(), mWordRelocOffsets.end());

		exebin.push_back(0xFD);
		exebin.push_back(0xFF);
		exebin.push_back(0x01);		// # of block to fix up
		exebin.push_back(0x00);		// fixup block size (or 0)
		exebin.push_back(0x00);

		emitRelocs(mWordRelocOffsets);
	}

	// emit symbol references
	for(const SymRef& ref : mSymbolRefs) {
		if (ref.mSymbol[0] == '_' && ref.mSymbol[1] == '_')
			continue;

		if (ref.mOffsets.empty())
			continue;

		exebin.push_back(0xFB);
		exebin.push_back(0xFF);
		exebin.insert(exebin.end(), std::begin(ref.mSymbol), std::end(ref.mSymbol));
		exebin.push_back(0);
		exebin.push_back(0);

		emitRelocs(ref.mOffsets);
	}

	// open output files
	mOutputFile.reset(fopen(argv[5], "wb"));
	if (!mOutputFile)
		fail("cannot open output file: %s", argv[5]);

	if (1 != fwrite(exebin.data(), exebin.size(), 1, mOutputFile.get()) || fclose(mOutputFile.release()))
		fail("cannot write output file: %s", argv[5]);

	printf("%s: %d lo relocs, %d word relocs, %d hi relocs\n", argv[5], (int)mLowRelocOffsets.size(), (int)mWordRelocOffsets.size(), (int)mHighRelocOffsets.size());

	return 0;
}

void ATCompileCmdMakeReloc4::BuildRelocs() {
	const SpanU8 srcbase(mRelocSeg[0].mAbsSegments[0].mData);
	const SpanU8 srclow(mRelocSeg[1].mAbsSegments[0].mData);
	const SpanU8 srchigh(mRelocSeg[2].mAbsSegments[0].mData);
	const SpanU8 srchighlow(mRelocSeg[3].mAbsSegments[0].mData);

	const auto len = srcbase.size();

	for(uint32 i = 0; i < len; ++i) {
		const uint8 vbase = srcbase[i];

		if (vbase == srclow[i]) {
			if (i+1 < len && (srcbase[i+1]^srchigh[i+1])&0x80)
				mHighRelocOffsets.emplace_back(i + 0x2801, srchighlow[i+1]);
		} else {
			if (i+1 < len && ((srcbase[i+1]^srchigh[i+1])&0x80))
				mWordRelocOffsets.push_back(i + 0x2800);
			else if (!((vbase ^ srchigh[i])&0x80))
				mLowRelocOffsets.push_back(i + 0x2800);
		}
	}

	// re-relocate segment to $0000
	for(const uint32 wordOffset : mWordRelocOffsets) {
		srcbase[wordOffset+1-0x2800] -= 0x28;
	}

	for(const auto [highOffset, lobyte] : mHighRelocOffsets) {
		srcbase[highOffset-0x2800] -= 0x28;
	}

	// encode high and low relocations at the end of the main segment
	VecU8& baseData = mRelocSeg[0].mAbsSegments[0].mData;

	size_t origLen = baseData.size();

	{
		uint32 pageOffset = 0x2800;
		size_t countOffset = baseData.size();
		baseData.push_back(0);

		for(const uint32 offset : mLowRelocOffsets) {
			while(pageOffset + 0x100 < offset) {
				pageOffset += 0x100;

				countOffset = baseData.size();
				baseData.push_back(0);
			}

			++baseData[countOffset];
			baseData.push_back((uint8)(offset - pageOffset));
		}

		if (baseData[countOffset])
			baseData.push_back(0xFF);
		else
			baseData[countOffset] = 0xFF;
	}

	{
		uint32 pageOffset = 0x2800;
		size_t countOffset = baseData.size();
		baseData.push_back(0);

		for(const auto [offset, lobyte] : mHighRelocOffsets) {
			while(pageOffset + 0x100 < offset) {
				pageOffset += 0x100;

				countOffset = baseData.size();
				baseData.push_back(0);
			}

			++baseData[countOffset];
			baseData.push_back((uint8)(offset - pageOffset));
			baseData.push_back(lobyte);
		}

		if (baseData[countOffset])
			baseData.push_back(0xFF);
		else
			baseData[countOffset] = 0xFF;
	}

	if (baseData.size() - origLen > 256)
		fail("lo/hi relocation data exceeds 256 bytes");
}
