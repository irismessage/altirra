//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008-2010 Avery Lee
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#ifndef f_AT_VBXE_H
#define f_AT_VBXE_H

#include <vd2/system/vdstl.h>

class IATVBXEEmulatorConnections {
public:
	virtual void VBXERequestMemoryMapUpdate() = 0;
	virtual void VBXEAssertIRQ() = 0;
	virtual void VBXENegateIRQ() = 0;
};

class ATVBXEEmulator {
	ATVBXEEmulator(const ATVBXEEmulator&);
	ATVBXEEmulator& operator=(const ATVBXEEmulator&);
public:
	ATVBXEEmulator();
	~ATVBXEEmulator();

	// VBXE requires 512K of memory.
	void Init(uint8 *memory, IATVBXEEmulatorConnections *conn);

	void ColdReset();
	void WarmReset();

	void SetAnalysisMode(bool analysisMode);
	void SetDefaultPalette(const uint32 pal[256]);

	bool IsBlitLoggingEnabled() const { return mbBlitLogging; }
	void SetBlitLoggingEnabled(bool enable);
	void DumpStatus();
	void DumpBlitList();
	bool DumpBlitListEntry(uint32 addr);
	void DumpXDL();

	// ANTIC/RAM interface
	uint8 ReadControl(uint8 addrLo);
	void WriteControl(uint8 addrLo, uint8 value);

	void UpdateMemoryMaps(const uint8 *cpuRead[256], uint8 *cpuWrite[256], const uint8 *anticRead[256]);

	// GTIA interface
	void BeginFrame();
	void BeginScanline(uint32 *dst, const uint8 *mergeBuffer, const uint8 *anticBuffer, bool hires);
	void RenderScanline(int x2);
	void EndScanline();

	void AddRegisterChange(uint8 pos, uint8 addr, uint8 value);

protected:
	struct RegisterChange {
		uint8 mPos;
		uint8 mReg;
		uint8 mValue;
		uint8 mPad;
	};

	void UpdateRegisters(const RegisterChange *changes, int count);

	void RenderAttrPixels(int x1, int x2);
	void RenderAttrDefaultPixels(int x1, int x2);

	void RenderLores(int x1, int x2);
	void RenderMode8(int x1, int x2);
	void RenderMode9(int x1, int x2);
	void RenderMode10(int x1, int x2);
	void RenderMode11(int x1, int x2);
	void RenderOverlay(int x1, int x2);
	void RenderOverlayLR(uint8 *dst, int x1, int w);
	void RenderOverlaySR(uint8 *dst, int x1, int w);
	void RenderOverlayHR(uint8 *dst, int x1, int w);
	void RenderOverlay80Text(uint8 *dst, int rx1, int x1, int w);

	void RunBlitter();
	void LoadBlitter();

	void InitPriorityTables();

	uint8 *mpMemory;
	IATVBXEEmulatorConnections *mpConn;

	uint8	mMemAcControl;
	uint8	mMemAcBankA;
	uint8	mMemAcBankB;

	uint32 mXdlBaseAddr;
	uint32 mXdlAddr;
	bool mbXdlActive;
	bool mbXdlEnabled;
	uint32 mXdlRepeatCounter;

	enum OvMode {
		kOvMode_Disabled,
		kOvMode_LR,
		kOvMode_SR,
		kOvMode_HR,
		kOvMode_80Text
	};

	enum OvWidth {
		kOvWidth_Narrow,
		kOvWidth_Normal,
		kOvWidth_Wide,
	};

	OvMode mOvMode;
	OvWidth mOvWidth;
	bool	mbOvTrans;
	bool	mbOvTrans15;
	uint8	mOvHscroll;
	uint8	mOvVscroll;
	uint8	mOvMainPriority;		// INVERTED priority from XDL
	uint8	mOvPriority[4];			// INVERTED priority chosen by att map

	uint32 mOvAddr;
	uint32 mOvStep;
	uint32 mOvTextRow;
	uint32 mChAddr;

	uint8 mPfPaletteIndex;
	uint8 mOvPaletteIndex;

	bool mbExtendedColor;
	bool mbAttrMapEnabled;
	uint32 mAttrAddr;
	uint32 mAttrStep;
	uint32 mAttrWidth;
	uint32 mAttrHeight;
	uint32 mAttrHscroll;
	uint32 mAttrVscroll;
	uint32 mAttrRow;

	// RAMDAC
	uint8 mPsel;
	uint8 mCsel;

	// IRQ
	bool mbIRQEnabled;
	bool mbIRQRequest;

	uint32	mDMACycles;

	// blitter
	bool mbBlitLogging;
	bool mbBlitterEnabled;
	bool mbBlitterActive;
	bool mbBlitterListActive;
	bool mbBlitterContinue;
	uint8 mBlitterMode;
	sint32 mBlitCyclesLeft;
	sint32 mBlitCyclesPerRow;
	uint32 mBlitListAddr;
	uint32 mBlitListFetchAddr;
	uint32 mBlitSrcAddr;
	sint32 mBlitSrcStepX;
	sint32 mBlitSrcStepY;
	uint32 mBlitDstAddr;
	sint32 mBlitDstStepX;
	sint32 mBlitDstStepY;
	uint32 mBlitWidth;
	uint32 mBlitHeight;
	uint8 mBlitAndMask;
	uint8 mBlitXorMask;
	uint8 mBlitCollisionMask;
	uint8 mBlitPatternMode;
	uint8 mBlitCollisionCode;
	uint8 mBlitZoomX;
	uint8 mBlitZoomY;
	uint8 mBlitZoomCounterY;

	// scan out
	const uint32 *mpPfPalette;
	const uint32 *mpOvPalette;

	const uint8 *mpMergeBuffer;
	const uint8 *mpAnticBuffer;

	uint32 *mpDst;
	int mX;
	int mRCIndex;
	int mRCCount;

	bool mbHiresMode;
	uint8 mPRIOR;

	const uint8 (*mpPriTable)[2];
	const uint8 *mpColorTable;

	typedef vdfastvector<RegisterChange> RegisterChanges;
	RegisterChanges mRegisterChanges;

	uint8	mColorTable[24];
	uint8	mPriorityTables[32][256][2];

	uint32	mPalette[4][256];
	uint32	mDefaultPalette[256];

	uint8	mGTIADecode[912];
	uint8	mOverlayDecode[912];
	uint8	mOvPriDecode[456];
	uint8	mOvTextTrans[912];

	struct AttrPixel {
		uint8 mPFK;
		uint8 mPF0;
		uint8 mPF1;
		uint8 mPF2;
		uint8 mCtrl;
		uint8 mHiresFlag;
		uint8 mPriority;
		uint8 _mUnused;
	};

	AttrPixel	mAttrPixels[456];

	static const OvMode kOvModeTable[3][4];
};

#endif
