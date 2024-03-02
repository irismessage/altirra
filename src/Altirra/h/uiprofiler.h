#ifndef f_AT_UIPROFILER_H
#define f_AT_UIPROFILER_H

class ATUIManager;

enum ATUIProfileRegion {
	kATUIProfileRegion_Idle,
	kATUIProfileRegion_Simulation,
	kATUIProfileRegion_NativeEvents,
	kATUIProfileRegion_DisplayTick,
	kATUIProfileRegionCount
};

void ATUIProfileCreateWindow(ATUIManager *m);
void ATUIProfileDestroyWindow();

void ATUIProfileBeginFrame();
void ATUIProfileBeginRegion(ATUIProfileRegion region);
void ATUIProfileEndRegion();

#endif
