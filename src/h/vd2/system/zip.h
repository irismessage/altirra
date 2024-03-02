//	VirtualDub - Video processing and capture application
//	System library component
//	Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
//
//	Beginning with 1.6.0, the VirtualDub system library is licensed
//	differently than the remainder of VirtualDub.  This particular file is
//	thus licensed as follows (the "zlib" license):
//
//	This software is provided 'as-is', without any express or implied
//	warranty.  In no event will the authors be held liable for any
//	damages arising from the use of this software.
//
//	Permission is granted to anyone to use this software for any purpose,
//	including commercial applications, and to alter it and redistribute it
//	freely, subject to the following restrictions:
//
//	1.	The origin of this software must not be misrepresented; you must
//		not claim that you wrote the original software. If you use this
//		software in a product, an acknowledgment in the product
//		documentation would be appreciated but is not required.
//	2.	Altered source versions must be plainly marked as such, and must
//		not be misrepresented as being the original software.
//	3.	This notice may not be removed or altered from any source
//		distribution.

#ifndef f_ZIP_H
#define f_ZIP_H

// Rest in peace, Phil Katz.

#include <vd2/system/vdtypes.h>
#include <vd2/system/file.h>
#include <vd2/system/file.h>
#include <vd2/system/VDString.h>
#include <string.h>
#include <vector>

class VDDeflateEncoder;

class VDDeflateBitReader {
public:
	void init(IVDStream *pSrc, uint64 limit) {
		mpSrc = pSrc;
		mBytesLeft = limit;
		refill();
		consume(0);
	}

	IVDStream *stream() const {
		return mpSrc;
	}

	unsigned long peek() const {
		return accum;
	}

	bool consume(unsigned n) {
//		printf("%08lx/%d\n", accum << ((-bits)&7), bits);
		bits -= n;

		if ((int)bits < 0)
			return false;

		accum >>= n;

		while(bits <= 24 && (mBufferPt || refill())) {
			accum += mBuffer[kBufferSize + mBufferPt++] << bits;
			bits += 8;
		}

		return true;
	}

	bool refill();

	bool getbit() {
		unsigned rv = accum;

		consume(1);

		return (rv&1) != 0;
	}

	unsigned getbits(unsigned n) {
		unsigned rv = accum & ((1<<n)-1);

		consume(n);

		return rv;
	}

	bool empty() const {
		return bits != 0;
	}

	unsigned avail() const {
		return bits;
	}

	unsigned bitsleft() const {
		return bits + (mBytesLeftLimited<<3);
	}

	unsigned bytesleft() const {
		return (bits>>3) + mBytesLeftLimited;
	}

	void align() {
		consume(bits&7);
	}

	void readbytes(void *dst, unsigned len);

protected:
	enum { kBigAvailThreshold = 16777216 };
	enum { kBufferSize = 256 };

	unsigned	accum = 0;
	unsigned	bits = 0;
	int			mBufferPt = 0;			// counts from -256 to 0
	uint64		mBytesLeft = 0;
	unsigned	mBytesLeftLimited = 0;

	IVDStream *mpSrc = nullptr;
	uint8	mBuffer[kBufferSize];
};

class VDCRCTable {
public:
	enum : uint32 {
		kCRC32		= 0xEDB88320		// CRC-32 used by PKZIP, PNG (x^32 + x^26 + x^23 + x^22 + x^16 + x^12 + x^11 + x^10 + x^8 + x^7 + x^5 + x^4 + x^2 + x^1 + 1)
	};

	VDCRCTable() = default;
	explicit VDCRCTable(uint32 crc) { Init(crc); }

	void Init(uint32 crc);

	uint32 Process(uint32 crc, const void *src, size_t len) const;
	uint32 CRC(const void *src, size_t len) const {
		return ~Process(0xFFFFFFFF, src, len);
	}

	static const VDCRCTable CRC32;

private:
	uint32 mTable[256];

	constexpr void InitConst(uint32 crc);
	constexpr static VDCRCTable MakeConst(uint32 crc);
};

class VDCRCChecker {
public:
	VDCRCChecker(const VDCRCTable& table) : mValue(0xFFFFFFFF), mTable(table) {}

	void Init() { mValue = 0xFFFFFFFF; }
	void Process(const void *src, sint32 len);

	uint32 CRC() const { return ~mValue; }

protected:
	uint32	mValue;
	const VDCRCTable& mTable;
};

class VDInflateStream : public IVDStream {
public:
	VDInflateStream() : mCRCChecker(mCRCTable) {}

	void	Init(IVDStream *pSrc, uint64 limit, bool bStored);
	void	EnableCRC(uint32 crc = VDCRCTable::kCRC32) { mCRCTable.Init(crc); mbCRCEnabled = true; }
	uint32	CRC() { return mCRCChecker.CRC(); }

	const wchar_t *GetNameForError() final override;

