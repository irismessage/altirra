#include "stdafx.h"
#include <vd2/system/strutil.h>
#include "console.h"
#include "debuggerlog.h"
#include "simulator.h"
#include "cassette.h"

extern ATSimulator g_sim;

ATDebuggerLogChannel *g_ATDebuggerLogChannels;
VDStringA g_ATDebuggerLogBuffer;

void ATRegisterLogChannel(ATDebuggerLogChannel *channel, bool enabled, bool tagged, const char *shortName, const char *longDesc) {
	channel->mpNext = g_ATDebuggerLogChannels;
	channel->mbEnabled = enabled;
	channel->mTagFlags = tagged ? kATTagFlags_Timestamp : 0;
	channel->mpShortName = shortName;
	channel->mpLongDesc = longDesc;

	g_ATDebuggerLogChannels = channel;
}

void ATDebuggerLogChannel::operator<<=(const char *message) {
	if (!mbEnabled)
		return;

	WriteTag();

	g_ATDebuggerLogBuffer.append(message);
	ATConsoleWrite(g_ATDebuggerLogBuffer.c_str());
}

void ATDebuggerLogChannel::operator()(const char *format, ...) {
	if (!mbEnabled)
		return;

	WriteTag();

	va_list val;

	va_start(val, format);
	g_ATDebuggerLogBuffer.append_vsprintf(format, val);
	va_end(val);

	ATConsoleWrite(g_ATDebuggerLogBuffer.c_str());
}

void ATDebuggerLogChannel::WriteTag() {
	g_ATDebuggerLogBuffer.clear();

	if (mTagFlags & kATTagFlags_Timestamp) {
		ATAnticEmulator& antic = g_sim.GetAntic();
		g_ATDebuggerLogBuffer.append_sprintf("(%3d:%3d,%3d) ", antic.GetFrameCounter(), antic.GetBeamY(), antic.GetBeamX());
	}

	if (mTagFlags & kATTagFlags_CassettePos) {
		ATCassetteEmulator& cas = g_sim.GetCassette();

		if (!cas.IsLoaded())
			g_ATDebuggerLogBuffer += "(---:--.---) ";
		else {
			float t = cas.GetPosition();
			int mins = (int)(t / 60.0f);
			g_ATDebuggerLogBuffer.append_sprintf("(%3d:%06.3f) ", mins, t - (float)mins * 60.0f);
		}
	}

	g_ATDebuggerLogBuffer += mpShortName;
	g_ATDebuggerLogBuffer += ": ";
}

ATDebuggerLogChannel *ATDebuggerLogChannel::GetFirstChannel() {
	return g_ATDebuggerLogChannels;
}

ATDebuggerLogChannel *ATDebuggerLogChannel::GetNextChannel(ATDebuggerLogChannel *p) {
	return p->mpNext;
}
