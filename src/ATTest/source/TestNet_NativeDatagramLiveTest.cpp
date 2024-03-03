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
#include <vd2/system/thread.h>
#include <at/atcore/asyncdispatcherimpl.h>
#include <at/atnetworksockets/nativesockets.h>
#include <test.h>

AT_DEFINE_TEST_NONAUTO(Net_LiveDatagramReceiveIPv4) {
	AT_TEST_ASSERT(ATSocketInit());

	vdrefptr sock = ATNetBind(ATSocketAddress::CreateIPv4(4789), false);
	AT_TEST_ASSERT(sock);

	printf("Listening on port 4789\n");

	VDSignal dispWake;
	ATAsyncDispatcher disp;
	disp.SetWakeCallback([&] { dispWake.signal(); });

	sock->SetOnEvent(
		&disp,
		[sockPtr = &*sock](const ATSocketStatus& status) {
			if (status.mError != ATSocketError::None) {
				printf("socket error\n");
			}

			if (status.mbCanRead) {
				ATSocketAddress addr;
				unsigned counter = 0;

				for(;;) {
					sint32 len = sockPtr->RecvFrom(addr, &counter, 4);
					if (len < 0)
						break;

					printf("Received %u bytes from %s: %u\n", len, addr.ToString().c_str(), counter);
				}
			}
		},
		true
	);

	ATTestBeginTestLoop();
	while(ATTestShouldContinueTestLoop()) {
		dispWake.tryWait(1000);
		disp.RunCallbacks();
	}

	sock->CloseSocket(true);
	sock = nullptr;

	ATSocketShutdown();

	return 0;
}

AT_DEFINE_TEST_NONAUTO(Net_LiveDatagramReceiveIPv6) {
	AT_TEST_ASSERT(ATSocketInit());

	vdrefptr sock = ATNetBind(ATSocketAddress::CreateIPv6(4789), true);
	AT_TEST_ASSERT(sock);

	printf("Listening on port 4789\n");

	VDSignal dispWake;
	ATAsyncDispatcher disp;
	disp.SetWakeCallback([&] { dispWake.signal(); });

	sock->SetOnEvent(
		&disp,
		[sockPtr = &*sock](const ATSocketStatus& status) {
			if (status.mError != ATSocketError::None) {
				printf("socket error\n");
			}

			if (status.mbCanRead) {
				ATSocketAddress addr;
				unsigned counter = 0;

				for(;;) {
					sint32 len = sockPtr->RecvFrom(addr, &counter, 4);
					if (len < 0)
						break;

					printf("Received %u bytes from %s: %u\n", len, addr.ToString().c_str(), counter);
				}
			}
		},
		true
	);

	ATTestBeginTestLoop();
	while(ATTestShouldContinueTestLoop()) {
		dispWake.tryWait(1000);
		disp.RunCallbacks();
	}

	sock->CloseSocket(true);
	sock = nullptr;

	ATSocketShutdown();

	return 0;
}

AT_DEFINE_TEST_NONAUTO(Net_LiveDatagramSendIPv4) {
	AT_TEST_ASSERT(ATSocketInit());

	vdrefptr sock = ATNetBind(ATSocketAddress::CreateIPv4(), false);
	AT_TEST_ASSERT(sock);

	printf("Sending on port 4789\n");

	uint32 counter = 0;

	ATTestBeginTestLoop();
	while(ATTestShouldContinueTestLoop()) {
		++counter;
		if (!sock->SendTo(ATSocketAddress::CreateLocalhostIPv4(4789), &counter, 4))
			printf("failed to send\n");
		else
			printf("sent %u\n", counter);
		VDThreadSleep(1000);
	}

	sock->CloseSocket(true);
	sock = nullptr;

	ATSocketShutdown();

	return 0;
}

AT_DEFINE_TEST_NONAUTO(Net_LiveDatagramSendIPv6) {
	AT_TEST_ASSERT(ATSocketInit());

	vdrefptr sock = ATNetBind(ATSocketAddress::CreateIPv6(), false);
	AT_TEST_ASSERT(sock);

	printf("Sending on port 4789\n");

	uint32 counter = 0;

	ATTestBeginTestLoop();
	while(ATTestShouldContinueTestLoop()) {
		++counter;
		if (!sock->SendTo(ATSocketAddress::CreateLocalhostIPv6(4789), &counter, 4))
			printf("failed to send\n");
		else
			printf("sent %u\n", counter);
		VDThreadSleep(1000);
	}

	sock->CloseSocket(true);
	sock = nullptr;

	ATSocketShutdown();

	return 0;
}
