//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2018 Avery Lee
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
#include <vd2/system/vdalloc.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <at/atcore/deviceparent.h>
#include <at/atcore/propertyset.h>
#include <at/atdebugger/argparse.h>
#include <at/atui/uicommandmanager.h>
#include "console.h"
#include "debugger.h"
#include "devicemanager.h"
#include "options.h"
#include "simulator.h"
#include "uiaccessors.h"
#include "uidisplay.h"

extern ATSimulator g_sim;

namespace {
	vdhashmap<VDStringA, VDStringA> g_ATAutotestMacros;
	VDStringA g_ATAutotestDesc;
	vdfastvector<size_t> g_ATAutotestDescBreaks;
	int g_ATAutotestDescColumnSize;
}

bool ATUISaveFrame(const wchar_t *path);

void ATDebuggerCmdAutotestAssert(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdExprNum exprArg(true);
	ATDebuggerCmdString exprMessage(false);
	parser >> exprArg >> exprMessage >> 0;

	if (!exprArg.GetValue()) {
		ATGetDebugger()->Break();

		throw MyError("Assertion failed: %s", exprMessage.IsValid() ? exprMessage->c_str() : exprArg.GetOriginalText());
	}
}

void ATDebuggerCmdAutotestExit(ATDebuggerCmdParser& parser) {
	ATUIExit(true);
}

void ATDebuggerCmdAutotestClearDevices(ATDebuggerCmdParser& parser) {
	parser >> 0;

	g_sim.GetDeviceManager()->RemoveAllDevices(false);
}

void ATDebuggerCmdAutotestAddDevice(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdName busArg(true);
	ATDebuggerCmdName tagArg(true);

	parser >> busArg >> tagArg >> 0;

	ATDeviceManager& devMgr = *g_sim.GetDeviceManager();
	ATParsedDevicePath busRef {};

	if (*busArg != "/") {
		busRef = devMgr.ParsePath(busArg->c_str());

		if (!busRef.mpDeviceBus)
			throw MyError("Invalid bus reference: %s.", busArg->c_str());
	}

	const ATDeviceDefinition *def = devMgr.GetDeviceDefinition(tagArg->c_str());
	if (!def)
		throw MyError("Unknown device definition: %s.", tagArg->c_str());

	ATPropertySet pset;
	IATDevice *newDev = devMgr.AddDevice(def, pset, busRef.mpDeviceBus != nullptr);

	try {
		if (busRef.mpDeviceBus) {
			busRef.mpDeviceBus->AddChildDevice(newDev);

			if (!newDev->GetParent())
				throw MyError("Unable to add device %s to bus: %s.", tagArg->c_str(), busArg->c_str());
		}
	} catch(...) {
		devMgr.RemoveDevice(newDev);
		throw;
	}

	ATConsolePrintf("Added new device: %s", devMgr.GetPathForDevice(newDev).c_str());
}

void ATDebuggerCmdAutotestRemoveDevice(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdName pathArg(true);

	parser >> pathArg >> 0;

	ATDeviceManager& devMgr = *g_sim.GetDeviceManager();
	ATParsedDevicePath deviceRef = devMgr.ParsePath(pathArg->c_str());

	if (!deviceRef.mpDevice)
		throw MyError("Invalid device reference: %s.\n", pathArg->c_str());

	devMgr.RemoveDevice(deviceRef.mpDevice);

	vdfastvector<IATDevice *> devs;
	devMgr.MarkAndSweep(nullptr, 0, devs);

	for(IATDevice *child : devs)
		devMgr.RemoveDevice(child);
}

