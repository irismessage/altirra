//	Altirra RMT - POKEY emulator plugin for Raster Music Tracker
//	Copyright (C) 2022 Avery Lee
//
//	This library is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This library is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#include <stdafx.h>
#include <vd2/system/thread.h>
#include <vd2/system/vdalloc.h>
#include <at/ataudio/audiooutput.h>
#include <at/ataudio/pokey.h>
#include <at/ataudio/pokeytables.h>
#include <at/atcore/wraptime.h>
#include <windows.h>
#include "rmtbypass.h"

class ATRMTPokeyContext final : public IATRMTBypassLink, public IATPokeyEmulatorConnections, public IATSchedulerCallback {
public:
	ATRMTPokeyContext();

	void InitPokeys(bool stereo);
	void InitReferenceClock(uint32 ticksPerSec, bool pal);
	void Advance(uint32 ticks);

	uint8 DebugReadByte(uint16 addr) const;
	void WriteByte(uint16 addr, uint8 v);

	bool IsLinked() const { return mbLinked; }
	void LinkInit(uint32 t0) override;
	uint8 LinkDebugReadByte(uint32 t, uint32 address) override;
	uint8 LinkReadByte(uint32 t, uint32 address) override;
	void LinkWriteByte(uint32 t, uint32 address, uint8 v) override;

	void PokeyAssertIRQ(bool cpuBased) override;
	void PokeyNegateIRQ(bool cpuBased) override;
	void PokeyBreak() override;
	bool PokeyIsInInterrupt() const override;
	bool PokeyIsKeyPushOK(uint8 scanCode, bool cooldownExpired) const override;

	void OnScheduledEvent(uint32 id) override;

private:
	void LinkAdvanceTo(uint32 t);

	static constexpr float kMachineClockNTSC = 1789772.5f;
	static constexpr float kMachineClockPAL =  1773447.0f;

	vdautoptr<ATPokeyEmulator> mPokeys[2];
	vdautoptr<IATAudioOutput> mpAudioOutput;
	uint32 mNumPokeys = 0;
	float mCyclesPerSec = kMachineClockNTSC;
	float mCyclesPerTick = 0.0f;
	float mCycleAccum = 0;
	ATEvent *mpEventEOF = nullptr;
	bool mbLinked = false;
	uint32 mLastLinkTime = 0;
	uint32 mLastAdvanceTime = 0;
	sint32 mStereoCounter = 0;
	uint8 mRMTShadowRegs[0x20] {};
	sint32 mMuteCounters[2][4] {};

	ATScheduler mScheduler;
	ATPokeyTables mPokeyTables;
};

ATRMTPokeyContext::ATRMTPokeyContext() {
	 mpAudioOutput = ATCreateAudioOutput();
	 mpAudioOutput->Init(nullptr, nullptr);
	 mpAudioOutput->InitNativeAudio();
	 mpAudioOutput->SetVolume(0.5f);

	 mScheduler.SetEvent(114 * 262, this, 1, mpEventEOF);
}

void ATRMTPokeyContext::InitPokeys(bool stereo) {
	if (mNumPokeys == 0) {
		mPokeys[0] = new ATPokeyEmulator(false);
		mPokeys[0]->Init(this, &mScheduler, mpAudioOutput, &mPokeyTables);
		mPokeys[0]->ColdReset();
		mPokeys[0]->WriteByte(0x0F, 0x03);
		mNumPokeys = 1;
	}

	if (stereo && mNumPokeys < 2) {
		mPokeys[1] = new ATPokeyEmulator(true);
		mPokeys[1]->Init(this, &mScheduler, mpAudioOutput, &mPokeyTables);
		mPokeys[1]->ColdReset();
		mPokeys[1]->WriteByte(0x0F, 0x03);

		mPokeys[0]->SetSlave(mPokeys[1]);
		mNumPokeys = 2;
	}

	if (!stereo && mNumPokeys > 1) {
		mPokeys[0]->SetSlave(nullptr);
		mPokeys[1] = nullptr;
		mNumPokeys = 1;
	}
}

void ATRMTPokeyContext::InitReferenceClock(uint32 ticksPerSec, bool pal) {
	mCyclesPerSec = pal ? kMachineClockPAL : kMachineClockNTSC;
	mCyclesPerTick = mCyclesPerSec / (float)ticksPerSec;
	mpAudioOutput->SetCyclesPerSecond(mCyclesPerSec, 1.0f);
}

