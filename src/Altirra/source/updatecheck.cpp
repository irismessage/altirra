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
#include <vd2/system/vdstl.h>
#include <at/atcore/asyncdispatcher.h>
#include <at/atcore/configvar.h>
#include "asyncdownloader.h"
#include "updatecheck.h"
#include "updatefeed.h"
#include "versioninfo.h"

ATConfigVarBool g_ATCVUpdateUseLocalTestUrl("update.use_local_test_url", false);

////////////////////////////////////////////////////////////////////////////////

VDStringW ATUpdateGetFeedUrl(bool useTestChannel) {
	return VDStringW(
		g_ATCVUpdateUseLocalTestUrl
			? (useTestChannel ? AT_UPDATE_CHECK_URL_LOCAL_TEST : AT_UPDATE_CHECK_URL_LOCAL_REL)
			: (useTestChannel ? AT_UPDATE_CHECK_URL_TEST : AT_UPDATE_CHECK_URL_REL)
	);
}

bool ATUpdateIsTestChannelDefault() {
	return AT_UPDATE_USE_TEST_CHANNEL;
}

////////////////////////////////////////////////////////////////////////////////

class ATUpdateChecker {
public:
	ATUpdateChecker(IATAsyncDispatcher& dispatcher);

	void Init(bool useTestChannel);
	void Shutdown();

	void AddCallback(ATUpdateInfoCallback fn);

private:
	void Process();

	IATAsyncDispatcher& mDispatcher;
	vdrefptr<IATAsyncDownloader> mpDownloader;
	vdfastvector<char> mData;
	uint64 mCallbackToken = 0;

	vdvector<ATUpdateInfoCallback> mCallbacks;
};

ATUpdateChecker::ATUpdateChecker(IATAsyncDispatcher& dispatcher)
	: mDispatcher(dispatcher)
{
}

void ATUpdateChecker::Init(bool useTestChannel) {
	ATAsyncDownloadUrl(ATUpdateGetFeedUrl(useTestChannel).c_str(), AT_HTTP_USER_AGENT, 4 * 1024 * 1024,
		[this](const void *data, size_t len) {
			if (data)
				mData.assign((const char *)data, (const char *)data + len);

			mDispatcher.Queue(&mCallbackToken, [this] { Process(); });
		},
		~mpDownloader
	);
}

void ATUpdateChecker::Shutdown() {
	if (mpDownloader) {
		mpDownloader->Cancel();
		mpDownloader = nullptr;
	}

	mDispatcher.Cancel(&mCallbackToken);
}

void ATUpdateChecker::AddCallback(ATUpdateInfoCallback fn) {
	mCallbacks.emplace_back(fn);
}

void ATUpdateChecker::Process() {
	ATUpdateFeedInfo feedInfo;
	bool success = feedInfo.Parse(mData.data(), mData.size());

	auto callbacks(std::move(mCallbacks));
	mCallbacks.clear();
	
	for(const auto& fn : callbacks)
		fn(success ? &feedInfo : nullptr);
}

////////////////////////////////////////////////////////////////////////////////

ATUpdateChecker *g_ATUpdateChecker;

void ATUpdateInit(bool useTestChannel, IATAsyncDispatcher& dispatcher, ATUpdateInfoCallback fn) {
	if (!g_ATUpdateChecker) {
		g_ATUpdateChecker = new ATUpdateChecker(dispatcher);
		g_ATUpdateChecker->Init(useTestChannel);
	}

	g_ATUpdateChecker->AddCallback(std::move(fn));
}

void ATUpdateShutdown() {
	if (g_ATUpdateChecker) {
		g_ATUpdateChecker->Shutdown();
		delete g_ATUpdateChecker;
		g_ATUpdateChecker = nullptr;
	}
}