void ATDebuggerCmdAutotestListDevices(ATDebuggerCmdParser& parser) {
	parser >> 0;

	ATDeviceManager& dm = *g_sim.GetDeviceManager();

	VDStringA s;

	const auto printEntry = [&](uint32 indent, const char *path, const wchar_t *desc) {
		s.clear();
		s.append(indent, ' ');
		s += '/';
		s += path;
		s.append(25 - std::min<size_t>(s.size(), 25), ' ');
		s.append_sprintf("%ls\n", desc);
		ATConsoleWrite(s.c_str());

	};

	const auto printDevice = [&](IATDevice *dev, uint32 indent, const auto& self) -> void {
		s.clear();
		s.append(indent, ' ');
		
		ATDeviceInfo info;
		dev->GetDeviceInfo(info);

		printEntry(indent, info.mpDef->mpTag, info.mpDef->mpName);

		IATDeviceParent *devParent = vdpoly_cast<IATDeviceParent *>(dev);
		if (devParent) {
			for(uint32 i=0; ; ++i) {
				IATDeviceBus *bus = devParent->GetDeviceBus(i);
				if (!bus)
					break;

				printEntry(indent + 2, bus->GetBusTag(), bus->GetBusName());

				vdfastvector<IATDevice *> children;
				bus->GetChildDevices(children);

				for(IATDevice *child : children) {
					self(child, indent + 4, self);
				}
			}
		}
	};

	for(IATDevice *dev : dm.GetDevices(true, true, false)) {
		printDevice(dev, 0, printDevice);
	}
}

template<bool T_State>
void ATDebuggerCmdAutotestCmdOffOn(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdName nameArg(true);
	parser >> nameArg >> 0;

	const ATUICommand *cmd = ATUIGetCommandManager().GetCommand(nameArg->c_str());
	if (!cmd)
		throw MyError("Unknown UI command: %s", nameArg->c_str());

	if (cmd->mpTestFn && !cmd->mpTestFn())
		return;

	if (!cmd->mpStateFn)
		return;

	ATUICmdState state = cmd->mpStateFn();

	const bool isOn = (state == kATUICmdState_Checked);

	if (isOn != T_State)
		cmd->mpExecuteFn();
}

void ATDebuggerCmdAutotestCmd(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdName nameArg(true);
	parser >> nameArg >> 0;

	const ATUICommand *cmd = ATUIGetCommandManager().GetCommand(nameArg->c_str());
	if (!cmd)
		throw MyError("Unknown UI command: %s", nameArg->c_str());

	if (cmd->mpTestFn && !cmd->mpTestFn())
		return;

	cmd->mpExecuteFn();
}

void ATDebuggerCmdAutotestBootImage(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdPath pathArg(true, false);
	parser >> pathArg >> 0;

	ATUIBootImage(pathArg->c_str());
}

class ATDebuggerActiveCmdCheckWaitScreen final : public vdrefcounted<IATDebuggerActiveCommand> {
public:
	ATDebuggerActiveCmdCheckWaitScreen(bool wait, uint32 waitChecksum);

	virtual bool IsBusy() const override { return true; }
	virtual const char *GetPrompt() override { return ""; }
	virtual void BeginCommand(IATDebugger *debugger) override;
	virtual void EndCommand() override;
	virtual bool ProcessSubCommand(const char *s) override;

private:
	void ProcessFrame(const VDPixmap& px);

	ATGTIARawFrameFn mRawFrameFn;
	bool mbMatched;
	IATDebugger *mpDebugger;
	bool mbWait;
	uint32 mWaitChecksum;
};

ATDebuggerActiveCmdCheckWaitScreen::ATDebuggerActiveCmdCheckWaitScreen(bool wait, uint32 waitChecksum) {
	mbMatched = false;
	mbWait = wait;
	mWaitChecksum = waitChecksum;
}

void ATDebuggerActiveCmdCheckWaitScreen::BeginCommand(IATDebugger *debugger) {
	mRawFrameFn = [this](const VDPixmap& px) { ProcessFrame(px); };
	g_sim.GetGTIA().AddRawFrameCallback(&mRawFrameFn);
	mpDebugger = debugger;
	mpDebugger->Run(kATDebugSrcMode_Same);
}

void ATDebuggerActiveCmdCheckWaitScreen::EndCommand() {
	g_sim.GetGTIA().RemoveRawFrameCallback(&mRawFrameFn);
}