void ATRMTPokeyContext::Advance(uint32 ticks) {
	if (!mNumPokeys)
		return;

	if (mbLinked) {
		for(uint32 pok = 0; pok < mNumPokeys; ++pok) {
			ATPokeyRegisterState rstate;
			mPokeys[pok]->GetRegisterState(rstate);

			for(uint32 i = 0; i < 4; ++i) {
				bool c6502_muted = !(rstate.mReg[i*2+1] & 0x0F);
				bool rmt_muted = !(mRMTShadowRegs[pok*16+i*2+1] & 0x0F);

				if (!c6502_muted) {
					if (rmt_muted)
						mMuteCounters[pok][i] = std::min<sint32>(mMuteCounters[pok][i] + 1, 20);
					else
						mMuteCounters[pok][i] = std::max<sint32>(mMuteCounters[pok][i] - 1, 0);
				}

				mPokeys[pok]->SetChannelEnabled(i, mMuteCounters[pok][i] < 10);
			}
		}
	}

	mCycleAccum += mCyclesPerTick * (float)ticks;

	if (mCycleAccum >= 1.0f) {
		uint32 cyc = (uint32)mCycleAccum;
		mCycleAccum -= (float)cyc;

		const uint32 t0 = mScheduler.GetTick();
		const uint32 tlimit = mLastAdvanceTime + cyc;

		if (ATWrapTime{tlimit} > t0) {
			mScheduler.SetStopTime(tlimit);

			while(mScheduler.mNextEventCounter != 0)
				ATSCHEDULER_ADVANCE_N(&mScheduler, ATSCHEDULER_GETTIMETONEXT(&mScheduler));

			mLastAdvanceTime = tlimit;
		} else
			mLastAdvanceTime = t0;

		mScheduler.UpdateTick64();
	}
}

uint8 ATRMTPokeyContext::DebugReadByte(uint16 addr) const {
	return mPokeys[0]->DebugReadByte((uint8)addr);
}

void ATRMTPokeyContext::WriteByte(uint16 addr, uint8 v) {
	if (mbLinked)
		mRMTShadowRegs[addr & 0x1F] = v;
	else
		mPokeys[0]->WriteByte((uint8)addr, v);
}

void ATRMTPokeyContext::LinkInit(uint32 t0) {
	mLastLinkTime = t0;
	mbLinked = true;
}

uint8 ATRMTPokeyContext::LinkDebugReadByte(uint32 t, uint32 address) {
	LinkAdvanceTo(t);

	return mPokeys[0] ? mPokeys[0]->DebugReadByte((uint8)address) : 0xFF;
}

uint8 ATRMTPokeyContext::LinkReadByte(uint32 t, uint32 address) {
	LinkAdvanceTo(t);

	return mPokeys[0] ? mPokeys[0]->ReadByte((uint8)address) : 0xFF;
}

void ATRMTPokeyContext::LinkWriteByte(uint32 t, uint32 address, uint8 v) {
	LinkAdvanceTo(t);

	if (mNumPokeys < 2 && (address & 0x10))
		return;

	if (mPokeys[0])
		mPokeys[0]->WriteByte((uint8)address, v);
}

void ATRMTPokeyContext::LinkAdvanceTo(uint32 t) {
	if (ATWrapTime{t} < mLastLinkTime)
		return;

	uint32 cyc = t - mLastLinkTime;
	mLastLinkTime = t;

	const uint32 t0 = mScheduler.GetTick();
	const uint32 tlimit = t0 + cyc;

	mScheduler.SetStopTime(tlimit);

	while(mScheduler.mNextEventCounter != 0)
		ATSCHEDULER_ADVANCE_N(&mScheduler, ATSCHEDULER_GETTIMETONEXT(&mScheduler));
}

void ATRMTPokeyContext::PokeyAssertIRQ(bool cpuBased) {
}

void ATRMTPokeyContext::PokeyNegateIRQ(bool cpuBased) {
}

void ATRMTPokeyContext::PokeyBreak() {
}

bool ATRMTPokeyContext::PokeyIsInInterrupt() const {
	return false;
}

bool ATRMTPokeyContext::PokeyIsKeyPushOK(uint8 scanCode, bool cooldownExpired) const {
	return true;
}

