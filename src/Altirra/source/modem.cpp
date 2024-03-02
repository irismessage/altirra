#include "stdafx.h"
#include "modem.h"
#include "scheduler.h"
#include "uirender.h"

namespace {
	enum {
		// 1.0s guard time
		kGuardTime = 7159090 / 4,
		kCommandTimeout = 7159090 * 30 / 4
	};
}

ATModemEmulator::ATModemEmulator()
	: mpScheduler(NULL)
	, mpDriver(NULL)
	, mpEventEnterCommandMode(NULL)
	, mpEventCommandModeTimeout(NULL)
	, mbCommandMode(false)
	, mbEchoMode(true)
{
}

ATModemEmulator::~ATModemEmulator() {
	Shutdown();
}

void ATModemEmulator::Init(ATScheduler *sched, IATUIRenderer *uir) {
	mpScheduler = sched;
	mpUIRenderer = uir;
	mLastWriteTime = ATSCHEDULER_GETTIME(sched);
	mTransmitIndex = 0;
	mTransmitLength = 0;

	mDeviceTransmitReadOffset = 0;
	mDeviceTransmitWriteOffset = 0;
	mDeviceTransmitLevel = 0;
	mbDeviceTransmitUnderflow = true;

	mCommandLength = 0;
	mbCommandMode = true;
	mbConnected = false;
	mbSuppressNoCarrier = false;
	mbConnectionFailed = false;
	mbNewConnectedState = 0;
}

void ATModemEmulator::Shutdown() {
	TerminateCall();

	if (mpScheduler) {
		if (mpEventEnterCommandMode) {
			mpScheduler->RemoveEvent(mpEventEnterCommandMode);
			mpEventEnterCommandMode = NULL;
		}

		if (mpEventCommandModeTimeout) {
			mpScheduler->RemoveEvent(mpEventCommandModeTimeout);
			mpEventCommandModeTimeout = NULL;
		}

		mpScheduler = NULL;
	}

	mpUIRenderer = NULL;
}

void ATModemEmulator::SetConfig(const ATRS232Config& config) {
	mConfig = config;

	if (mpDriver)
		mpDriver->SetConfig(config);
}

bool ATModemEmulator::Read(uint8& c) {
	if (mTransmitIndex < mTransmitLength) {
		c = mTransmitBuffer[mTransmitIndex++];

		if (mTransmitIndex >= mTransmitLength) {
			mTransmitIndex = 0;
			mTransmitLength = 0;
		}

		return true;
	}

	if (mbConnectionFailed) {
		mbConnectionFailed = false;

		if (mpUIRenderer)
			mpUIRenderer->SetModemConnection("");

		SendResponseNoAnswer();
		mbCommandMode = true;
	} else {
		bool nowConnected = (mbNewConnectedState != 0);

		if (mbConnected != nowConnected) {
			mbConnected = nowConnected;

			if (nowConnected) {
				SendResponseConnect();

				if (mpUIRenderer) {
					VDStringA str;
					str.sprintf("Connected to %s:%d", mAddress.c_str(), mPort);
					mpUIRenderer->SetModemConnection(str.c_str());
				}

				mbCommandMode = false;
			} else {
				if (mpUIRenderer)
					mpUIRenderer->SetModemConnection("");

				if (mbSuppressNoCarrier)
					mbSuppressNoCarrier = false;
				else
					SendResponseNoCarrier();

				mbCommandMode = true;
			}
		}
	}

	if (mbCommandMode)
		return false;


	return mpDriver->Read(&c, 1) != 0;
}

void ATModemEmulator::Write(uint8 c) {
	const uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
	uint32 delay = t - mLastWriteTime;
	mLastWriteTime = t;

	if ((c & 0x7f) == '+') {
		if (delay >= kGuardTime || mGuardCharCounter) {
			if (++mGuardCharCounter >= 3)
				mpScheduler->SetEvent(kGuardTime, this, 1, mpEventEnterCommandMode);
		}
	} else {
		mGuardCharCounter = 0;

		if (mpEventEnterCommandMode) {
			mpScheduler->RemoveEvent(mpEventEnterCommandMode);
			mpEventEnterCommandMode = NULL;
		}
	}

	if (mbCommandMode) {
		if (mbEchoMode) {
			if (mTransmitLength < sizeof mTransmitBuffer)
				mTransmitBuffer[mTransmitLength++] = c;
		}

		c &= 0x7F;

		if (c == 0x08) {
			if (mCommandLength > 0)
				--mCommandLength;
		} else if (c == 0x0D) {
			ParseCommand();
		} else if (mCommandLength < sizeof mCommandBuffer) {
			mCommandBuffer[mCommandLength++] = c;

			mpScheduler->SetEvent(kCommandTimeout, this, 2, mpEventCommandModeTimeout);
		}
	} else {
		mMutex.Lock();
		if (mDeviceTransmitLevel < sizeof mDeviceTransmitBuffer) {
			c &= 0x7f;
			mDeviceTransmitBuffer[mDeviceTransmitWriteOffset] = c;

			if (++mDeviceTransmitWriteOffset >= sizeof mDeviceTransmitBuffer)
				mDeviceTransmitWriteOffset = 0;

			++mDeviceTransmitLevel;

			if (c == 0x0D && mDeviceTransmitLevel < sizeof mDeviceTransmitBuffer) {
				mDeviceTransmitBuffer[mDeviceTransmitWriteOffset] = 0x0A;

				if (++mDeviceTransmitWriteOffset >= sizeof mDeviceTransmitBuffer)
					mDeviceTransmitWriteOffset = 0;

				++mDeviceTransmitLevel;
			}

			if (mbDeviceTransmitUnderflow) {
				if (mpDriver)
					OnWriteAvail(mpDriver);
			}
		}
		mMutex.Unlock();
	}
}