bool ATDebuggerActiveCmdCheckWaitScreen::ProcessSubCommand(const char *s) {
	if (!mbMatched)
		return true;

	return false;
}

void ATDebuggerActiveCmdCheckWaitScreen::ProcessFrame(const VDPixmap& px) {
	uint32 sum1 = 0;
	uint32 sum2 = 0;

	const uint8 *p = (const uint8 *)px.data;

	for(sint32 y = 0; y < px.h; ++y) {
		uint32 sum3 = 0;
		uint32 sum4 = 0;

		for(sint32 x = 0; x < px.w; ++x) {
			sum3 += p[x];
			sum4 += sum3;
		}

		sum1 = (sum1 + sum3) % 65535;
		sum2 = (sum2 + sum4) % 65535;

		p += px.pitch;
	}

	uint32 sum = sum1 + (sum2 << 16);


	if (mbWait) {
		if (sum == mWaitChecksum)
			mbMatched = true;
	} else {
		ATConsolePrintf("Image checksum: %08X\n", sum);
		mbMatched = true;
	}

	if (mbMatched)
		mpDebugger->Stop();
}

void ATDebuggerCmdAutotestCheckScreen(ATDebuggerCmdParser& parser) {
	parser >> 0;

	ATGetDebugger()->StartActiveCommand(new ATDebuggerActiveCmdCheckWaitScreen(false, 0));
}

void ATDebuggerCmdAutotestWaitScreen(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdExprNum checksum(true);
	parser >> checksum >> 0;

	ATGetDebugger()->StartActiveCommand(new ATDebuggerActiveCmdCheckWaitScreen(true, checksum.GetValue()));
}

class ATDebuggerActiveCmdCheckWait final : public vdrefcounted<IATDebuggerActiveCommand> {
public:
	ATDebuggerActiveCmdCheckWait(vdautoptr<ATDebugExpNode> expr);

	virtual bool IsBusy() const override { return true; }
	virtual const char *GetPrompt() override { return ""; }
	virtual void BeginCommand(IATDebugger *debugger) override;
	virtual void EndCommand() override;
	virtual bool ProcessSubCommand(const char *s) override;

private:
	void OnFrameTick();

	vdautoptr<ATDebugExpNode> mpExpr;
	IATDebugger *mpDebugger;
	uint32 mEventId = 0;
	bool mbCompleted = false;
};

ATDebuggerActiveCmdCheckWait::ATDebuggerActiveCmdCheckWait(vdautoptr<ATDebugExpNode> expr) {
	mpExpr = std::move(expr);
}

void ATDebuggerActiveCmdCheckWait::BeginCommand(IATDebugger *debugger) {
	mpDebugger = debugger;

	mEventId = g_sim.GetEventManager()->AddEventCallback(kATSimEvent_FrameTick, [this] { OnFrameTick(); });
	mpDebugger->Run(kATDebugSrcMode_Same);
}

void ATDebuggerActiveCmdCheckWait::EndCommand() {
	g_sim.GetEventManager()->RemoveEventCallback(mEventId);
}

bool ATDebuggerActiveCmdCheckWait::ProcessSubCommand(const char *s) {
	return !mbCompleted;
}

void ATDebuggerActiveCmdCheckWait::OnFrameTick() {
	if (!mbCompleted && mpDebugger->Evaluate(mpExpr).second) {
		mpDebugger->Stop();
		mbCompleted = true;
	}
}

void ATDebuggerCmdAutotestWait(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdExpr cond(true);
	parser >> cond >> 0;

	ATGetDebugger()->StartActiveCommand(new ATDebuggerActiveCmdCheckWait(vdautoptr<ATDebugExpNode>(cond.DetachValue())));
}

void ATDebuggerCmdAutotestSaveImage(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdString path(true);
	parser >> path >> 0;

	if (!ATUISaveFrame(VDTextAToW(path->c_str()).c_str()))
		throw MyError("No framebuffer available.");
}