void ATRMTPokeyContext::OnScheduledEvent(uint32 id) {
	mpEventEOF = mScheduler.AddEvent(114 * 262, this, 1);

	if (mbLinked) {
		if (memcmp(mRMTShadowRegs, mRMTShadowRegs + 0x10, 9))
			++mStereoCounter;
		else
			--mStereoCounter;

		mStereoCounter = std::clamp(mStereoCounter, -30, 30);

		bool wantStereo = mStereoCounter > 0;

		if ((mNumPokeys > 1) != wantStereo)
			InitPokeys(wantStereo);
	}

	mPokeys[0]->AdvanceFrame(true, mScheduler.GetTick64());
}

////////////////////////////////////////////////////////////////////////////////

ATRMTPokeyContext *g_pATRMTPokeyContext;

// This is not actually an RMT endpoint, but we include it in case someone else
// wants to use this DLL and can offer more controlled shutdown.
extern "C" void __declspec(dllexport) __cdecl Pokey_Shutdown() {
	delete g_pATRMTPokeyContext;
	g_pATRMTPokeyContext = nullptr;
}

extern "C" void __declspec(dllexport) __cdecl Pokey_Initialise(int *argc, char *argv[]) {
	if (g_pATRMTPokeyContext)
		return;

	g_pATRMTPokeyContext = new ATRMTPokeyContext;

	HMODULE hmsvcrt = GetModuleHandleW(L"msvcrt");
	if (hmsvcrt) {
		FARPROC fp = GetProcAddress(hmsvcrt, "atexit");
		if (fp) {
			((int (__cdecl *)(void (__cdecl *)()))fp)(
				[] {
					Pokey_Shutdown();
				}
			);
		}
	}

	// pin ourselves -- we can't allow ourselves to be unloaded before sa_c6502.dll,
	// like RMT tries to do
	HMODULE h;
	(void)GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)Pokey_Initialise, &h);

	HMODULE hcpu = GetModuleHandleW(L"sa_c6502");
	if (hcpu) {
		FARPROC fp = GetProcAddress(hcpu, "AltirraRMT_LinkFromPOKEY");

		if (fp)
			((void (__cdecl *)(IATRMTBypassLink&))fp)(*g_pATRMTPokeyContext);
	}
}

extern "C" void __declspec(dllexport) __cdecl Pokey_SoundInit(uint32 freq17, uint16 playback_freq, uint8 num_pokeys) {
	if (!g_pATRMTPokeyContext)
		return;

	g_pATRMTPokeyContext->InitPokeys(num_pokeys >= 2);

	freq17 = 1773447;

	// RMT uses an NTSC rate that is slightly off (1789790). The ideal values
	// are 1789772.5 and 1773447; we split the difference as the threshold for
	// detecting PAL clock.
	g_pATRMTPokeyContext->InitReferenceClock(playback_freq, freq17 < 1781610);
}

extern "C" void __declspec(dllexport) __cdecl Pokey_Process(uint8 * sndbuffer, const uint16 sndn) {
	memset(sndbuffer, 0x80, sndn);

	if (g_pATRMTPokeyContext)
		g_pATRMTPokeyContext->Advance(sndn / 2);
}

extern "C" uint8 __declspec(dllexport) __cdecl Pokey_GetByte(uint16 addr) {
	if (!g_pATRMTPokeyContext)
		return 0xFF;

	return g_pATRMTPokeyContext->DebugReadByte(addr);
}

extern "C" void __declspec(dllexport) __cdecl Pokey_PutByte(uint16 addr, uint8 byte) {
	if (g_pATRMTPokeyContext)
		g_pATRMTPokeyContext->WriteByte(addr, byte);
}

extern "C" void __declspec(dllexport) __cdecl Pokey_About(char **name, char **author, char **description) {
	*name = const_cast<char *>("Altirra POKEY emulation v1.01 for RASTER Music Tracker (Altirra 4.00 core)");
	*author = const_cast<char *>("Avery Lee");
	*description = const_cast<char *>("Pair with Altirra sa_c6502.dll for RMT for cycle-precise operation.");
}

extern "C" __declspec(dllexport) IATRMTBypassLink * __cdecl AltirraRMT_LinkFromCPU() {
	return g_pATRMTPokeyContext;
}