void ATModemEmulator::ParseCommand() {
	if (mpEventCommandModeTimeout) {
		mpScheduler->RemoveEvent(mpEventCommandModeTimeout);
		mpEventCommandModeTimeout = NULL;
	}

	uint32 len = mCommandLength;
	mCommandLength = 0;

	if (!len) {
		SendResponseOK();
		return;
	}

	// Valid commands must be at least two chars.
	if (len < 2) {
		SendResponseError();
		mLastCommand.assign((const char *)mCommandBuffer, (const char *)mCommandBuffer + len);
		return;
	}

	// A/ prefix replays last command.
	uint32 c0 = mCommandBuffer[0];
	uint32 c1 = mCommandBuffer[1];

	if ((c0 & 0xdf) == 'A' && c1 == '/') {
		len = mLastCommand.size();
		mLastCommand.copy((char *)mCommandBuffer, len);
		c0 = mCommandBuffer[0];
		c1 = mCommandBuffer[1];
	} else {
		mLastCommand.assign((const char *)mCommandBuffer, (const char *)mCommandBuffer + len);
	}

	// command must start with at or AT (NOT aT or At)
	if ((c0 ^ c1) & 0x20 || (c0 & 0xdf) != 'A' || (c1 & 0xdf) != 'T') {
		SendResponseError();
		return;
	}

	const char *s = (const char *)mCommandBuffer + 2;
	const char *t = (const char *)mCommandBuffer + len;

	for(;;) {
		// eat whitespace
		uint8 c;
		do {
			if (s == t) {
				SendResponseOK();
				return;
			}

			c = *s++;
		} while(c == ' ');

		// capture command
		const uint8 cmd = c;

		// eat more whitespace
		while(s != t && *s == ' ')
			++s;

		// capture a number
		bool hasNumber = false;
		uint32 number = 0;

		if (cmd != 'D') {
			if ((uint8)(*s - (uint8)'0') < 10) {
				hasNumber = true;

				do {
					number *= 10;
					number += (*s - '0');

					++s;
					if (s == t)
						break;

					c = *s;
				} while((uint8)(*s - (uint8)'0') < 10);
			}
		}

		switch(cmd) {
			case 'A':	// answer
				SendResponseNoAnswer();
				return;

			case 'B':	// select communication standard (ignored)
				break;

			case 'D':
				// eat more whitespace
				while(s != t && *s == ' ')
					++s;

				if (s == t) {
					SendResponseError();
					return;
				}

				// check for 'I' to indicate IP-based dialing (for compatibility with Atari800)
				if (*s == 'I') {
					// parse out hostname and port
					++s;

					while(s != t && *s == ' ')
						++s;

					const char *hostname = s;

					while(s != t && *s != ' ')
						++s;

					const char *hostnameend = s;

					while(s != t && *s == ' ')
						++s;

					if (s == t) {
						SendResponseError();
						return;
					}

					uint32 port = 0;
					if ((uint8)(*s - '0') >= 10) {
						SendResponseError();
						return;
					}

					for(;;) {
						port = (port * 10) + (*s - '0');

						++s;

						if (s == t)
							break;

						if ((uint8)(*s - '0') >= 10) {
							SendResponseError();
							return;
						}
					}

					if (port < 1 || port > 65535) {
						SendResponseError();
						return;
					}

					TerminateCall();

					mbConnectionFailed = false;

					mAddress.assign(hostname, hostnameend);
					mPort = port;

					mpDriver = ATCreateModemDriverTCP();
					mpDriver->SetConfig(mConfig);
					if (!mpDriver->Init(mAddress.c_str(), port, this)) {
						SendResponseError();
						return;
					}

					if (mpUIRenderer) {
						VDStringA str;
						str.sprintf("Connecting to %s:%d...", mAddress.c_str(), mPort);
						mpUIRenderer->SetModemConnection(str.c_str());
					}
				} else {
					SendResponseError();
					return;
				}
				return;

			case 'E':	// echo (number)
				if (!hasNumber || number >= 2) {
					SendResponseError();
					return;
				}

				mbEchoMode = (number != 0);
				break;

			case 'H':	// hook control (optional number)
				if (hasNumber && number >= 2) {
					SendResponseError();
					return;
				}

				mbSuppressNoCarrier = true;

				TerminateCall();
				break;

			case 'L':	// set speaker volume (ignored)
				if (!hasNumber || number >= 4) {
					SendResponseError();
					return;
				}
				break;

			case 'M':	// speaker control
				if (!hasNumber || number >= 4) {
					SendResponseError();
					return;
				}
				break;

			case 'O':	// on-hook
				if (!mbConnected)
					SendResponseError();
				else {
					SendResponseConnect();
					mbCommandMode = false;
				}
				return;

			case 'Q':	// quiet mode (not yet handled)
				break;

			case 'V':	// verbose mode (net yet handled)
				if (hasNumber && number >= 2) {
					SendResponseError();
					return;
				}
				break;

			case 'X':	// select call progress method (not yet handled)
				if (!hasNumber || number >= 5) {
					SendResponseError();
					return;
				}
				break;

			case 'Z':	// reset modem (number optional)
				break;

			default:
				SendResponseError();
				return;
		}
	}
}