void ATAutotestAnalyzeFrame(const VDPixmap& px) {
	uint64 rsum = 0, gsum = 0, bsum = 0;
	uint64 rsumsq = 0, gsumsq = 0, bsumsq = 0;

	const uint32 w = px.w;
	const uint32 h = px.h;

	for(uint32 y = 0; y < h; ++y) {
		const uint8 *src = (const uint8 *)px.data + px.pitch * y;
		uint32 rsum2 = 0, gsum2 = 0, bsum2 = 0;
		uint32 rsumsq2 = 0, gsumsq2 = 0, bsumsq2 = 0;

		for(uint32 x = 0; x < w; ++x) {
			uint32 b = src[0];
			uint32 g = src[1];
			uint32 r = src[2];
			src += 4;

			rsum2 += r;
			gsum2 += g;
			bsum2 += b;
			rsumsq2 += r*r;
			gsumsq2 += g*g;
			bsumsq2 += b*b;
		}

		rsum += rsum2;
		gsum += gsum2;
		bsum += bsum2;
		rsumsq += rsumsq2;
		gsumsq += gsumsq2;
		bsumsq += bsumsq2;
	}

	double rsumf = (double)rsum;
	double gsumf = (double)gsum;
	double bsumf = (double)bsum;
	double rsumsqf = (double)rsumsq;
	double gsumsqf = (double)gsumsq;
	double bsumsqf = (double)bsumsq;
	double n = (double)(w * h);
	double invn = 1.0 / n;
	double invn2 = invn * invn;
	double correction = n > 1.0 ? n / (n - 1.0) : 0.0;

	ATConsolePrintf("%-*s | %4u x %-4u | R %5.3f +/- %5.3f | G %5.3f +/- %5.3f | B %5.3f +/- %5.3f\n"
		, g_ATAutotestDescColumnSize
		, g_ATAutotestDesc.c_str()
		, w, h
		, rsumf * invn / 255.0, sqrt(std::max(0.0, correction * (rsumsqf * invn - rsum*rsum*invn2))) / 255.0
		, gsumf * invn / 255.0, sqrt(std::max(0.0, correction * (gsumsqf * invn - gsum*gsum*invn2))) / 255.0
		, bsumf * invn / 255.0, sqrt(std::max(0.0, correction * (bsumsqf * invn - bsum*bsum*invn2))) / 255.0
	);
}

class ATDebuggerActiveCmdAutotestAnalyzeRenderedImage final : public vdrefcounted<IATDebuggerActiveCommand> {
public:
	bool IsBusy() const override { return true; }
	const char *GetPrompt() override { return ""; }

	void BeginCommand(IATDebugger *debugger) override {
		IATDisplayPane *pane = ATGetUIPaneAs<IATDisplayPane>(kATUIPaneId_Display);

		if (!pane)
			throw MyError("No display pane to capture.");

		pane->RequestRenderedFrame(
			[self = vdrefptr(this)](const VDPixmap *px) {
				if (!self->mbCompleted)
					self->OnCapturedFrame(px);
			}
		);
	}

	void EndCommand() {
		mbCompleted = true;
	}

	bool ProcessSubCommand(const char *s) { return !mbCompleted; }

private:
	void OnCapturedFrame(const VDPixmap *px) {
		mbCompleted = true;

		if (!px) {
			ATConsoleWrite("Unable to capture rendered frame.\n");
			return;
		}

		VDPixmapBuffer tmp;
		if (px->format != nsVDPixmap::kPixFormat_XRGB8888) {
			tmp.init(px->w, px->h, nsVDPixmap::kPixFormat_XRGB8888);
			VDPixmapBlt(tmp, *px);

			px = &tmp;
		}

		ATAutotestAnalyzeFrame(*px);
	}

	bool mbCompleted = false;
};

void ATDebuggerCmdAutotestAnalyzeRenderedImage(ATDebuggerCmdParser& parser) {
	parser >> 0;

	ATGetDebugger()->StartActiveCommand(new ATDebuggerActiveCmdAutotestAnalyzeRenderedImage);
}

