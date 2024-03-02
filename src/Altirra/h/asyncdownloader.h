//	Altirra - Atari 800/800XL/5200 emulator
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
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#ifndef f_AT_ASYNCDOWNLOADER_H
#define f_AT_ASYNCDOWNLOADER_H

#include <vd2/system/function.h>
#include <vd2/system/refcount.h>

class IATAsyncDownloader : public IVDRefCount {
public:
	virtual void Cancel() = 0;
};

// Request a background download of the given URL. The callback is invoked asynchronously
// with either the retrieved buffer or null/0 on failure. The download is failed
// if it would exceed the max length, to avoid overflowing.
void ATAsyncDownloadUrl(const wchar_t *url, const wchar_t *userAgent, size_t maxLen, vdfunction<void(const void *, size_t)> fn, IATAsyncDownloader **downloader);

#endif
