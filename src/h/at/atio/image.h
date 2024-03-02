//	Altirra - Atari 800/800XL/5200 emulator
//	I/O library - image common definitions
//	Copyright (C) 2008-2016 Avery Lee
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

#ifndef f_AT_ATIO_IMAGE_H
#define f_AT_ATIO_IMAGE_H

#include <optional>
#include <at/atcore/checksum.h>
#include <vd2/system/unknown.h>

class VDStringW;
struct ATCartLoadContext;
struct ATCassetteLoadContext;
class IVDRandomAccessStream;

enum ATImageType {
	kATImageType_None,
	kATImageType_Cartridge,
	kATImageType_Disk,
	kATImageType_Tape,
	kATImageType_Program,
	kATImageType_BasicProgram,
	kATImageType_SaveState,
	kATImageType_Zip,
	kATImageType_GZip,
	kATImageType_SAP,
	kATImageType_SaveState2
};

struct ATStateLoadContext {
	bool mbAllowKernelMismatch;
	bool mbKernelMismatchDetected;
	bool mbPrivateStateLoaded;
};

struct ATImageLoadContext {
	ATImageType mLoadType = kATImageType_None;
	ATCartLoadContext *mpCartLoadContext = nullptr;
	ATStateLoadContext *mpStateLoadContext = nullptr;
	ATCassetteLoadContext *mpCassetteLoadContext = nullptr;
	int mLoadIndex = -1;
};

class IATImage : public IVDRefUnknown {
public:
	virtual ATImageType GetImageType() const = 0;

	// Try to retrieve the CRC32 checksum or SHA256 digest associated with the image file
	// for the image. May be absent if the image does not have a backing file associated with
	// it, has been modified, or the calculation was skipped due to the image being too big.
	// CRC32 is the same as Zip and PNG and useful for quick ID in UI; SHA256 is preferred
	// for signature matching.
	virtual std::optional<uint32> GetImageFileCRC() const = 0;
	virtual std::optional<ATChecksumSHA256> GetImageFileSHA256() const = 0;
};

ATImageType ATGetImageTypeForFileExtension(const wchar_t *s);
ATImageType ATDetectImageType(const wchar_t *imagePath, IVDRandomAccessStream& stream);
bool ATImageLoadAuto(const wchar_t *origPath, const wchar_t *imagePath, IVDRandomAccessStream& stream, ATImageLoadContext *loadCtx, VDStringW *resultPath, bool *canUpdate, IATImage **ppImage);

#endif	// f_AT_ATIO_IMAGE_H