void ATDebuggerCmdAutotestAnalyzeCopiedImage(ATDebuggerCmdParser& parser) {
	parser >> 0;

	IATDisplayPane *pane = ATGetUIPaneAs<IATDisplayPane>(kATUIPaneId_Display);

	if (!pane)
		throw MyError("No display pane to capture.");

	VDPixmapBuffer buf;
	if (!pane->CopyFrameImage(false, buf))
		throw MyError("No image available to copy.");

	ATAutotestAnalyzeFrame(buf);
}

class ATDebuggerActiveCmdAutotestAnalyzeRecordedImage final : public vdrefcounted<IATDebuggerActiveCommand>, IATGTIAVideoTap {
public:
	bool IsBusy() const override { return true; }
	const char *GetPrompt() override { return ""; }

	void BeginCommand(IATDebugger *debugger) override {
		g_sim.GetGTIA().AddVideoTap(this);
		g_sim.SetBreakOnFrameEnd(false);
		g_sim.Resume();
	}

	void EndCommand() {
		g_sim.GetGTIA().RemoveVideoTap(this);
		mbCompleted = true;
	}

	bool ProcessSubCommand(const char *s) { return !mbCompleted; }

private:
	void WriteFrame(const VDPixmap& px, uint64 timestampStart, uint64 timestampEnd, float par) {
		if (mFramesRecorded++ == 0)
			return;

		g_sim.SetBreakOnFrameEnd(true);
		g_sim.GetGTIA().RemoveVideoTap(this);

		mbCompleted = true;

		const VDPixmap *px2 = &px;
		VDPixmapBuffer tmp;
		if (px.format != nsVDPixmap::kPixFormat_XRGB8888) {
			tmp.init(px.w, px.h, nsVDPixmap::kPixFormat_XRGB8888);
			VDPixmapBlt(tmp, px);

			px2 = &tmp;
		}

		ATAutotestAnalyzeFrame(*px2);
	}

	bool mbCompleted = false;
	int mFramesRecorded = 0;
};

void ATDebuggerCmdAutotestAnalyzeRecordedImage(ATDebuggerCmdParser& parser) {
	parser >> 0;

	ATGetDebugger()->StartActiveCommand(new ATDebuggerActiveCmdAutotestAnalyzeRecordedImage);
}

class ATDebuggerActiveCmdAutotestCreateMacro final : public vdrefcounted<IATDebuggerActiveCommand> {
public:
	ATDebuggerActiveCmdAutotestCreateMacro(const VDStringA& name) : mName(name) {}

	bool IsBusy() const override { return false; }
	const char *GetPrompt() override { return "Macro"; }

	void BeginCommand(IATDebugger *debugger) override {
		mCommands.clear();
		mCommands.push_back(0);
	}

	void EndCommand() {
		g_ATAutotestMacros[mName] = mCommands;
	}

	bool ProcessSubCommand(const char *s) {
		if (!*s)
			return false;

		ATConsolePrintf("%s\n", s);

		// note that we are intentionally capturing the null here
		mCommands.append(s, s+strlen(s)+1);
		return true;
	}

private:
	VDStringA mName;
	VDStringA mCommands;
};

void ATDebuggerCmdAutotestCreateMacro(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdName name(true);
	parser >> name >> 0;

	ATGetDebugger()->StartActiveCommand(new ATDebuggerActiveCmdAutotestCreateMacro(*name));
}

void ATDebuggerCmdAutotestExecMacro(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdName name(true);
	parser >> name >> 0;

	auto it = g_ATAutotestMacros.find_as(*name);
	if (it == g_ATAutotestMacros.end())
		throw MyError("Unknown autotest macro: %s.", name->c_str());

	IATDebugger *d = ATGetDebugger();
	
	const char *s = it->second.c_str();
	const char *t = s + it->second.size() - 1;

	while(s + 1 != t) {
		do {
			--t;
		} while(t[-1]);

		d->QueueCommandFront(t, false);
	}
}

