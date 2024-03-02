#ifndef f_AT_MYIDE_H
#define f_AT_MYIDE_H

class ATMemoryManager;
class ATMemoryLayer;
class ATIDEEmulator;

class ATMyIDEEmulator {
	ATMyIDEEmulator(const ATMyIDEEmulator&);
	ATMyIDEEmulator& operator=(const ATMyIDEEmulator&);
public:
	ATMyIDEEmulator();
	~ATMyIDEEmulator();

	void Init(ATMemoryManager *memman, ATIDEEmulator *ide, bool used5xx);
	void Shutdown();

protected:
	static sint32 OnDebugReadByte(void *thisptr, uint32 addr);
	static sint32 OnReadByte(void *thisptr, uint32 addr);
	static bool OnWriteByte(void *thisptr, uint32 addr, uint8 value);

	ATMemoryManager *mpMemMan;
	ATMemoryLayer *mpMemLayerIDE;
	ATIDEEmulator *mpIDE;
};

#endif
