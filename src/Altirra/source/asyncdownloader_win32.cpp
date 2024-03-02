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

#include <stdafx.h>
#include <vd2/system/function.h>
#include <vd2/system/refcount.h>
#include <windows.h>
#include <wininet.h>

#include "asyncdownloader.h"

#pragma comment(lib, "wininet")

#pragma optimize("", off)

class ATAsyncDownloaderW32 final : public vdrefcounted<IATAsyncDownloader> {
public:
	bool Init(const wchar_t *url, const wchar_t *userAgent, size_t maxLen, vdfunction<void(const void *, size_t)> fn);
	void Shutdown();

	void Cancel() override { Shutdown(); }

private:
	void StatusCallback(HINTERNET h, DWORD status, LPVOID statusInfo, DWORD statusLen);
	void QueueRead();

	void Signal(const void *data, size_t len);

	DWORD mRequestedReadLen = 0;
	DWORD mActualReadLen = 0;
	vdfastvector<char> mBuffer;
	size_t mMaxLen = 0;

	VDCriticalSection mMutex;
	HINTERNET mhInternet = nullptr;
	HINTERNET mhUrlRequest = nullptr;
	HINTERNET mhUrlRequestRef = nullptr;
	bool mbInSyncCall = false;
	bool mbShutdownRequested = false;
	vdfunction<void(const void *, size_t)> mCompletionFn;
};

bool ATAsyncDownloaderW32::Init(const wchar_t *url, const wchar_t *userAgent, size_t maxLen, vdfunction<void(const void *, size_t)> fn) {
	mCompletionFn = std::move(fn);
	mMaxLen = maxLen;

	mhInternet = InternetOpen(userAgent, INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, INTERNET_FLAG_ASYNC);
	if (!mhInternet) {
		Signal(nullptr, 0);
		Shutdown();
		return false;
	}

	InternetSetStatusCallbackW(mhInternet,
		[](HINTERNET h, DWORD_PTR context, DWORD status, LPVOID statusInfo, DWORD statusLen) {
			return ((ATAsyncDownloaderW32 *)context)->StatusCallback(h, status, statusInfo, statusLen);
		}
	);

	ULONG timeout = 5000;
	InternetSetOptionW(mhInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof timeout);

	VDVERIFY(!InternetOpenUrlW(mhInternet, url, nullptr, 0, 0, (DWORD_PTR)this));
	if (GetLastError() != ERROR_IO_PENDING) {
		Signal(nullptr, 0);
		Shutdown();
		return false;
	}

	return true;
}

void ATAsyncDownloaderW32::Shutdown() {
	mMutex.Lock();
	HINTERNET hUrlRequest = mhUrlRequest;
	mhUrlRequest = nullptr;
	mCompletionFn = nullptr;
	mMutex.Unlock();

	// WinINet only allows for soft-closing of handles, but we can't change
	// the callback once async operations is in flight, we can't change the
	// callback on the root handle until the request handle is already active,
	// and callbacks don't work reliably on the root handle since the callback
	// context isn't set.
	//
	// To make this sanely reliable, we trigger a soft-close on the request
	// handle, and keep the object alive until we receive its handle closing
	// callback. At that point, we strip the callback from the root handle,
	// trigger a soft-close on it, then release the object. The root handle
	// should close immediately, but if it doesn't we're OK since we removed
	// the callback.
	//
	if (hUrlRequest)
		InternetCloseHandle(hUrlRequest);
}