void ATDebuggerCmdAutotestSetDisplayAPI(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdSwitchNumArg d3d9("d3d9", 0, 1);
	ATDebuggerCmdSwitchNumArg d3d11("d3d11", 0, 1);
	parser >> d3d9 >> d3d11 >> 0;

	ATOptions prevOpts(g_ATOptions);
	g_ATOptions.mbDisplayD3D9 = d3d9.GetValue() != 0;
	g_ATOptions.mbDisplay3D = d3d11.GetValue() != 0;

	ATOptionsRunUpdateCallbacks(&prevOpts);
}

void ATDebuggerCmdAutotestSetDesc(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdString str(true);
	parser >> str >> 0;

	g_ATAutotestDesc += *str;
	g_ATAutotestDescBreaks.clear();
}

void ATDebuggerCmdAutotestAppendDesc(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdString str(true);
	parser >> str >> 0;

	g_ATAutotestDescBreaks.push_back(g_ATAutotestDesc.size());
	g_ATAutotestDesc += *str;
}

void ATDebuggerCmdAutotestPopDesc(ATDebuggerCmdParser& parser) {
	parser >> 0;

	if (!g_ATAutotestDescBreaks.empty()) {
		g_ATAutotestDesc.resize(g_ATAutotestDescBreaks.back());
		g_ATAutotestDescBreaks.pop_back();
	}
}

void ATDebuggerCmdAutotestSetDescColumnSize(ATDebuggerCmdParser& parser) {
	ATDebuggerCmdExprNum width(true, false, 0, 100);
	parser >> width >> 0;
	g_ATAutotestDescColumnSize = width.GetValue();
}

void ATDebuggerInitAutotestCommands() {
	static constexpr ATDebuggerCmdDef kCommands[]={
		{ ".autotest_assert",				ATDebuggerCmdAutotestAssert },
		{ ".autotest_exit",					ATDebuggerCmdAutotestExit },
		{ ".autotest_cleardevices",			ATDebuggerCmdAutotestClearDevices },
		{ ".autotest_listdevices",			ATDebuggerCmdAutotestListDevices },
		{ ".autotest_adddevice",			ATDebuggerCmdAutotestAddDevice },
		{ ".autotest_removedevice",			ATDebuggerCmdAutotestRemoveDevice },
		{ ".autotest_cmd",					ATDebuggerCmdAutotestCmd },
		{ ".autotest_cmdoff",				ATDebuggerCmdAutotestCmdOffOn<false> },
		{ ".autotest_cmdon",				ATDebuggerCmdAutotestCmdOffOn<true> },
		{ ".autotest_bootimage",			ATDebuggerCmdAutotestBootImage },
		{ ".autotest_checkscreen",			ATDebuggerCmdAutotestCheckScreen },
		{ ".autotest_waitscreen",			ATDebuggerCmdAutotestWaitScreen },
		{ ".autotest_wait",					ATDebuggerCmdAutotestWait },
		{ ".autotest_saveimage",			ATDebuggerCmdAutotestSaveImage },
		{ ".autotest_analyzerenderedimage",	ATDebuggerCmdAutotestAnalyzeRenderedImage },
		{ ".autotest_analyzecopiedimage",	ATDebuggerCmdAutotestAnalyzeCopiedImage },
		{ ".autotest_analyzerecordedimage",	ATDebuggerCmdAutotestAnalyzeRecordedImage },
		{ ".autotest_createmacro",			ATDebuggerCmdAutotestCreateMacro },
		{ ".autotest_execmacro",			ATDebuggerCmdAutotestExecMacro },
		{ ".autotest_setdisplayapi",		ATDebuggerCmdAutotestSetDisplayAPI },
		{ ".autotest_setdesc",				ATDebuggerCmdAutotestSetDesc },
		{ ".autotest_appenddesc",			ATDebuggerCmdAutotestAppendDesc },
		{ ".autotest_popdesc",				ATDebuggerCmdAutotestPopDesc },
		{ ".autotest_setdesccolumnsize",	ATDebuggerCmdAutotestSetDescColumnSize },
	};

	ATGetDebugger()->DefineCommands(kCommands, vdcountof(kCommands));
}
