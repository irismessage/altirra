//	Altirra - Atari 800/800XL/5200 emulator
//	I/O library - filesystem handler defines
//	Copyright (C) 2009-2016 Avery Lee
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
#include <vd2/system/file.h>
#include <vd2/system/strutil.h>
#include <at/atio/diskfs.h>
#include <at/atio/diskfsutil.h>

uint32 ATDiskRecursivelyExpandARCs(IATDiskFS& fs, uintptr parentKey, int nestingDepth) {
	uint32 totalExpanded = 0;
	vdfastvector<uint8> buf;
	vdfastvector<uint8> buf2;

	uint32 tempfnCounter = 1;

	// Preread the directory. Our filesystems don't allow modifications during
	// directory searches just yet.
	vdvector<ATDiskFSEntryInfo> arcEnts;
	vdfastvector<uint32> dirKeys;

	{
		ATDiskFSEntryInfo info;
		uintptr searchKey = fs.FindFirst(parentKey, info);
		try {
			do {
				size_t len = info.mFileName.size();

				if (info.mbIsDirectory)
					dirKeys.push_back(info.mKey);
				else if (len > 4 && !vdstricmp(info.mFileName.c_str() + len - 4, ".arc")) {
					arcEnts.push_back(info);
				}
			} while(fs.FindNext(searchKey, info));
			fs.FindEnd(searchKey);
		} catch(...) {
			fs.FindEnd(searchKey);
			throw;
		}
	}

	for(const auto& arcInfo : arcEnts) {
		// try to read and mount the ARChive
		fs.ReadFile(arcInfo.mKey, buf);

		VDMemoryStream ms(buf.data(), (uint32)buf.size());
		vdautoptr<IATDiskFS> arcfs;

		try {
			arcfs = ATDiskMountImageARC(ms, VDTextAToW(arcInfo.mFileName).c_str());
		} catch(ATDiskFSException) {
			// Hmm, we couldn't extract. Oh well, skip it.
		}

		if (arcfs) {
			// Hey, we mounted the ARChive. Try to create a temporary directory.
			VDStringA fn;
			uint32 tempDirKey = 0;
					
			for(int i=0; i<100; ++i) {
				fn.sprintf("arct%u.tmp", tempfnCounter++);

				try {
					tempDirKey = fs.CreateDir(parentKey, fn.c_str());
					break;
				} catch(const ATDiskFSException& e) {
					if (e.GetErrorCode() != kATDiskFSError_FileExists)
						throw;
				}
			}

			if (tempDirKey) {
				// Hey, we got a key. Okay, let's copy files one at a time.
				ATDiskFSEntryInfo info2;
				uintptr searchKey2 = arcfs->FindFirst(0, info2);

				try {
					do {
						arcfs->ReadFile(info2.mKey, buf2);

						uint32 fileKey = fs.WriteFile(tempDirKey, info2.mFileName.c_str(), buf2.data(), (uint32)buf2.size());

						if (info2.mbDateValid)
							fs.SetFileTimestamp(fileKey, info2.mDate);

					} while(arcfs->FindNext(searchKey2, info2));
					arcfs->FindEnd(searchKey2);
				} catch(...) {
					arcfs->FindEnd(searchKey2);
					throw;
				}

				// Delete the archive.
				fs.DeleteFile(arcInfo.mKey);

				// Rename the temporary directory to match the original archive.
				fs.RenameFile(tempDirKey, arcInfo.mFileName.c_str());

				// Set the timestamp on the directory to match the original archive.
				if (arcInfo.mbDateValid)
					fs.SetFileTimestamp(tempDirKey, arcInfo.mDate);

				// Queue this directory for a rescan in case there are sub-ARCs.
				dirKeys.push_back(tempDirKey);

				++totalExpanded;
			}
		}
	}

	// Recursively all subdirectories.
	if (nestingDepth < 32) {
		for(uint32 subDirKey : dirKeys)
			totalExpanded += ATDiskRecursivelyExpandARCs(fs, subDirKey, nestingDepth + 1);
	}

	return totalExpanded;
}

uint32 ATDiskRecursivelyExpandARCs(IATDiskFS& fs) {
	return ATDiskRecursivelyExpandARCs(fs, 0, 0);
}