void ATModemEmulator::SendResponseOK() {
	if (mTransmitLength > sizeof mTransmitBuffer - 6)
		return;

	memcpy(mTransmitBuffer + mTransmitLength, "\r\nOK\r\n", 6);
	mTransmitLength += 6;
}

void ATModemEmulator::SendResponseError() {
	if (mTransmitLength > sizeof mTransmitBuffer - 9)
		return;

	memcpy(mTransmitBuffer + mTransmitLength, "\r\nERROR\r\n", 9);
	mTransmitLength = 9;
}

void ATModemEmulator::SendResponseConnect() {
	if (mTransmitLength <= sizeof mTransmitBuffer - 11) {
		memcpy(mTransmitBuffer + mTransmitLength, "\r\nCONNECT\r\n", 11);
		mTransmitLength = 11;
	}
}

void ATModemEmulator::SendResponseNoCarrier() {
	if (mTransmitLength <= sizeof mTransmitBuffer - 14) {
		memcpy(mTransmitBuffer + mTransmitLength, "\r\nNO CARRIER\r\n", 14);
		mTransmitLength = 14;
	}
}

void ATModemEmulator::SendResponseNoAnswer() {
	if (mTransmitLength <= sizeof mTransmitBuffer - 13) {
		memcpy(mTransmitBuffer + mTransmitLength, "\r\nNO ANSWER\r\n", 13);
		mTransmitLength = 13;
	}
}

void ATModemEmulator::TerminateCall() {
	if (mpDriver) {
		mpDriver->Shutdown();
		delete mpDriver;
		mpDriver = NULL;

		if (mpUIRenderer)
			mpUIRenderer->SetModemConnection("");
	}

	mbNewConnectedState = false;
	mbConnected = false;
	mbDeviceTransmitUnderflow = true;
	mDeviceTransmitReadOffset = 0;
	mDeviceTransmitWriteOffset = 0;
	mDeviceTransmitLevel = 0;
}

void ATModemEmulator::OnScheduledEvent(uint32 id) {
	if (id == 1) {
		mpEventEnterCommandMode = NULL;

		if (mGuardCharCounter >= 3) {
			// enter command mode
			SendResponseOK();
			mGuardCharCounter = 0;
			mbCommandMode = true;
			mCommandLength = 0;
		}
	} else if (id == 2) {
		mpEventCommandModeTimeout = NULL;

		if (mbCommandMode && mCommandLength > 0) {
			mCommandLength = 0;

			SendResponseError();
		}
	}
}

void ATModemEmulator::OnReadAvail(IATModemDriver *sender, uint32 len) {
	// do nothing -- we poll this
}

void ATModemEmulator::OnWriteAvail(IATModemDriver *sender) {
	mMutex.Lock();
	for(;;) {
		uint32 tc = mDeviceTransmitLevel;

		if (!tc) {
			mbDeviceTransmitUnderflow = true;
			break;
		}

		if (mDeviceTransmitReadOffset + tc > sizeof mDeviceTransmitBuffer)
			tc = sizeof mDeviceTransmitBuffer - mDeviceTransmitReadOffset;

		tc = sender->Write(mDeviceTransmitBuffer + mDeviceTransmitReadOffset, tc);

		if (!tc)
			break;

		mbDeviceTransmitUnderflow = false;

		mDeviceTransmitReadOffset += tc;
		if (mDeviceTransmitReadOffset >= sizeof mDeviceTransmitBuffer)
			mDeviceTransmitReadOffset = 0;

		mDeviceTransmitLevel -= tc;
	}
	mMutex.Unlock();
}

void ATModemEmulator::OnEvent(IATModemDriver *sender, ATModemPhase phase, ATModemEvent event) {
	if (event == kATModemEvent_Connected)
		mbNewConnectedState = true;
	else {
		mbNewConnectedState = false;

		if (phase <= kATModemPhase_Connecting)
			mbConnectionFailed = true;
	}
}
