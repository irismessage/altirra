#ifndef f_AT_DEBUGGERLOG_H
#define f_AT_DEBUGGERLOG_H

class ATDebuggerLogChannel;

void ATRegisterLogChannel(ATDebuggerLogChannel *channel, bool enabled, bool tagged, const char *shortName, const char *longDesc);

enum ATTagFlags {
	kATTagFlags_None = 0x00,
	kATTagFlags_Timestamp = 0x01,
	kATTagFlags_CassettePos = 0x02,
	kATTagFlags_All = 0xFF,

};

class ATDebuggerLogChannel {
	struct Dummy {
		void A() {}
	};
	typedef void (Dummy::*DummyMemberFn)();

public:
	ATDebuggerLogChannel(bool enabled, bool tagged, const char *shortName, const char *longDesc) {
		ATRegisterLogChannel(this, enabled, tagged, shortName, longDesc);
	}

	bool IsEnabled() const { return mbEnabled; }
	void SetEnabled(bool enabled) { mbEnabled = enabled; }

	uint32 GetTagFlags() const { return mTagFlags; }
	void SetTagFlags(uint32 flags) { mTagFlags = flags; }

	const char *GetName() const { return mpShortName; }
	const char *GetDesc() const { return mpLongDesc; }

	operator DummyMemberFn() const { return mbEnabled ? &Dummy::A : NULL; }
	void operator<<=(const char *message);
	void operator()(const char *message, ...);

	static ATDebuggerLogChannel *GetFirstChannel();
	static ATDebuggerLogChannel *GetNextChannel(ATDebuggerLogChannel *);

protected:
	friend void ATRegisterLogChannel(ATDebuggerLogChannel *channel, bool enabled, bool tagged, const char *shortName, const char *longDesc);
	void WriteTag();

	ATDebuggerLogChannel *mpNext;
	bool mbEnabled;
	uint32 mTagFlags;
	const char *mpShortName;
	const char *mpLongDesc;
};

#endif
