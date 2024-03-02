#include <stdafx.h>
#include <vd2/system/vdalloc.h>
#include <at/atcore/wraptime.h>
#include <at/atcpu/breakpoints.h>
#include <at/atcpu/co6502.h>
#include <at/atcpu/execstate.h>
#include <at/atcpu/memorymap.h>
#include <windows.h>
#include "rmtbypass.h"

class ATRMTCPUContext final : public IATCPUBreakpointHandler {
public:
	void Link(IATRMTBypassLink& link);
	bool IsLinked() const { return mpLink != nullptr; }

	bool CheckBreakpoint(uint32 pc) override {
		if (mCPU.GetS() < 3) {
			mbExit = true;
			return true;
		}

		return false;
	}

public:
	ATCoProc6502 mCPU { false, false };
	ATScheduler mScheduler;
	ATCPUExecState mExecState;
	uint8 *mpMemory = nullptr;
	bool mbExit = false;
	IATRMTBypassLink *mpLink = nullptr;

	ATCoProcReadMemNode mPokeyReadNode;
	ATCoProcWriteMemNode mPokeyWriteNode;

	bool mBreakpointMap[0x10000] {};
};

void ATRMTCPUContext::Link(IATRMTBypassLink& link) {
	if (mpLink)
		return;

	mpLink = &link;

	mPokeyReadNode.mpThis = this;
	mPokeyReadNode.mpRead = [](uint32 addr, void *thisptr0) {
		ATRMTCPUContext *thisptr = (ATRMTCPUContext *)thisptr0;

		return thisptr->mpLink ? thisptr->mpLink->LinkReadByte(thisptr->mScheduler.GetTick(), addr) : thisptr->mpMemory[addr & 0xFFFF];
	};

	mPokeyReadNode.mpDebugRead = [](uint32 addr, void *thisptr0) {
		ATRMTCPUContext *thisptr = (ATRMTCPUContext *)thisptr0;

		return thisptr->mpLink ? thisptr->mpLink->LinkDebugReadByte(thisptr->mScheduler.GetTick(), addr) : thisptr->mpMemory[addr & 0xFFFF];
	};

	mPokeyWriteNode.mpThis = this;
	mPokeyWriteNode.mpWrite = [](uint32 addr, uint8 v, void *thisptr0) {
		ATRMTCPUContext *thisptr = (ATRMTCPUContext *)thisptr0;

		thisptr->mpMemory[addr & 0xFFFF] = v;

		if (thisptr->mpLink)
			thisptr->mpLink->LinkWriteByte(thisptr->mScheduler.GetTick(), addr, v);
	};

	ATCoProcMemoryMapView view(mCPU.GetReadMap(), mCPU.GetWriteMap());

	view.SetHandlers(0xD2, 0x01, mPokeyReadNode, mPokeyWriteNode);

	mpLink->LinkInit(mScheduler.GetTick());
}

vdautoptr<ATRMTCPUContext> g_pATRMTContext;

extern "C" void __declspec(dllexport) __cdecl C6502_Initialise(uint8 *memory) {
	g_pATRMTContext = new ATRMTCPUContext;
	g_pATRMTContext->mpMemory = memory;

	ATCoProcMemoryMapView view(g_pATRMTContext->mCPU.GetReadMap(), g_pATRMTContext->mCPU.GetWriteMap());
	view.SetReadMem(0, 0x100, memory);
	view.SetWriteMem(0, 0x100, memory);

	g_pATRMTContext->mCPU.SetBreakpointMap(g_pATRMTContext->mBreakpointMap, g_pATRMTContext);

	for(auto& bp : g_pATRMTContext->mBreakpointMap)
		bp = true;

	g_pATRMTContext->mCPU.ColdReset();
	g_pATRMTContext->mCPU.GetExecState(g_pATRMTContext->mExecState);

	HMODULE hpok = GetModuleHandleW(L"sa_pokey");
	if (hpok) {
		FARPROC fp = GetProcAddress(hpok, "AltirraRMT_LinkFromCPU");

		if (fp) {
			IATRMTBypassLink *link = ((IATRMTBypassLink *(__cdecl *)())fp)();

			if (link)
				g_pATRMTContext->Link(*link);
		}
	}
}

extern "C" int __declspec(dllexport) __cdecl C6502_JSR(uint16 *adr, uint8 *areg, uint8 *xreg, uint8 *yreg, int *maxcycles) {
	if (!g_pATRMTContext)
		return -1;

	g_pATRMTContext->mExecState.m6502.mA = *areg;
	g_pATRMTContext->mExecState.m6502.mX = *xreg;
	g_pATRMTContext->mExecState.m6502.mY = *yreg;
	g_pATRMTContext->mExecState.m6502.mS = 0xFF;
	g_pATRMTContext->mCPU.SetExecState(g_pATRMTContext->mExecState);
	g_pATRMTContext->mCPU.Jump(*adr);

	const uint32 t0 = g_pATRMTContext->mScheduler.GetTick();
	const uint32 tlimit = t0 + *maxcycles;
	uint8 stoppingInsn = 0;

	// we don't actually want this, but it's required
	g_pATRMTContext->mScheduler.SetStopTime(tlimit + 1000);

	g_pATRMTContext->mbExit = false;

	for(;;) {
		while(g_pATRMTContext->mCPU.Run(g_pATRMTContext->mScheduler))
			;

		const uint32 t = g_pATRMTContext->mScheduler.GetTick();
		if (g_pATRMTContext->mbExit) {
			*maxcycles -= (int)(t - t0);

			stoppingInsn = 0x60;
			break;
		}

		if (ATWrapTime{t} >= tlimit) {
			*maxcycles -= (int)(t - t0);
			stoppingInsn = g_pATRMTContext->mpMemory[g_pATRMTContext->mCPU.GetPC()];
			break;
		}
	}

	g_pATRMTContext->mCPU.GetExecState(g_pATRMTContext->mExecState);

	*adr = g_pATRMTContext->mExecState.m6502.mPC;
	*areg = g_pATRMTContext->mExecState.m6502.mA;
	*xreg = g_pATRMTContext->mExecState.m6502.mX;
	*yreg = g_pATRMTContext->mExecState.m6502.mY;

	g_pATRMTContext->mScheduler.UpdateTick64();

	return stoppingInsn;
}

extern "C" void __declspec(dllexport) __cdecl C6502_About(char **name, char **author, char **description) {
	*name = const_cast<char *>("Altirra 6502 emulation v1.00 for RASTER Music Tracker (Altirra 4.00 core)");
	*author = const_cast<char *>("Avery Lee");
	*description = const_cast<char *>("Pair with Altirra sa_pokey.dll for RMT for cycle-precise operation.");
}

extern "C" void __declspec(dllexport) __cdecl AltirraRMT_LinkFromPOKEY(IATRMTBypassLink& link) {
	if (g_pATRMTContext)
		g_pATRMTContext->Link(link);
}