void ATAsyncDownloaderW32::StatusCallback(HINTERNET h, DWORD status, LPVOID statusInfo, DWORD statusLen) {
	if (status == INTERNET_STATUS_REQUEST_COMPLETE) {
		const INTERNET_ASYNC_RESULT *result = (const INTERNET_ASYNC_RESULT *)statusInfo;

		if (!result->dwResult) {
			Signal(nullptr, 0);
			Shutdown();
			return;
		}

		// If this is the open request, check the status code and fail out if we got a 4xx or 5xx,
		// we don't want to process the error message as data.
		if (!mRequestedReadLen) {
			DWORD code = 0;
			DWORD index = 0;
			DWORD len = sizeof code;
			if (!HttpQueryInfoW(mhUrlRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &code, &len, &index) || code >= 400) {
				Signal(nullptr, 0);
				Shutdown();
				return;
			}
		}

		// check if we got some data, in which case we should queue another read
		if (mActualReadLen || !mRequestedReadLen) {
			QueueRead();
			return;
		}

		// EOF -- signify completion and shut down
		mCompletionFn(mBuffer.data(), mBuffer.size() - mRequestedReadLen + mActualReadLen);
		Shutdown();
	} else if (status == INTERNET_STATUS_HANDLE_CREATED) {
		HINTERNET h = *(HINTERNET *)statusInfo;

		vdsynchronized(mMutex) {
			mhUrlRequest = h;
			mhUrlRequestRef = h;
		}

		// pin for mhUrlRequestRef
		AddRef();
	} else if (status == INTERNET_STATUS_HANDLE_CLOSING) {
		HINTERNET h = *(HINTERNET *)statusInfo;
		bool doRelease = false;

		HINTERNET hInternet = nullptr;

		// We may get CLOSING directly if the connection fails, so make sure
		// to signal failure if we haven't done so already.
		Signal(nullptr, 0);

		vdsynchronized(mMutex) {
			if (mhUrlRequestRef == h) {
				mhUrlRequestRef = nullptr;
				doRelease = true;

				hInternet = mhInternet;
				mhInternet = nullptr;
			}
		}

		if (doRelease) {
			if (hInternet) {
				InternetSetStatusCallback(hInternet, nullptr);
				InternetCloseHandle(hInternet);
			}

			Release();
		}
	}
}

void ATAsyncDownloaderW32::QueueRead() {
	for(;;) {
		if (mBuffer.size() > mMaxLen) {
			Signal(nullptr, 0);
			Shutdown();
			break;
		}

		mBuffer.resize(mBuffer.size() - mRequestedReadLen + mActualReadLen + 65536);

		mRequestedReadLen = 65536;
		mActualReadLen = 0;

		HINTERNET hUrlRequest = nullptr;
		vdsynchronized(mMutex) {
			mbInSyncCall = true;
			hUrlRequest = mhUrlRequest;
		}

		if (!hUrlRequest)
			break;

		BOOL success = InternetReadFile(mhUrlRequest, mBuffer.data() + (mBuffer.size() - mRequestedReadLen), mRequestedReadLen, &mActualReadLen);

		bool doShutdown = false;
		vdsynchronized(mMutex) {
			mbInSyncCall = false;
			doShutdown = mbShutdownRequested;
			mbShutdownRequested = false;
		}

		if (doShutdown) {
			Shutdown();
			break;
		}
		
		if (!success) {
			DWORD err = GetLastError();
			if (err != ERROR_IO_PENDING) {
				mCompletionFn(nullptr, 0);
				Shutdown();
			}

			break;
		}

		if (!mActualReadLen) {
			Signal(mBuffer.data(), mBuffer.size() - mRequestedReadLen + mActualReadLen);
			Shutdown();
			break;
		}
	}
}

void ATAsyncDownloaderW32::Signal(const void *data, size_t len) {
	vdsynchronized(mMutex) {
		auto fn(std::move(mCompletionFn));
		mCompletionFn = nullptr;

		if (fn)
			fn(data, len);

	}
}

////////////////////////////////////////////////////////////////////////////////

void ATAsyncDownloadUrl(const wchar_t *url, const wchar_t *userAgent, size_t maxLen, vdfunction<void(const void *, size_t)> fn, IATAsyncDownloader **downloader) {
	vdrefptr p(new ATAsyncDownloaderW32);

	if (downloader) {
		p->AddRef();
		*downloader = p;
	}

	p->Init(url, userAgent, maxLen, std::move(fn));
}