	sint64	Pos() final override;
	void	Read(void *buffer, sint32 bytes) final override;
	sint32	ReadData(void *buffer, sint32 bytes) final override;
	void	Write(const void *buffer, sint32 bytes) final override;

protected:
	bool	ParseBlockHeader();
	bool	Inflate();

	VDDeflateBitReader mBits;					// critical -- make this first!
	uint32	mReadPt = 0;
	uint32	mWritePt = 0;
	uint32	mBufferLevel = 0;

	enum {
		kNoBlock,
		kStoredBlock,
		kDeflatedBlock
	} mBlockType = kNoBlock;

	uint32	mStoredBytesLeft = 0;
	bool	mbNoMoreBlocks = false;
	bool	mbCRCEnabled = false;

	sint64	mPos = 0;
	uint8	mBuffer[65536];

	uint16	mCodeDecode[32768];
	uint8	mCodeLengths[288 + 32];
	uint16	mDistDecode[32768];

	VDCRCChecker	mCRCChecker;
	VDCRCTable		mCRCTable;
};

class VDZipArchive {
public:
	struct FileInfo {
		VDString	mFileName;
		uint32		mCompressedSize;
		uint32		mUncompressedSize;
		uint32		mCRC32;
		bool		mbPacked;
	};

	VDZipArchive();
	~VDZipArchive();

	void Init(IVDRandomAccessStream *pSrc);

	sint32			GetFileCount();
	const FileInfo&	GetFileInfo(sint32 idx);
	IVDStream		*OpenRawStream(sint32 idx);

protected:
	struct FileInfoInternal : public FileInfo {
		uint32		mDataStart;
	};

	std::vector<FileInfoInternal>	mDirectory;
	IVDRandomAccessStream			*mpStream;
};

class VDZipStream final : public VDInflateStream {
public:
	VDZipStream() = default;

	VDZipStream(IVDStream *pSrc, uint64 limit, bool bStored) {
		Init(pSrc, limit, bStored);
	}
};

class VDGUnzipStream final : public VDInflateStream {
public:
	VDGUnzipStream() = default;
	VDGUnzipStream(IVDStream *pSrc, uint64 limit) {
		Init(pSrc, limit);
	}

	void Init(IVDStream *pSrc, uint64 limit);

	const char *GetFilename() const { return mFilename.c_str(); }

protected:
	VDStringA mFilename;
};

// Adapter stream for compressing data with the Deflate algorithm, as
// described in RFC1951. The child stream receives the raw Deflate
// stream; no header is added. A CRC-32 is computed on the uncompressed
// data, as needed for gzip and zip, but different than the Adler-32
// used by zlib and png.
//
// The compression heuristics are tuned toward generic binary data.
// PNG typically uses different heuristics aimed at longer matches,
// such as Z_FILTERED in zlib.
//
class VDDeflateStream final : public IVDStream {
	VDDeflateStream(const VDDeflateStream&) = delete;
	VDDeflateStream& operator=(const VDDeflateStream&) = delete;
public:
	VDDeflateStream(IVDStream& dest);
	~VDDeflateStream();

	// Reset the stream state to prepare for compressing another source
	// stream to the same destination stream. This must be called after
	// Finalize() for the previous stream.
	void Reset();

	// Flush any remaining data out, writing a partial block if necessary.
	// This must be called prior to destruction for the compressed
	// stream to be valid.
	void Finalize();

	// Return the CRC-32 of the uncompressed data. Only valid after
	// Finalize().
	uint32 GetCRC() const { return mCRCChecker.CRC(); }

public:
	const wchar_t *GetNameForError() override;

	sint64 Pos() override {
		return mPos;
	}

	void	Read(void *buffer, sint32 bytes) override;
	sint32	ReadData(void *buffer, sint32 bytes) override;
	void	Write(const void *buffer, sint32 bytes) override;

private:
	void PreProcessInput(const void *p, uint32 n);
	void WriteOutput(const void *p, uint32 n);

	IVDStream& mDestStream;
	VDDeflateEncoder *mpEncoder;
	sint64 mPos;

	VDCRCChecker mCRCChecker;
};

class IVDZipArchiveWriter {
public:
	virtual ~IVDZipArchiveWriter() = default;

	// Begin writing a new file to the archive using the given path; returns
	// a reference to stream to use for writing the uncompressed data. Only
	// one file can be written at a time. EndFile() must be called after the
	// file data is written. The path may contain subdirectories; the path
	// is normalized to zip file conventions.
	virtual VDDeflateStream& BeginFile(const wchar_t *path) = 0;

	// End writing a file and complete the file metadata.
	virtual void EndFile() = 0;

	// Complete the zip archive by writing the central directory.
	virtual void Finalize() = 0;
};

IVDZipArchiveWriter *VDCreateZipArchiveWriter(IVDStream& stream);

#endif
