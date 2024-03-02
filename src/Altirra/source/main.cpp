//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2009 Avery Lee
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

#include "stdafx.h"
#include <windows.h>
#include <mmsystem.h>
#include <dbghelp.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <commctrl.h>
#include <vd2/system/bitmath.h>
#include <vd2/system/cpuaccel.h>
#include <vd2/system/math.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/w32assist.h>
#include <vd2/system/error.h>
#include <vd2/system/registry.h>
#include <vd2/system/cmdline.h>
#include <vd2/system/thunk.h>
#include <vd2/system/binary.h>
#include <vd2/system/strutil.h>
#include <vd2/Riza/display.h>
#include <vd2/Riza/direct3d.h>
#include <vd2/Dita/services.h>
#include "console.h"
#include "simulator.h"
#include "cassette.h"
#include "ui.h"
#include "uiframe.h"
#include "debugger.h"
#include "harddisk.h"
#include "Dialog.h"
#include "savestate.h"
#include "resource.h"
#include "oshelper.h"
#include "autotest.h"
#include "audiowriter.h"
#include "inputmanager.h"
#include "inputcontroller.h"
#include "joystick.h"
#include "cartridge.h"
#include "version.h"

#ifdef _DEBUG
	#define AT_VERSION_DEBUG "-debug"
#else
	#define AT_VERSION_DEBUG ""
#endif

#pragma comment(lib, "comctl32")
#pragma comment(lib, "shlwapi")

///////////////////////////////////////////////////////////////////////////////

void ATUIShowDialogInputMappings(VDZHWND parent, ATInputManager& iman, IATJoystickManager *ijoy);

///////////////////////////////////////////////////////////////////////////////

void ATInitDebugger();
void ATInitProfilerUI();
void ATInitUIPanes();
void ATShutdownUIPanes();
void ATShowChangeLog(VDGUIHandle hParent);

void SaveInputMaps();

void ProcessKey(char c, bool repeat, bool allowQueue);

///////////////////////////////////////////////////////////////////////////////

class ATDisplayBasePane : public ATUIPane {
public:
	ATDisplayBasePane();

	virtual void OnSize() = 0;
	virtual void UpdateTextDisplay(bool enabled) = 0;
	virtual void UpdateTextModeFont() = 0;
};

ATDisplayBasePane::ATDisplayBasePane()
	: ATUIPane(kATUIPaneId_Display, "Display")
{
}

///////////////////////////////////////////////////////////////////////////////
HINSTANCE g_hInst;
HWND g_hwnd;
ATContainerWindow *g_pMainWindow;
HWND g_hwndDisplay;
HACCEL g_hAccel;
IVDVideoDisplay *g_pDisplay;
HMENU g_hMenu;

ATSimulator g_sim;

bool g_fullscreen = false;
bool g_mouseCaptured = false;
bool g_pauseInactive = true;
bool g_winActive = true;
bool g_showFps = false;
IVDVideoDisplay::FilterMode g_dispFilterMode = IVDVideoDisplay::kFilterBilinear;
int g_enhancedText = 0;
LOGFONTW g_enhancedTextFont;

ATSaveStateWriter::Storage g_quickSave;

vdautoptr<ATAudioWriter> g_pAudioWriter;

enum ATDisplayStretchMode {
	kATDisplayStretchMode_Unconstrained,
	kATDisplayStretchMode_PreserveAspectRatio,
	kATDisplayStretchMode_SquarePixels,
	kATDisplayStretchMode_Integral,
	kATDisplayStretchModeCount
};

ATDisplayStretchMode g_displayStretchMode = kATDisplayStretchMode_PreserveAspectRatio;

#define MYWM_PRESYSKEYDOWN (WM_APP + 0x120)
#define MYWM_PRESYSKEYUP (WM_APP + 0x121)
#define MYWM_PREKEYDOWN (WM_APP + 0x122)
#define MYWM_PREKEYUP (WM_APP + 0x123)

///////////////////////////////////////////////////////////////////////////////

class ATInputPortMenu {
public:
	ATInputPortMenu();
	~ATInputPortMenu();

	void Init(int portIdx, uint32 baseId, HMENU subMenu);
	void Shutdown();

	void Reload();
	void UpdateMenu();
	void HandleCommand(uint32 id);

protected:
	int mPortIdx;
	uint32 mBaseId;
	HMENU mhmenu;

	typedef vdfastvector<ATInputMap *> InputMaps;
	InputMaps mInputMaps;

	struct InputMapSort;
};

struct ATInputPortMenu::InputMapSort {
	bool operator()(const ATInputMap *x, const ATInputMap *y) const {
		return vdwcsicmp(x->GetName(), y->GetName()) < 0;
	}
};

ATInputPortMenu::ATInputPortMenu()
	: mPortIdx(0)
	, mBaseId(0)
	, mhmenu(NULL)
{
}

ATInputPortMenu::~ATInputPortMenu() {
	Shutdown();
}

void ATInputPortMenu::Init(int portIdx, uint32 baseId, HMENU subMenu) {
	mPortIdx = portIdx;
	mBaseId = baseId;
	mhmenu = subMenu;
}

void ATInputPortMenu::Shutdown() {
	while(!mInputMaps.empty()) {
		mInputMaps.back()->Release();
		mInputMaps.pop_back();
	}

	mhmenu = NULL;
}

void ATInputPortMenu::Reload() {
	if (!mhmenu)
		return;

	// clear existing items
	int count = ::GetMenuItemCount(mhmenu);
	for(int i=count; i>=1; --i)
		::DeleteMenu(mhmenu, i, MF_BYPOSITION);

	while(!mInputMaps.empty()) {
		mInputMaps.back()->Release();
		mInputMaps.pop_back();
	}

	// find input maps that touch a port
	ATInputManager& im = *g_sim.GetInputManager();

	uint32 mapCount = im.GetInputMapCount();
	for(uint32 i=0; i<mapCount; ++i) {
		vdrefptr<ATInputMap> imap;
		if (im.GetInputMapByIndex(i, ~imap)) {
			if (imap->UsesPhysicalPort(mPortIdx)) {
				mInputMaps.push_back(imap);
				imap.release();
			}
		}
	}

	// sort input maps
	std::sort(mInputMaps.begin(), mInputMaps.end(), InputMapSort());

	// populate menu
	const uint32 entryCount = mInputMaps.size();
	for(uint32 i=0; i<entryCount; ++i) {
		ATInputMap *imap = mInputMaps[i];

		VDAppendMenuW32(mhmenu, MF_STRING, mBaseId + i + 1, imap->GetName());
	}
}

void ATInputPortMenu::UpdateMenu() {
	ATInputManager& im = *g_sim.GetInputManager();

	uint32 n = mInputMaps.size();
	bool anyActive = false;

	for(uint32 i=0; i<n; ++i) {
		bool active = false;

		if (im.IsInputMapEnabled(mInputMaps[i])) {
			active = true;
			anyActive = true;
		}

		VDCheckRadioMenuItemByPositionW32(mhmenu, i+1, active);
	}

	VDCheckRadioMenuItemByPositionW32(mhmenu, 0, !anyActive);
}

void ATInputPortMenu::HandleCommand(uint32 id) {
	ATInputManager& im = *g_sim.GetInputManager();

	uint32 idx = id - mBaseId;
	uint32 i = 1;

	for(InputMaps::const_iterator it(mInputMaps.begin()), itEnd(mInputMaps.end()); it != itEnd; ++it, ++i) {
		ATInputMap *imap = *it;

		im.ActivateInputMap(imap, id == (mBaseId + i));
	}
}

ATInputPortMenu g_portMenus[4];

HMENU ATFindParentMenuW32(HMENU hmenuStart, UINT id) {
	int n = GetMenuItemCount(hmenuStart);
	for(int i=0; i<n; ++i) {
		if (GetMenuItemID(hmenuStart, i) == id)
			return hmenuStart;

		HMENU hSubMenu = GetSubMenu(hmenuStart, i);
		if (hSubMenu) {
			HMENU result = ATFindParentMenuW32(hSubMenu, id);
			if (result)
				return result;
		}
	}

	return NULL;
}

void ATInitPortMenus(HMENU hmenu) {
	HMENU hSubMenu;

	hSubMenu = ATFindParentMenuW32(hmenu, ID_INPUT_PORT1_NONE);
	g_portMenus[0].Init(0, ID_INPUT_PORT1_NONE, hSubMenu);

	hSubMenu = ATFindParentMenuW32(hmenu, ID_INPUT_PORT2_NONE);
	g_portMenus[1].Init(1, ID_INPUT_PORT2_NONE, hSubMenu);

	hSubMenu = ATFindParentMenuW32(hmenu, ID_INPUT_PORT3_NONE);
	g_portMenus[2].Init(2, ID_INPUT_PORT3_NONE, hSubMenu);

	hSubMenu = ATFindParentMenuW32(hmenu, ID_INPUT_PORT4_NONE);
	g_portMenus[3].Init(3, ID_INPUT_PORT4_NONE, hSubMenu);
}

void ATUpdatePortMenus() {
	for(int i=0; i<4; ++i)
		g_portMenus[i].UpdateMenu();
}

void ATShutdownPortMenus() {
	for(int i=0; i<4; ++i)
		g_portMenus[i].Shutdown();
}

void ATReloadPortMenus() {
	for(int i=0; i<4; ++i)
		g_portMenus[i].Reload();
}

///////////////////////////////////////////////////////////////////////////////

class ATInputConsoleCallback : public IATInputConsoleCallback {
public:
	virtual void SetConsoleTrigger(uint32 id, bool state);
};

void ATInputConsoleCallback::SetConsoleTrigger(uint32 id, bool state) {
	switch(id) {
		case kATInputTrigger_Start:
			g_sim.GetGTIA().SetConsoleSwitch(0x01, state);
			break;
		case kATInputTrigger_Select:
			g_sim.GetGTIA().SetConsoleSwitch(0x02, state);
			break;
		case kATInputTrigger_Option:
			g_sim.GetGTIA().SetConsoleSwitch(0x04, state);
			break;
		case kATInputTrigger_ColdReset:
			if (state)
				g_sim.ColdReset();
			break;
		case kATInputTrigger_WarmReset:
			if (state)
				g_sim.ColdReset();
			break;
		case kATInputTrigger_Turbo:
			g_sim.SetTurboModeEnabled(state);
			break;
		case kATInputTrigger_KeySpace:
			if (state)
				g_sim.GetPokey().PushKey(0x21, false);
			break;
	}
}

ATInputConsoleCallback g_inputConsoleCallback;

///////////////////////////////////////////////////////////////////////////////

int ATExceptionFilter(DWORD code, EXCEPTION_POINTERS *exp) {
	if (IsDebuggerPresent())
		return EXCEPTION_CONTINUE_SEARCH;

	char buf[1024];

	HMODULE hmodDbgHelp = LoadLibrary("dbghelp");
	if (hmodDbgHelp) {
		typedef BOOL (WINAPI *tpMiniDumpWriteDump)(
			  HANDLE hProcess,
			  DWORD ProcessId,
			  HANDLE hFile,
			  MINIDUMP_TYPE DumpType,
			  PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
			  PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
			  PMINIDUMP_CALLBACK_INFORMATION CallbackParam
			);

		tpMiniDumpWriteDump pMiniDumpWriteDump = (tpMiniDumpWriteDump)GetProcAddress(hmodDbgHelp, "MiniDumpWriteDump");

		if (pMiniDumpWriteDump) {
			MINIDUMP_EXCEPTION_INFORMATION exInfo;

			exInfo.ThreadId = GetCurrentThreadId();
			exInfo.ExceptionPointers = exp;
			exInfo.ClientPointers = TRUE;

			static const char kFilename[] = "AltirraCrash.mdmp";
			if (GetModuleFileName(NULL, buf, sizeof buf / sizeof buf[0])) {
				size_t len = strlen(buf);

				while(len > 0) {
					char c = buf[len - 1];

					if (c == ':' || c == '\\' || c == '/')
						break;

					--len;
				}

				if (len < MAX_PATH - sizeof(kFilename)) {
					strcpy(buf + len, kFilename);

					HANDLE hFile = CreateFile(buf, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

					if (hFile != INVALID_HANDLE_VALUE) {
						pMiniDumpWriteDump(
								GetCurrentProcess(),
								GetCurrentProcessId(),
								hFile,
								MiniDumpNormal,
								&exInfo,
								NULL,
								NULL);

						CloseHandle(hFile);
					}
				}
			}
		}

		FreeLibrary(hmodDbgHelp);
	}

	if (g_hwnd) {
		EnableWindow(g_hwnd, FALSE);
		SetWindowLongPtr(g_hwnd, GWL_WNDPROC, (LONG_PTR)(IsWindowUnicode(g_hwnd) ? DefWindowProcW : DefWindowProcA));
	}

	wsprintf(buf, "A fatal error has occurred in the emulator. A minidump file called AltirraCrash.mdmp has been written for diagnostic purposes.\n"
		"\n"
		"Exception code: %08x  PC: %08x", code, exp->ContextRecord->Eip);
	MessageBox(g_hwnd, buf, "Altirra Program Failure", MB_OK | MB_ICONERROR);

	TerminateProcess(GetCurrentProcess(), code);
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

INT_PTR CALLBACK CPUOptionsDlgProc(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_INITDIALOG:
			{
				ATCPUEmulator& cpuem = g_sim.GetCPU();

				CheckDlgButton(hdlg, IDC_ENABLE_HISTORY, cpuem.IsHistoryEnabled());
				CheckDlgButton(hdlg, IDC_ENABLE_PATHS, cpuem.IsPathfindingEnabled());
				CheckDlgButton(hdlg, IDC_ENABLE_ILLEGALS, cpuem.AreIllegalInsnsEnabled());
				CheckDlgButton(hdlg, IDC_STOP_ON_BRK, cpuem.GetStopOnBRK());

				switch(cpuem.GetCPUMode()) {
					case kATCPUMode_6502:
						CheckDlgButton(hdlg, IDC_CPUMODEL_6502C, BST_CHECKED);
						break;
					case kATCPUMode_65C02:
						CheckDlgButton(hdlg, IDC_CPUMODEL_65C02, BST_CHECKED);
						break;
					case kATCPUMode_65C816:
						CheckDlgButton(hdlg, IDC_CPUMODEL_65C816, BST_CHECKED);
						break;
				}
			}
			return TRUE;

		case WM_COMMAND:
			switch(LOWORD(wParam)) {
			case IDCANCEL:
				EndDialog(hdlg, FALSE);
				return TRUE;

			case IDOK:
				{
					ATCPUEmulator& cpuem = g_sim.GetCPU();

					cpuem.SetHistoryEnabled(!!IsDlgButtonChecked(hdlg, IDC_ENABLE_HISTORY));
					cpuem.SetPathfindingEnabled(!!IsDlgButtonChecked(hdlg, IDC_ENABLE_PATHS));
					cpuem.SetIllegalInsnsEnabled(!!IsDlgButtonChecked(hdlg, IDC_ENABLE_ILLEGALS));
					cpuem.SetStopOnBRK(!!IsDlgButtonChecked(hdlg, IDC_STOP_ON_BRK));

					ATCPUMode cpuMode = kATCPUMode_6502;
					if (IsDlgButtonChecked(hdlg, IDC_CPUMODEL_65C816))
						cpuMode = kATCPUMode_65C816;
					else if (IsDlgButtonChecked(hdlg, IDC_CPUMODEL_65C02))
						cpuMode = kATCPUMode_65C02;

					if (cpuem.GetCPUMode() != cpuMode) {
						cpuem.SetCPUMode(cpuMode);
						g_sim.ColdReset();
					}

					EndDialog(hdlg, TRUE);
				}
				return TRUE;
			}
			break;
	}

	return FALSE;
}

///////////////////////////////////////////////////////////////////////////////

class ATNewDiskDialog : public VDDialogFrameW32 {
public:
	ATNewDiskDialog();
	~ATNewDiskDialog();

	uint32 GetSectorCount() const { return mSectorCount; }
	uint32 GetBootSectorCount() const { return mBootSectorCount; }
	uint32 GetSectorSize() const { return mSectorSize; }

protected:
	bool OnLoaded();
	void OnDataExchange(bool write);
	bool OnCommand(uint32 id, uint32 extcode);
	void UpdateEnables();

	enum Format {
		kFormatSingle,
		kFormatMedium,
		kFormatDouble,
		kFormatCustom
	};

	Format	mFormat;
	uint32	mSectorCount;
	uint32	mBootSectorCount;
	uint32	mSectorSize;
};

ATNewDiskDialog::ATNewDiskDialog()
	: VDDialogFrameW32(IDD_CREATE_DISK)
	, mFormat(kFormatSingle)
	, mSectorCount(720)
	, mBootSectorCount(3)
	, mSectorSize(128)
{
}

ATNewDiskDialog::~ATNewDiskDialog() {
}

bool ATNewDiskDialog::OnLoaded() {
	CBAddString(IDC_FORMAT, L"Single density (720 sectors, 128 bytes/sector)");
	CBAddString(IDC_FORMAT, L"Medium density (1040 sectors, 128 bytes/sector)");
	CBAddString(IDC_FORMAT, L"Double density (720 sectors, 256 bytes/sector)");
	CBAddString(IDC_FORMAT, L"Custom");

	return VDDialogFrameW32::OnLoaded();
}

void ATNewDiskDialog::OnDataExchange(bool write) {
	ExchangeControlValueUint32(write, IDC_BOOT_SECTOR_COUNT, mBootSectorCount, 0, 255);
	ExchangeControlValueUint32(write, IDC_SECTOR_COUNT, mSectorCount, mBootSectorCount, 65535);

	if (write) {
		mSectorSize = 128;
		if (IsButtonChecked(IDC_SECTOR_SIZE_256))
			mSectorSize = 256;
		else if (IsButtonChecked(IDC_SECTOR_SIZE_512))
			mSectorSize = 512;
	} else {
		CheckButton(IDC_SECTOR_SIZE_128, mSectorSize == 128);
		CheckButton(IDC_SECTOR_SIZE_256, mSectorSize == 256);
		CheckButton(IDC_SECTOR_SIZE_512, mSectorSize == 512);
		CBSetSelectedIndex(IDC_FORMAT, (int)mFormat);
		UpdateEnables();
	}
}

bool ATNewDiskDialog::OnCommand(uint32 id, uint32 extcode) {
	if (id == IDC_FORMAT && extcode == CBN_SELCHANGE) {
		Format format = (Format)CBGetSelectedIndex(IDC_FORMAT);

		if (mFormat != format) {
			mFormat = format;
			UpdateEnables();

			switch(format) {
				case kFormatSingle:
					mSectorCount = 720;
					mBootSectorCount = 3;
					mSectorSize = 128;
					OnDataExchange(false);
					break;

				case kFormatMedium:
					mSectorCount = 1040;
					mBootSectorCount = 3;
					mSectorSize = 128;
					OnDataExchange(false);
					break;

				case kFormatDouble:
					mSectorCount = 720;
					mBootSectorCount = 3;
					mSectorSize = 256;
					OnDataExchange(false);
					break;
			}
		}
	}

	return false;
}

void ATNewDiskDialog::UpdateEnables() {
	bool custom = (CBGetSelectedIndex(IDC_FORMAT) == kFormatCustom);

	EnableControl(IDC_SECTOR_SIZE_128, custom);
	EnableControl(IDC_SECTOR_SIZE_256, custom);
	EnableControl(IDC_SECTOR_SIZE_512, custom);
	EnableControl(IDC_SECTOR_COUNT, custom);
	EnableControl(IDC_BOOT_SECTOR_COUNT, custom);
}

///////////////////////////////////////////////////////////////////////////////

class ATDiskDriveDialog : public VDDialogFrameW32 {
public:
	ATDiskDriveDialog();
	~ATDiskDriveDialog();

	bool OnLoaded();
	void OnDataExchange(bool write);
	bool OnCommand(uint32 id, uint32 extcode);
	void Update(int id);
};

ATDiskDriveDialog::ATDiskDriveDialog()
	: VDDialogFrameW32(IDD_DISK_DRIVES)
{
}

ATDiskDriveDialog::~ATDiskDriveDialog() {
}

namespace {
	static const uint32 kDiskPathID[]={
		IDC_DISKPATH1,
		IDC_DISKPATH2,
		IDC_DISKPATH3,
		IDC_DISKPATH4,
		IDC_DISKPATH5,
		IDC_DISKPATH6,
		IDC_DISKPATH7,
		IDC_DISKPATH8
	};

	static const uint32 kWriteModeID[]={
		IDC_WRITEMODE1,
		IDC_WRITEMODE2,
		IDC_WRITEMODE3,
		IDC_WRITEMODE4,
		IDC_WRITEMODE5,
		IDC_WRITEMODE6,
		IDC_WRITEMODE7,
		IDC_WRITEMODE8
	};
}

bool ATDiskDriveDialog::OnLoaded() {
	for(int i=0; i<8; ++i) {
		uint32 id = kWriteModeID[i];
		CBAddString(id, L"R/O");
		CBAddString(id, L"VirtRW");
		CBAddString(id, L"R/W");
	}

	return VDDialogFrameW32::OnLoaded();
}

void ATDiskDriveDialog::OnDataExchange(bool write) {
	if (!write) {
		for(int i=0; i<8; ++i) {
			ATDiskEmulator& disk = g_sim.GetDiskDrive(i);
			SetControlText(kDiskPathID[i], disk.GetPath());

			CBSetSelectedIndex(kWriteModeID[i], disk.IsWriteEnabled() ? disk.IsAutoFlushEnabled() ? 2 : 1 : 0);
		}
	}
}

bool ATDiskDriveDialog::OnCommand(uint32 id, uint32 extcode) {
	int index = 0;

	switch(id) {
		case IDC_EJECT8:	++index;
		case IDC_EJECT7:	++index;
		case IDC_EJECT6:	++index;
		case IDC_EJECT5:	++index;
		case IDC_EJECT4:	++index;
		case IDC_EJECT3:	++index;
		case IDC_EJECT2:	++index;
		case IDC_EJECT1:
			g_sim.GetDiskDrive(index).UnloadDisk();
			SetControlText(kDiskPathID[index], L"");
			return true;

		case IDC_BROWSE8:	++index;
		case IDC_BROWSE7:	++index;
		case IDC_BROWSE6:	++index;
		case IDC_BROWSE5:	++index;
		case IDC_BROWSE4:	++index;
		case IDC_BROWSE3:	++index;
		case IDC_BROWSE2:	++index;
		case IDC_BROWSE1:
			{
				VDStringW s(VDGetLoadFileName('disk', (VDGUIHandle)mhdlg, L"Load disk image", L"All supported types\0*.atr;*.pro;*.atx;*.xfd\0Atari disk image (*.atr, *.xfd)\0*.atr;*.xfd\0Protected disk image (*.pro)\0*.pro\0VAPI disk image (*.atx)\0*.atx\0All files\0*.*\0", L"atr"));

				if (!s.empty()) {
					ATDiskEmulator& disk = g_sim.GetDiskDrive(index);

					try {
						disk.LoadDisk(s.c_str());
						SetControlText(kDiskPathID[index], s.c_str());
					} catch(const MyError& e) {
						e.post(mhdlg, "Disk load error");
					}
				}
			}
			return true;

		case IDC_NEWDISK8:	++index;
		case IDC_NEWDISK7:	++index;
		case IDC_NEWDISK6:	++index;
		case IDC_NEWDISK5:	++index;
		case IDC_NEWDISK4:	++index;
		case IDC_NEWDISK3:	++index;
		case IDC_NEWDISK2:	++index;
		case IDC_NEWDISK1:
			{
				ATNewDiskDialog dlg;
				if (dlg.ShowDialog((VDGUIHandle)mhdlg)) {
					ATDiskEmulator& disk = g_sim.GetDiskDrive(index);

					disk.UnloadDisk();
					disk.CreateDisk(dlg.GetSectorCount(), dlg.GetBootSectorCount(), dlg.GetSectorSize());
					disk.SetWriteFlushMode(true, false);

					SetControlText(kDiskPathID[index], disk.GetPath());
					CBSetSelectedIndex(kWriteModeID[index], 1);
				}
			}
			return true;

		case IDC_SAVEAS8:	++index;
		case IDC_SAVEAS7:	++index;
		case IDC_SAVEAS6:	++index;
		case IDC_SAVEAS5:	++index;
		case IDC_SAVEAS4:	++index;
		case IDC_SAVEAS3:	++index;
		case IDC_SAVEAS2:	++index;
		case IDC_SAVEAS1:
			{
				ATDiskEmulator& disk = g_sim.GetDiskDrive(index);

				if (disk.GetPath()) {
					VDStringW s(VDGetSaveFileName(
							'disk',
							(VDGUIHandle)mhdlg,
							L"Save disk image",
							L"Atari disk image (*.atr)\0*.atr\0All files\0*.*\0",
							L"atr"));

					if (!s.empty()) {
						try {
							disk.SaveDisk(s.c_str());

							// if the disk is in VirtR/W mode, switch to R/W mode
							if (disk.IsWriteEnabled() && !disk.IsAutoFlushEnabled()) {
								disk.SetWriteFlushMode(true, true);
								OnDataExchange(false);
							}

							SetControlText(kDiskPathID[index], s.c_str());
						} catch(const MyError& e) {
							e.post(mhdlg, "Disk load error");
						}
					}
				}
			}
			return true;

		case IDC_WRITEMODE8:	++index;
		case IDC_WRITEMODE7:	++index;
		case IDC_WRITEMODE6:	++index;
		case IDC_WRITEMODE5:	++index;
		case IDC_WRITEMODE4:	++index;
		case IDC_WRITEMODE3:	++index;
		case IDC_WRITEMODE2:	++index;
		case IDC_WRITEMODE1:
			{
				int mode = CBGetSelectedIndex(id);
				ATDiskEmulator& disk = g_sim.GetDiskDrive(index);

				disk.SetWriteFlushMode(mode > 0, mode == 2);
			}
			return true;
	}

	return false;
}

///////////////////////////////////////////////////////////////////////////////

class ATHardDiskDialog : public VDDialogFrameW32 {
public:
	ATHardDiskDialog(IATHardDiskEmulator *hd);
	~ATHardDiskDialog();

	bool OnLoaded();
	void OnDataExchange(bool write);
	bool OnCommand(uint32 id, uint32 extcode);
	void Update(int id);

protected:
	IATHardDiskEmulator *mpHardDisk;
};

ATHardDiskDialog::ATHardDiskDialog(IATHardDiskEmulator *hd)
	: VDDialogFrameW32(IDD_HARD_DISK)
	, mpHardDisk(hd)
{
}

ATHardDiskDialog::~ATHardDiskDialog() {
}

bool ATHardDiskDialog::OnLoaded() {
	return VDDialogFrameW32::OnLoaded();
}

void ATHardDiskDialog::OnDataExchange(bool write) {
	if (!write) {
		CheckButton(IDC_ENABLE, mpHardDisk->IsEnabled());
		CheckButton(IDC_READONLY, mpHardDisk->IsReadOnly());
		SetControlText(IDC_PATH, mpHardDisk->GetBasePath());
	} else {
		bool enable = IsButtonChecked(IDC_ENABLE);
		bool reset = false;

		if (mpHardDisk->IsEnabled() != enable) {
			mpHardDisk->SetEnabled(enable);
			reset = true;
		}

		mpHardDisk->SetReadOnly(IsButtonChecked(IDC_READONLY));

		VDStringW path;
		GetControlText(IDC_PATH, path);
		mpHardDisk->SetBasePath(path.c_str());

		if (reset)
			g_sim.ColdReset();
	}
}

bool ATHardDiskDialog::OnCommand(uint32 id, uint32 extcode) {
	int index = 0;

	switch(id) {
		case IDC_BROWSE:
			{
				VDStringW s(VDGetDirectory('hard', (VDGUIHandle)mhdlg, L"Select base directory"));
				if (!s.empty())
					SetControlText(IDC_PATH, s.c_str());
			}
			return true;
	}

	return false;
}

///////////////////////////////////////////////////////////////////////////////

class ATAboutDialog : public VDDialogFrameW32 {
public:
	ATAboutDialog();
	~ATAboutDialog();
};

ATAboutDialog::ATAboutDialog()
	: VDDialogFrameW32(IDD_ABOUT)
{
}

ATAboutDialog::~ATAboutDialog() {
}

///////////////////////////////////////////////////////////////////////////////

class ATAudioOptionsDialog : public VDDialogFrameW32 {
public:
	ATAudioOptionsDialog();
	~ATAudioOptionsDialog();

protected:
	bool OnLoaded();
	void OnDataExchange(bool write);
	void OnHScroll(uint32 id, int code);
	void UpdateDecibelLabel();

	int mVolumeTick;
};

ATAudioOptionsDialog::ATAudioOptionsDialog()
	: VDDialogFrameW32(IDD_AUDIO_OPTIONS)
{
}

ATAudioOptionsDialog::~ATAudioOptionsDialog() {
}

bool ATAudioOptionsDialog::OnLoaded() {
	TBSetRange(IDC_VOLUME, 0, 200);

	OnDataExchange(false);
	SetFocusToControl(IDC_VOLUME);
	return true;
}

void ATAudioOptionsDialog::OnDataExchange(bool write) {
	if (write) {
		g_sim.GetPokey().SetVolume(powf(10.0f, (mVolumeTick - 200) * 0.01f));
	} else {
		float volume = g_sim.GetPokey().GetVolume();
		mVolumeTick = 200 + VDRoundToInt(100.0f * log10(volume));

		TBSetValue(IDC_VOLUME, mVolumeTick);
		UpdateDecibelLabel();
	}
}

void ATAudioOptionsDialog::OnHScroll(uint32 id, int code) {
	if (id == IDC_VOLUME) {
		int tick = TBGetValue(IDC_VOLUME);

		if (tick != mVolumeTick) {
			mVolumeTick = tick;
			UpdateDecibelLabel();
		}
	}
}

void ATAudioOptionsDialog::UpdateDecibelLabel() {
	SetControlTextF(IDC_STATIC_VOLUME, L"%.1fdB", 0.1f * (mVolumeTick - 200));
}

///////////////////////////////////////////////////////////////////////////////

class ATTapeControlDialog : public VDDialogFrameW32 {
public:
	ATTapeControlDialog(ATCassetteEmulator& tape);

protected:
	bool OnLoaded();
	void OnHScroll(uint32 id, int code);
	void UpdateLabelText();
	void AppendTime(VDStringW& s, float t);

	ATCassetteEmulator& mTape;
	VDStringW mLabel;
	float mPos;
	float mLength;
	float mSecondsPerTick;
};

ATTapeControlDialog::ATTapeControlDialog(ATCassetteEmulator& tape)
	: VDDialogFrameW32(IDD_TAPE_CONTROL)
	, mTape(tape)
{
}

bool ATTapeControlDialog::OnLoaded() {
	mPos = mTape.GetPosition();
	mLength = ceilf(mTape.GetLength());

	float r = mLength < 1e-5f ? 0.0f : mPos / mLength;
	int ticks = 100000;

	if (mLength < 10000.0f)
		ticks = VDCeilToInt(mLength * 10.0f);

	mSecondsPerTick = mLength / (float)ticks;

	TBSetRange(IDC_POSITION, 0, ticks);
	TBSetValue(IDC_POSITION, VDRoundToInt(r * (float)ticks));
	
	UpdateLabelText();
	SetFocusToControl(IDC_POSITION);
	return true;
}

void ATTapeControlDialog::OnHScroll(uint32 id, int code) {
	float pos = (float)TBGetValue(IDC_POSITION) * mSecondsPerTick;

	if (pos != mPos) {
		mPos = pos;

		mTape.SeekToTime(mPos);
		UpdateLabelText();
	}
}

void ATTapeControlDialog::UpdateLabelText() {
	mLabel.clear();
	AppendTime(mLabel, mTape.GetPosition());
	mLabel += L'/';
	AppendTime(mLabel, mTape.GetLength());

	SetControlText(IDC_STATIC_POSITION, mLabel.c_str());
}

void ATTapeControlDialog::AppendTime(VDStringW& s, float t) {
	sint32 ticks = VDRoundToInt32(t*10.0f);
	int minutesWidth = 1;

	if (ticks >= 36000) {
		sint32 hours = ticks / 36000;
		ticks %= 36000;

		s.append_sprintf(L"%d:", hours);
		minutesWidth = 2;
	}

	sint32 minutes = ticks / 600;
	ticks %= 600;

	sint32 seconds = ticks / 10;
	ticks %= 10;

	s.append_sprintf(L"%0*d:%02d.%d", minutesWidth, minutes, seconds, ticks);
}

///////////////////////////////////////////////////////////////////////////////

class ATAdjustColorsDialog : public VDDialogFrameW32 {
public:
	ATAdjustColorsDialog();

protected:
	bool OnLoaded();
	void OnDestroy();
	void OnDataExchange(bool write);
	bool OnCommand(uint32 id, uint32 extcode);
	void OnHScroll(uint32 id, int code);
	VDZINT_PTR DlgProc(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam);
	void UpdateLabel(uint32 id);
	void UpdateColorImage();

	ATColorParams mParams;
	HMENU mPresetsPopupMenu;
};

ATAdjustColorsDialog g_adjustColorsDialog;

ATAdjustColorsDialog::ATAdjustColorsDialog()
	: VDDialogFrameW32(IDD_ADJUST_COLORS)
	, mPresetsPopupMenu(NULL)
{
}

bool ATAdjustColorsDialog::OnLoaded() {
	mPresetsPopupMenu = LoadMenu(VDGetLocalModuleHandleW32(), MAKEINTRESOURCE(IDR_COLOR_PRESETS_MENU));

	TBSetRange(IDC_HUESTART, -60, 360);
	TBSetRange(IDC_HUERANGE, 0, 540);
	TBSetRange(IDC_BRIGHTNESS, -50, 50);
	TBSetRange(IDC_CONTRAST, 0, 200);
	TBSetRange(IDC_SATURATION, 0, 100);
	TBSetRange(IDC_ARTPHASE, -60, 360);
	TBSetRange(IDC_ARTSAT, 0, 400);
	TBSetRange(IDC_ARTBRI, -50, 50);

	OnDataExchange(false);
	SetFocusToControl(IDC_HUESTART);
	return true;
}

void ATAdjustColorsDialog::OnDestroy() {
	if (mPresetsPopupMenu) {
		DestroyMenu(mPresetsPopupMenu);
		mPresetsPopupMenu = NULL;
	}

	VDDialogFrameW32::OnDestroy();
}

void ATAdjustColorsDialog::OnDataExchange(bool write) {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	if (write) {
	} else {
		mParams = gtia.GetColorParams();
		TBSetValue(IDC_HUESTART, VDRoundToInt(mParams.mHueStart));
		TBSetValue(IDC_HUERANGE, VDRoundToInt(mParams.mHueRange));
		TBSetValue(IDC_BRIGHTNESS, VDRoundToInt(mParams.mBrightness * 100.0f));
		TBSetValue(IDC_CONTRAST, VDRoundToInt(mParams.mContrast * 100.0f));
		TBSetValue(IDC_SATURATION, VDRoundToInt(mParams.mSaturation * 100.0f));
		TBSetValue(IDC_ARTPHASE, VDRoundToInt(mParams.mArtifactHue));
		TBSetValue(IDC_ARTSAT, VDRoundToInt(mParams.mArtifactSat * 100.0f));
		TBSetValue(IDC_ARTBRI, VDRoundToInt(mParams.mArtifactBias * 100.0f));

		UpdateLabel(IDC_HUESTART);
		UpdateLabel(IDC_HUERANGE);
		UpdateLabel(IDC_BRIGHTNESS);
		UpdateLabel(IDC_CONTRAST);
		UpdateLabel(IDC_SATURATION);
		UpdateLabel(IDC_ARTPHASE);
		UpdateLabel(IDC_ARTSAT);
		UpdateLabel(IDC_ARTBRI);
		UpdateColorImage();
	}
}

bool ATAdjustColorsDialog::OnCommand(uint32 id, uint32 extcode) {
	if (id == IDC_LOADPRESET) {
		TPMPARAMS tpp = { sizeof(TPMPARAMS) };

		GetWindowRect(GetDlgItem(mhdlg, IDC_LOADPRESET), &tpp.rcExclude);

		TrackPopupMenuEx(GetSubMenu(mPresetsPopupMenu, 0), TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON, tpp.rcExclude.right, tpp.rcExclude.top, mhdlg, &tpp);
		return true;
	} else if (id == ID_COLORS_ORIGINAL) {
		ATGTIAEmulator& gtia = g_sim.GetGTIA();
		gtia.ResetColors();
		OnDataExchange(false);
	} else if (id == ID_COLORS_ALTERNATE) {
		ATColorParams params;
		params.mHueStart = -29.0f;
		params.mHueRange = 26.1f * 15.0f;
		params.mBrightness = -0.03f;
		params.mContrast = 0.88f;
		params.mSaturation = 0.23f;
		params.mArtifactHue = 96.0f;
		params.mArtifactSat = 2.76f;
		params.mArtifactBias = 0.35f;

		ATGTIAEmulator& gtia = g_sim.GetGTIA();
		gtia.SetColorParams(params);
		OnDataExchange(false);
	} else if (id == ID_COLORS_AUTHENTICNTSC) {
		ATColorParams params;
		params.mHueStart = -51.0f;
		params.mHueRange = 418.0f;
		params.mBrightness = -0.11f;
		params.mContrast = 1.04f;
		params.mSaturation = 0.34f;
		params.mArtifactHue = 96.0f;
		params.mArtifactSat = 2.76f;
		params.mArtifactBias = 0.35f;

		ATGTIAEmulator& gtia = g_sim.GetGTIA();
		gtia.SetColorParams(params);
		OnDataExchange(false);
	}

	return false;
}

void ATAdjustColorsDialog::OnHScroll(uint32 id, int code) {
	if (id == IDC_HUESTART) {
		float v = (float)TBGetValue(IDC_HUESTART);

		if (mParams.mHueStart != v) {
			mParams.mHueStart = v;

			g_sim.GetGTIA().SetColorParams(mParams);
			UpdateColorImage();
			UpdateLabel(id);
		}
	} else if (id == IDC_HUERANGE) {
		float v = (float)TBGetValue(IDC_HUERANGE);

		if (mParams.mHueRange != v) {
			mParams.mHueRange = v;

			g_sim.GetGTIA().SetColorParams(mParams);
			UpdateColorImage();
			UpdateLabel(id);
		}
	} else if (id == IDC_BRIGHTNESS) {
		float v = (float)TBGetValue(IDC_BRIGHTNESS) / 100.0f;

		if (mParams.mBrightness != v) {
			mParams.mBrightness = v;

			g_sim.GetGTIA().SetColorParams(mParams);
			UpdateColorImage();
			UpdateLabel(id);
		}
	} else if (id == IDC_CONTRAST) {
		float v = (float)TBGetValue(IDC_CONTRAST) / 100.0f;

		if (mParams.mContrast != v) {
			mParams.mContrast = v;

			g_sim.GetGTIA().SetColorParams(mParams);
			UpdateColorImage();
			UpdateLabel(id);
		}
	} else if (id == IDC_SATURATION) {
		float v = (float)TBGetValue(IDC_SATURATION) / 100.0f;

		if (mParams.mSaturation != v) {
			mParams.mSaturation = v;

			g_sim.GetGTIA().SetColorParams(mParams);
			UpdateColorImage();
			UpdateLabel(id);
		}
	} else if (id == IDC_ARTPHASE) {
		float v = (float)TBGetValue(IDC_ARTPHASE);

		if (mParams.mArtifactHue != v) {
			mParams.mArtifactHue = v;

			g_sim.GetGTIA().SetColorParams(mParams);
			UpdateColorImage();
			UpdateLabel(id);
		}
	} else if (id == IDC_ARTSAT) {
		float v = (float)TBGetValue(IDC_ARTSAT) / 100.0f;

		if (mParams.mArtifactSat != v) {
			mParams.mArtifactSat = v;

			g_sim.GetGTIA().SetColorParams(mParams);
			UpdateColorImage();
			UpdateLabel(id);
		}
	} else if (id == IDC_ARTBRI) {
		float v = (float)TBGetValue(IDC_ARTBRI) / 100.0f;

		if (mParams.mArtifactBias != v) {
			mParams.mArtifactBias = v;

			g_sim.GetGTIA().SetColorParams(mParams);
			UpdateColorImage();
			UpdateLabel(id);
		}
	}
}

VDZINT_PTR ATAdjustColorsDialog::DlgProc(VDZUINT msg, VDZWPARAM wParam, VDZLPARAM lParam) {
	if (msg == WM_DRAWITEM) {
		if (wParam == IDC_COLORS) {
			const DRAWITEMSTRUCT& drawInfo = *(const DRAWITEMSTRUCT *)lParam;

			BITMAPINFO bi = {
				{
					sizeof(BITMAPINFOHEADER),
					16,
					16,
					1,
					32,
					BI_RGB,
					16*16*4,
					0,
					0,
					0,
					0
				}
			};

			uint32 pal[256];
			g_sim.GetGTIA().GetPalette(pal);

			for(int i=0; i<128; i += 16)
				VDSwapMemory(&pal[i], &pal[240-i], 16*sizeof(uint32));

			StretchDIBits(drawInfo.hDC,
				drawInfo.rcItem.left,
				drawInfo.rcItem.top,
				drawInfo.rcItem.right - drawInfo.rcItem.left,
				drawInfo.rcItem.bottom - drawInfo.rcItem.top,
				0, 0, 16, 16, pal, &bi, DIB_RGB_COLORS, SRCCOPY);

			SetWindowLongPtr(mhdlg, DWLP_MSGRESULT, TRUE);
			return TRUE;
		}
	}

	return VDDialogFrameW32::DlgProc(msg, wParam, lParam);
}

void ATAdjustColorsDialog::UpdateLabel(uint32 id) {
	switch(id) {
		case IDC_HUESTART:
			SetControlTextF(IDC_STATIC_HUESTART, L"%.0f\u00B0", mParams.mHueStart);
			break;
		case IDC_HUERANGE:
			SetControlTextF(IDC_STATIC_HUERANGE, L"%.1f\u00B0", mParams.mHueRange / 15.0f);
			break;
		case IDC_BRIGHTNESS:
			SetControlTextF(IDC_STATIC_BRIGHTNESS, L"%+.0f%%", mParams.mBrightness * 100.0f);
			break;
		case IDC_CONTRAST:
			SetControlTextF(IDC_STATIC_CONTRAST, L"%.0f%%", mParams.mContrast * 100.0f);
			break;
		case IDC_SATURATION:
			SetControlTextF(IDC_STATIC_SATURATION, L"%.0f%%", mParams.mSaturation * 100.0f);
			break;
		case IDC_ARTPHASE:
			SetControlTextF(IDC_STATIC_ARTPHASE, L"%.0f\u00B0", mParams.mArtifactHue);
			break;
		case IDC_ARTSAT:
			SetControlTextF(IDC_STATIC_ARTSAT, L"%.0f%%", mParams.mArtifactSat * 100.0f);
			break;
		case IDC_ARTBRI:
			SetControlTextF(IDC_STATIC_ARTBRI, L"%+.0f%%", mParams.mArtifactBias * 100.0f);
			break;
	}
}

void ATAdjustColorsDialog::UpdateColorImage() {
	// update image
	HWND hwndColors = GetDlgItem(mhdlg, IDC_COLORS);
	InvalidateRect(hwndColors, NULL, FALSE);
}

///////////////////////////////////////////////////////////////////////////////
void OnCommandOpen(bool forceColdBoot) {
	VDStringW fn(VDGetLoadFileName('load', (VDGUIHandle)g_hwnd, L"Load disk, cassette, cartridge, or program image",
		L"All supported types\0*.atr;*.xfd;*.dcm;*.pro;*.atx;*.xex;*.obx;*.com;*.car;*.rom;*.bin;*.cas;*.wav\0"
		L"Atari program (*.xex,*.obx,*.com)\0*.xex;*.obx;*.com\0"
		L"Atari disk image (*.atr,*.xfd,*.dcm)\0*.atr;*.xfd;*.dcm\0"
		L"Protected disk image (*.pro)\0*.pro\0"
		L"VAPI disk image (*.atx)\0*.atx\0"
		L"Cartridge (*.rom,*.bin)\0*.rom;*.bin\0"
		L"Cassette tape (*.cas,*.wav)\0*.cas;*.wav\0"
		L"All files\0*.*\0",
		L"atr"
		));

	if (!fn.empty()) {
		try {
			if (forceColdBoot)
				g_sim.UnloadAll();

			g_sim.Load(fn.c_str());

			if (forceColdBoot)
				g_sim.ColdReset();
		} catch(const MyError& e) {
			e.post(g_hwnd, "Altirra Error");
		}
	}
}

void ATSetFullscreen(bool fs) {
	if (!g_sim.IsRunning())
		fs = false;

	if (fs == g_fullscreen)
		return;

	ATUIPane *dispPane = ATGetUIPane(kATUIPaneId_Display);
	ATFrameWindow *frame = NULL;

	if (dispPane) {
		HWND parent = ::GetParent(dispPane->GetHandleW32());

		if (parent) {
			frame = ATFrameWindow::GetFrameWindow(parent);

			if (frame)
				frame->SetFullScreen(fs);
		}
	}

	DWORD style = GetWindowLong(g_hwnd, GWL_STYLE);
	if (fs) {
		SetMenu(g_hwnd, NULL);
		SetWindowLong(g_hwnd, GWL_STYLE, (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
		SetWindowPos(g_hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_FRAMECHANGED|SWP_NOZORDER);
		SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE);
		ShowWindow(g_hwnd, SW_SHOWNORMAL);
		ShowWindow(g_hwnd, SW_MAXIMIZE);
		g_fullscreen = true;
		if (g_pDisplay)
			g_pDisplay->SetFullScreen(true);
		g_sim.SetFrameSkipEnabled(false);
	} else {
		if (g_pDisplay)
			g_pDisplay->SetFullScreen(false);
		SetWindowLong(g_hwnd, GWL_STYLE, (style | WS_OVERLAPPEDWINDOW) & ~WS_POPUP);
		SetWindowPos(g_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_FRAMECHANGED|SWP_NOACTIVATE);
		ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
		g_fullscreen = false;
		SetMenu(g_hwnd, g_hMenu);
		g_sim.SetFrameSkipEnabled(true);
	}
}

void OnCommandToggleFullScreen() {
	if (!g_enhancedText)
		ATSetFullscreen(!g_fullscreen);
}

void OnCommandFilterModeNext() {
	if (!g_pDisplay)
		return;

	switch(g_pDisplay->GetFilterMode()) {
		case IVDVideoDisplay::kFilterPoint:
			g_dispFilterMode = IVDVideoDisplay::kFilterBilinear;
			break;
		case IVDVideoDisplay::kFilterBilinear:
			g_dispFilterMode = IVDVideoDisplay::kFilterBicubic;
			break;
		case IVDVideoDisplay::kFilterBicubic:
			g_dispFilterMode = IVDVideoDisplay::kFilterAnySuitable;
			break;
		case IVDVideoDisplay::kFilterAnySuitable:
			g_dispFilterMode = IVDVideoDisplay::kFilterPoint;
			break;
	}

	g_pDisplay->SetFilterMode(g_dispFilterMode);
}

void OnCommandAnticVisualizationNext() {
	ATAnticEmulator& antic = g_sim.GetAntic();
	antic.SetAnalysisMode((ATAnticEmulator::AnalysisMode)(((int)antic.GetAnalysisMode() + 1) % ATAnticEmulator::kAnalyzeModeCount));
}

void OnCommandGTIAVisualizationNext() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	gtia.SetAnalysisMode((ATGTIAEmulator::AnalysisMode)(((int)gtia.GetAnalysisMode() + 1) % ATGTIAEmulator::kAnalyzeCount));
}

void OnCommandEnhancedTextFont() {
	CHOOSEFONTW cf = {sizeof(CHOOSEFONTW)};

	cf.hwndOwner	= g_hwnd;
	cf.hDC			= NULL;
	cf.lpLogFont	= &g_enhancedTextFont;
	cf.iPointSize	= 0;
	cf.Flags		= CF_FIXEDPITCHONLY | CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT;

	if (ChooseFontW(&cf)) {
		g_enhancedTextFont.lfWidth			= 0;
		g_enhancedTextFont.lfEscapement		= 0;
		g_enhancedTextFont.lfOrientation	= 0;
		g_enhancedTextFont.lfWeight			= 0;
		g_enhancedTextFont.lfItalic			= FALSE;
		g_enhancedTextFont.lfUnderline		= FALSE;
		g_enhancedTextFont.lfStrikeOut		= FALSE;
		g_enhancedTextFont.lfCharSet		= DEFAULT_CHARSET;
		g_enhancedTextFont.lfOutPrecision	= OUT_DEFAULT_PRECIS;
		g_enhancedTextFont.lfClipPrecision	= CLIP_DEFAULT_PRECIS;
		g_enhancedTextFont.lfQuality		= DEFAULT_QUALITY;
		g_enhancedTextFont.lfPitchAndFamily	= FF_DONTCARE | DEFAULT_PITCH;

		VDRegistryAppKey key("Settings");
		key.setString("Enhanced video: Font family", g_enhancedTextFont.lfFaceName);
		key.setInt("Enhanced video: Font size", g_enhancedTextFont.lfHeight);

		ATDisplayBasePane *pane = static_cast<ATDisplayBasePane *>(ATGetUIPane(kATUIPaneId_Display));
		if (pane)
			pane->UpdateTextModeFont();
	}
}

void OnCommandPaste() {
	if (OpenClipboard(NULL)) {
		HANDLE hData = GetClipboardData(CF_TEXT);

		if (hData) {
			void *data = GlobalLock(hData);

			if (data) {
				size_t len = GlobalSize(hData);
				const char *s = (const char *)data;

				char skipLT = 0;
				while(len--) {
					char c = *s++;

					if (c == skipLT) {
						skipLT = 0;
						continue;
					}

					skipLT = 0;

					if (c == '\r' || c == '\n') {
						skipLT = c ^ ('\r' ^ '\n');
						g_sim.GetPokey().PushKey(0x0C, false, true, false);
					} else
						ProcessKey(c, false, true);
				}

				GlobalUnlock(hData);
			}
		}

		CloseClipboard();
	}
}

void ResizeDisplay() {
	ATDisplayBasePane *pane = static_cast<ATDisplayBasePane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->OnSize();
}

bool OnCommand(UINT id) {
	switch(id) {
		case ID_FILE_OPENIMAGE:
			OnCommandOpen(false);
			return true;

		case ID_FILE_BOOTIMAGE:
			OnCommandOpen(true);
			return true;

		case ID_FILE_DISKDRIVES:
			ATDiskDriveDialog().ShowDialog((VDGUIHandle)g_hwnd);
			return true;

		case ID_FILE_QUICKLOADSTATE:
			if (!g_quickSave.empty()) {
				try {
					ATSaveStateReader reader(g_quickSave.data(), g_quickSave.size());

					g_sim.LoadState(reader);
				} catch(const MyError& e) {
					e.post(g_hwnd, "Load state error");
				}
			}
			return true;

		case ID_FILE_QUICKSAVESTATE:
			try {
				g_quickSave.clear();

				ATSaveStateWriter writer(g_quickSave);

				g_sim.SaveState(writer);
			} catch(const MyError& e) {
				e.post(g_hwnd, "Save state error");
			}
			return true;

		case ID_FILE_ATTACHCARTRIDGE:
			{
				VDStringW fn(VDGetLoadFileName('cart', (VDGUIHandle)g_hwnd, L"Load cartridge",
					L"All supported types\0*.bin;*.car;*.rom\0"
					L"Cartridge image (*.bin,*.car,*.rom)\0*.bin;*.car;*.rom\0"
					L"All files\0*.*\0",

					L"bin"));

				if (!fn.empty()) {
					try {
						g_sim.LoadCartridge(fn.c_str());
						g_sim.ColdReset();
					} catch(const MyError& e) {
						e.post(g_hwnd, "Altirra Error");
					}
				}
			}
			return true;

		case ID_FILE_DETACHCARTRIDGE:
			g_sim.LoadCartridge(NULL);
			g_sim.ColdReset();
			return true;

		case ID_ATTACHSPECIALCARTRIDGE_SUPERCHARGER3D:
			g_sim.LoadCartridgeSC3D();
			g_sim.ColdReset();
			return true;

		case ID_FILE_EXIT:
			PostQuitMessage(0);
			return true;

		case ID_CASSETTE_LOAD:
			{
				VDStringW fn(VDGetLoadFileName('cass', (VDGUIHandle)g_hwnd, L"Load cassette tape", L"All supported types\0*.wav;*.cas\0Atari cassette image (*.cas)\0*.cas\0Waveform audio (*.wav)\0*.wav\0All files\0*.*\0", L"wav"));

				if (!fn.empty()) {
					try {
						g_sim.GetCassette().Load(fn.c_str());
					} catch(const MyError& e) {
						e.post(g_hwnd, "Altirra Error");
					}
				}
			}
			return true;

		case ID_CASSETTE_UNLOAD:
			g_sim.GetCassette().Unload();
			return true;

		case ID_CASSETTE_TAPECONTROL:
			{
				ATTapeControlDialog dlg(g_sim.GetCassette());

				dlg.ShowDialog((VDGUIHandle)g_hwnd);
			}
			return true;

		case ID_CASSETTE_SIOPATCH:
			g_sim.SetCassetteSIOPatchEnabled(!g_sim.IsCassetteSIOPatchEnabled());
			return true;

		case ID_CASSETTE_AUTOBOOT:
			g_sim.SetCassetteAutoBootEnabled(!g_sim.IsCassetteAutoBootEnabled());
			return true;

		case ID_CASSETTE_LOADDATAASAUDIO:
			{
				ATCassetteEmulator& cas = g_sim.GetCassette();

				cas.SetLoadDataAsAudioEnable(!cas.IsLoadDataAsAudioEnabled());
			}
			return true;

		case ID_DEBUG_OPENSOURCE:
			{
				VDStringW fn(VDGetLoadFileName('src ', (VDGUIHandle)g_hwnd, L"Load source file", L"All files (*.*)\0*.*\0", NULL));

				if (!fn.empty()) {
					ATOpenSourceWindow(fn.c_str());
				}
			}
			return true;

		case ID_FILTERMODE_NEXTMODE:
			OnCommandFilterModeNext();
			return true;

		case ID_FILTERMODE_POINT:
			g_dispFilterMode = IVDVideoDisplay::kFilterPoint;
			if (g_pDisplay)
				g_pDisplay->SetFilterMode(IVDVideoDisplay::kFilterPoint);
			return true;

		case ID_FILTERMODE_BILINEAR:
			g_dispFilterMode = IVDVideoDisplay::kFilterBilinear;
			if (g_pDisplay)
				g_pDisplay->SetFilterMode(IVDVideoDisplay::kFilterBilinear);
			return true;

		case ID_FILTERMODE_BICUBIC:
			g_dispFilterMode = IVDVideoDisplay::kFilterBicubic;
			if (g_pDisplay)
				g_pDisplay->SetFilterMode(IVDVideoDisplay::kFilterBicubic);
			return true;

		case ID_FILTERMODE_ANYSUITABLE:
			g_dispFilterMode = IVDVideoDisplay::kFilterAnySuitable;
			if (g_pDisplay)
				g_pDisplay->SetFilterMode(IVDVideoDisplay::kFilterAnySuitable);
			return true;

		case ID_STRETCHMODE_FITTOWINDOW:
			g_displayStretchMode = kATDisplayStretchMode_Unconstrained;
			ResizeDisplay();
			return true;

		case ID_STRETCHMODE_PRESERVEASPECTRATIO:
			g_displayStretchMode = kATDisplayStretchMode_PreserveAspectRatio;
			ResizeDisplay();
			return true;

		case ID_STRETCHMODE_SQUAREPIXELS:
			g_displayStretchMode = kATDisplayStretchMode_SquarePixels;
			ResizeDisplay();
			return true;

		case ID_STRETCHMODE_INTEGRALSQUAREPIXELS:
			g_displayStretchMode = kATDisplayStretchMode_Integral;
			ResizeDisplay();
			return true;

		case ID_OVERSCAN_NORMAL:
			g_sim.GetGTIA().SetOverscanMode(ATGTIAEmulator::kOverscanNormal);
			ResizeDisplay();
			return true;

		case ID_OVERSCAN_EXTENDED:
			g_sim.GetGTIA().SetOverscanMode(ATGTIAEmulator::kOverscanExtended);
			ResizeDisplay();
			return true;

		case ID_OVERSCAN_FULL:
			g_sim.GetGTIA().SetOverscanMode(ATGTIAEmulator::kOverscanFull);
			ResizeDisplay();
			return true;

		case ID_OVERSCAN_PALEXTENDED:
			g_sim.GetGTIA().SetOverscanPALExtended(!g_sim.GetGTIA().IsOverscanPALExtended());
			ResizeDisplay();
			return true;


		case ID_VIEW_FULLSCREEN:
			OnCommandToggleFullScreen();
			return true;

		case ID_VIEW_VSYNC:
			{
				ATGTIAEmulator& gtia = g_sim.GetGTIA();

				gtia.SetVsyncEnabled(!gtia.IsVsyncEnabled());
			}
			return true;

		case ID_VIEW_SHOWFPS:
			g_showFps = !g_showFps;
			return true;

		case ID_VIEW_DISPLAY:
			ATActivateUIPane(kATUIPaneId_Display, true);
			return true;

		case ID_VIEW_PRINTEROUTPUT:
			ATActivateUIPane(kATUIPaneId_PrinterOutput, true);
			return true;

		case ID_VIEW_CONSOLE:
			if (ATIsDebugConsoleActive())
				ATActivateUIPane(kATUIPaneId_Console, true);
			return true;

		case ID_VIEW_REGISTERS:
			if (ATIsDebugConsoleActive())
				ATActivateUIPane(kATUIPaneId_Registers, true);
			return true;

		case ID_VIEW_DISASSEMBLY:
			if (ATIsDebugConsoleActive())
				ATActivateUIPane(kATUIPaneId_Disassembly, true);
			return true;

		case ID_VIEW_CALLSTACK:
			if (ATIsDebugConsoleActive())
				ATActivateUIPane(kATUIPaneId_CallStack, true);
			return true;

		case ID_VIEW_HISTORY:
			if (ATIsDebugConsoleActive())
				ATActivateUIPane(kATUIPaneId_History, true);
			return true;

		case ID_VIEW_MEMORY:
			if (ATIsDebugConsoleActive())
				ATActivateUIPane(kATUIPaneId_Memory, true);
			return true;

		case ID_VIEW_COPYFRAMETOCLIPBOARD:
			{
				const VDPixmap *frame = g_sim.GetGTIA().GetLastFrameBuffer();

				if (frame)
					ATCopyFrameToClipboard(g_hwnd, *frame);
			}
			return true;

		case ID_VIEW_RESETWINDOWLAYOUT:
			ATLoadDefaultPaneLayout();
			return true;

		case ID_VIEW_PASTE:
			OnCommandPaste();
			return true;

		case ID_ANTICVISUALIZATION_NEXTMODE:
			OnCommandAnticVisualizationNext();
			return true;

		case ID_GTIAVISUALIZATION_NEXTMODE:
			OnCommandGTIAVisualizationNext();
			return true;

		case ID_HARDWARE_800:
			g_sim.SetHardwareMode(kATHardwareMode_800);
			switch(g_sim.GetKernelMode()) {
				case kATKernelMode_XL:
				case kATKernelMode_Other:
					g_sim.SetKernelMode(kATKernelMode_Default);
					break;
			}
			g_sim.ColdReset();
			return true;

		case ID_HARDWARE_800XL:
			g_sim.SetHardwareMode(kATHardwareMode_800XL);
			g_sim.ColdReset();
			return true;

		case ID_FIRMWARE_DEFAULT:
			g_sim.SetKernelMode(kATKernelMode_Default);
			g_sim.ColdReset();
			return true;

		case ID_FIRMWARE_OSA:
			g_sim.SetKernelMode(kATKernelMode_OSA);
			g_sim.ColdReset();
			return true;

		case ID_FIRMWARE_OSB:
			g_sim.SetKernelMode(kATKernelMode_OSB);
			g_sim.ColdReset();
			return true;

		case ID_FIRMWARE_800XL:
			g_sim.SetKernelMode(kATKernelMode_XL);

			if (g_sim.GetHardwareMode() == kATHardwareMode_800)
				g_sim.SetHardwareMode(kATHardwareMode_800XL);

			switch(g_sim.GetMemoryMode()) {
				case kATMemoryMode_48K:
				case kATMemoryMode_52K:
					g_sim.SetMemoryMode(kATMemoryMode_64K);
					break;
			}

			g_sim.ColdReset();
			return true;

		case ID_FIRMWARE_OTHER:
			g_sim.SetKernelMode(kATKernelMode_Other);

			if (g_sim.GetHardwareMode() == kATHardwareMode_800)
				g_sim.SetHardwareMode(kATHardwareMode_800XL);

			switch(g_sim.GetMemoryMode()) {
				case kATMemoryMode_48K:
				case kATMemoryMode_52K:
					g_sim.SetMemoryMode(kATMemoryMode_64K);
					break;
			}

			g_sim.ColdReset();
			return true;

		case ID_FIRMWARE_HLE:
			g_sim.SetKernelMode(kATKernelMode_HLE);
			g_sim.ColdReset();
			return true;

		case ID_FIRMWARE_LLE:
			g_sim.SetKernelMode(kATKernelMode_LLE);
			g_sim.ColdReset();
			return true;

		case ID_FIRMWARE_BASIC:
			g_sim.SetBASICEnabled(!g_sim.IsBASICEnabled());
			return true;

		case ID_MEMORYSIZE_48K:
			g_sim.SetMemoryMode(kATMemoryMode_48K);
			if (g_sim.GetKernelMode() == kATKernelMode_XL)
				g_sim.SetKernelMode(kATKernelMode_OSB);
			if (g_sim.GetHardwareMode() == kATHardwareMode_800XL)
				g_sim.SetHardwareMode(kATHardwareMode_800);
			g_sim.ColdReset();
			return true;

		case ID_MEMORYSIZE_52K:
			g_sim.SetMemoryMode(kATMemoryMode_52K);
			if (g_sim.GetKernelMode() == kATKernelMode_XL)
				g_sim.SetKernelMode(kATKernelMode_OSB);
			if (g_sim.GetHardwareMode() == kATHardwareMode_800XL)
				g_sim.SetHardwareMode(kATHardwareMode_800);
			g_sim.ColdReset();
			return true;

		case ID_MEMORYSIZE_64K:
			g_sim.SetMemoryMode(kATMemoryMode_64K);
			g_sim.ColdReset();
			return true;

		case ID_MEMORYSIZE_128K:
			g_sim.SetMemoryMode(kATMemoryMode_128K);
			g_sim.ColdReset();
			return true;

		case ID_MEMORYSIZE_320K:
			g_sim.SetMemoryMode(kATMemoryMode_320K);
			g_sim.ColdReset();
			return true;

		case ID_MEMORYSIZE_576K:
			g_sim.SetMemoryMode(kATMemoryMode_576K);
			g_sim.ColdReset();
			return true;

		case ID_MEMORYSIZE_1088K:
			g_sim.SetMemoryMode(kATMemoryMode_1088K);
			g_sim.ColdReset();
			return true;

		case ID_MEMORY_RANDOMIZE:
			g_sim.SetRandomFillEnabled(!g_sim.IsRandomFillEnabled());
			return true;

		case ID_SYSTEM_WARMRESET:
			g_sim.WarmReset();
			g_sim.Resume();
			return true;

		case ID_SYSTEM_COLDRESET:
			g_sim.ColdReset();
			g_sim.Resume();
			return true;

		case ID_SYSTEM_PRINTER:
			g_sim.SetPrinterEnabled(!g_sim.IsPrinterEnabled());
			return true;

		case ID_SYSTEM_RTIME8:
			g_sim.SetRTime8Enabled(!g_sim.IsRTime8Enabled());
			return true;

		case ID_SYSTEM_PAL:
			g_sim.SetPALMode(!g_sim.IsPALMode());
			{
				ATDisplayBasePane *pane = static_cast<ATDisplayBasePane *>(ATGetUIPane(kATUIPaneId_Display));
				if (pane)
					pane->OnSize();
			}
			return true;

		case ID_VIDEO_FRAMEBLENDING:
			{
				ATGTIAEmulator& gtia = g_sim.GetGTIA();

				gtia.SetBlendModeEnabled(!gtia.IsBlendModeEnabled());
			}
			return true;

		case ID_VIDEO_INTERLACE:
			{
				ATGTIAEmulator& gtia = g_sim.GetGTIA();

				gtia.SetInterlaceEnabled(!gtia.IsInterlaceEnabled());
			}
			return true;

		case ID_VIDEO_VBXE:
			g_sim.SetVBXEEnabled(!g_sim.GetVBXE());
			g_sim.ColdReset();
			return true;

		case ID_VIDEO_VBXESHAREDMEMORY:
			g_sim.SetVBXESharedMemoryEnabled(!g_sim.IsVBXESharedMemoryEnabled());
			g_sim.ColdReset();
			return true;

		case ID_VIDEO_VBXEALTPAGE:
			g_sim.SetVBXEAltPageEnabled(!g_sim.IsVBXEAltPageEnabled());
			g_sim.ColdReset();
			return true;

		case ID_VIDEO_NOARTIFACTING:
			{
				ATGTIAEmulator& gtia = g_sim.GetGTIA();

				gtia.SetArtifactingMode(ATGTIAEmulator::kArtifactNone);
			}
			return true;

		case ID_VIDEO_NTSCARTIFACTING:
			{
				ATGTIAEmulator& gtia = g_sim.GetGTIA();

				gtia.SetArtifactingMode(ATGTIAEmulator::kArtifactNTSC);
			}
			return true;

		case ID_VIDEO_NTSCARTIFACTINGHI:
			{
				ATGTIAEmulator& gtia = g_sim.GetGTIA();

				gtia.SetArtifactingMode(ATGTIAEmulator::kArtifactNTSCHi);
			}
			return true;

		case ID_VIDEO_PALARTIFACTING:
			{
				ATGTIAEmulator& gtia = g_sim.GetGTIA();

				gtia.SetArtifactingMode(ATGTIAEmulator::kArtifactPAL);
			}
			return true;

		case ID_VIDEO_ADJUSTCOLORS:
			g_adjustColorsDialog.Create((VDGUIHandle)g_hwnd);
			return true;

		case ID_SYSTEM_PAUSEWHENINACTIVE:
			g_pauseInactive = !g_pauseInactive;
			return true;

		case ID_SYSTEM_WARPSPEED:
			g_sim.SetTurboModeEnabled(!g_sim.IsTurboModeEnabled());
			return true;

		case ID_SYSTEM_CPUOPTIONS:
			DialogBox(g_hInst, MAKEINTRESOURCE(IDD_CPU_OPTIONS), g_hwnd, CPUOptionsDlgProc);
			return true;

		case ID_SYSTEM_FPPATCH:
			g_sim.SetFPPatchEnabled(!g_sim.IsFPPatchEnabled());
			return true;

		case ID_SYSTEM_HARDDISK:
			{
				IATHardDiskEmulator *hd = g_sim.GetHardDisk();

				if (hd) {
					ATHardDiskDialog dlg(hd);

					dlg.ShowDialog((VDGUIHandle)g_hwnd);
				}
			}
			return true;

		case ID_VIDEO_STANDARDVIDEO:
			g_enhancedText = 0;
			return true;

		case ID_VIDEO_ENHANCEDTEXT:
			g_enhancedText = 1;
			ATSetFullscreen(false);
			return true;

		case ID_VIDEO_ENHANCEDTEXTFONT:
			OnCommandEnhancedTextFont();
			return true;

		case ID_DISKDRIVE_ENABLED:
			for(int i=0; i<4; ++i) {
				ATDiskEmulator& disk = g_sim.GetDiskDrive(i);
				disk.SetEnabled(!disk.IsEnabled());
			}
			return true;

		case ID_DISKDRIVE_SIOPATCH:
			g_sim.SetDiskSIOPatchEnabled(!g_sim.IsDiskSIOPatchEnabled());
			return true;

		case ID_DISKDRIVE_BURSTDISABLED:
			g_sim.GetPokey().SetSerialBurstMode(ATPokeyEmulator::kSerialBurstMode_Disabled);
			return true;

		case ID_DISKDRIVE_BURSTSTANDARD:
			g_sim.GetPokey().SetSerialBurstMode(ATPokeyEmulator::kSerialBurstMode_Standard);
			return true;

		case ID_DISKDRIVE_BURSTPOLLED:
			g_sim.GetPokey().SetSerialBurstMode(ATPokeyEmulator::kSerialBurstMode_Polled);
			return true;

		case ID_DISKDRIVE_ACCURATESECTORTIMING:
			for(int i=0; i<4; ++i) {
				ATDiskEmulator& disk = g_sim.GetDiskDrive(i);
				disk.SetAccurateSectorTimingEnabled(!disk.IsAccurateSectorTimingEnabled());
			}
			return true;

		case ID_DISKDRIVE_SHOWSECTORCOUNTER:
			g_sim.SetDiskSectorCounterEnabled(!g_sim.IsDiskSectorCounterEnabled());
			return true;

		case ID_AUDIO_STEREO:
			g_sim.SetDualPokeysEnabled(!g_sim.IsDualPokeysEnabled());
			return true;

		case ID_AUDIO_OPTIONS:
			{
				ATAudioOptionsDialog dlg;
				dlg.ShowDialog((VDGUIHandle)g_hwnd);
			}
			return true;

		case ID_AUDIO_NONLINEARMIXING:
			{
				ATPokeyEmulator& pokey = g_sim.GetPokey();

				pokey.SetNonlinearMixingEnabled(!pokey.IsNonlinearMixingEnabled());
			}
			return true;

		case ID_AUDIO_CHANNEL1:
			{
				ATPokeyEmulator& pokey = g_sim.GetPokey();
				pokey.SetChannelEnabled(0, !pokey.IsChannelEnabled(0));
			}
			return true;

		case ID_AUDIO_CHANNEL2:
			{
				ATPokeyEmulator& pokey = g_sim.GetPokey();
				pokey.SetChannelEnabled(1, !pokey.IsChannelEnabled(1));
			}
			return true;

		case ID_AUDIO_CHANNEL3:
			{
				ATPokeyEmulator& pokey = g_sim.GetPokey();
				pokey.SetChannelEnabled(2, !pokey.IsChannelEnabled(2));
			}
			return true;

		case ID_AUDIO_CHANNEL4:
			{
				ATPokeyEmulator& pokey = g_sim.GetPokey();
				pokey.SetChannelEnabled(3, !pokey.IsChannelEnabled(3));
			}
			return true;

		case ID_INPUT_INPUTMAPPINGS:
			ATUIShowDialogInputMappings(g_hwnd, *g_sim.GetInputManager(), &g_sim.GetJoystickManager());
			SaveInputMaps();
			ATReloadPortMenus();
			return true;

		case ID_CHEAT_DISABLEPMCOLLISIONS:
			g_sim.GetGTIA().SetPMCollisionsEnabled(!g_sim.GetGTIA().ArePMCollisionsEnabled());
			return true;

		case ID_CHEAT_DISABLEPFCOLLISIONS:
			g_sim.GetGTIA().SetPFCollisionsEnabled(!g_sim.GetGTIA().ArePFCollisionsEnabled());
			return true;

		case ID_DEBUG_AUTORELOADROMS:
			g_sim.SetROMAutoReloadEnabled(!g_sim.IsROMAutoReloadEnabled());
			return true;

		case ID_DEBUG_AUTOLOADKERNELSYMBOLS:
			g_sim.SetAutoLoadKernelSymbolsEnabled(!g_sim.IsAutoLoadKernelSymbolsEnabled());
			return true;

		case ID_DEBUG_ENABLEDEBUGGER:
			if (ATIsDebugConsoleActive()) {
				if (!g_sim.IsRunning())
					ATGetDebugger()->Detach();

				ATCloseConsole();
			} else
				ATOpenConsole();
			return true;

		case ID_DEBUG_RUN:
			ATGetDebugger()->Run(kATDebugSrcMode_Same);
			return true;

		case ID_DEBUG_BREAK:
			ATOpenConsole();
			ATGetDebugger()->Break();
			return true;

		case ID_DEBUG_STEPINTO:
			ATGetDebugger()->StepInto(kATDebugSrcMode_Same);
			return true;

		case ID_DEBUG_STEPOUT:
			ATGetDebugger()->StepOut(kATDebugSrcMode_Same);
			return true;

		case ID_DEBUG_STEPOVER:
			ATGetDebugger()->StepOver(kATDebugSrcMode_Same);
			return true;

		case ID_PROFILE_PROFILEVIEW:
			ATActivateUIPane(kATUIPaneId_Profiler, true);
			return true;

		case ID_VERIFIER_ENABLED:
			g_sim.SetVerifierEnabled(!g_sim.IsVerifierEnabled());
			return true;

		case ID_RECORD_STOPRECORDING:
			if (g_pAudioWriter) {
				g_sim.GetPokey().SetAudioTap(NULL);
				g_pAudioWriter = NULL;
			}
			return true;

		case ID_RECORD_RECORDRAWAUDIO:
			{
				VDStringW s(VDGetSaveFileName('raud', (VDGUIHandle)g_hwnd, L"Record raw audio", L"Raw 32-bit float data\0*.pcm\0", L"pcm"));

				if (!s.empty()) {
					try {
						g_pAudioWriter = new ATAudioWriter(s.c_str());
						g_sim.GetPokey().SetAudioTap(g_pAudioWriter);
					} catch(const MyError& e) {
						e.post(g_hwnd, "Altirra Error");
					}
				}
			}
			return true;

		case ID_HELP_ABOUT:
			ATAboutDialog().ShowDialog((VDGUIHandle)g_hwnd);
			return true;

		case ID_HELP_CHANGELOG:
			ATShowChangeLog((VDGUIHandle)g_hwnd);
			return true;

		default:
			if ((id - ID_INPUT_PORT1_NONE) < 100) {
				g_portMenus[0].HandleCommand(id);
				return true;
			}

			if ((id - ID_INPUT_PORT2_NONE) < 100) {
				g_portMenus[1].HandleCommand(id);
				return true;
			}

			if ((id - ID_INPUT_PORT3_NONE) < 100) {
				g_portMenus[2].HandleCommand(id);
				return true;
			}

			if ((id - ID_INPUT_PORT4_NONE) < 100) {
				g_portMenus[3].HandleCommand(id);
				return true;
			}

			break;
	}
	return false;
}

void OnInitMenu(HMENU hmenu) {
	VDEnableMenuItemByCommandW32(hmenu, ID_CASSETTE_UNLOAD, g_sim.GetCassette().IsLoaded());
	VDEnableMenuItemByCommandW32(hmenu, ID_FILE_DETACHCARTRIDGE, g_sim.IsCartridgeAttached());

	VDCheckMenuItemByCommandW32(hmenu, ID_SYSTEM_PAL, g_sim.IsPALMode());
	VDCheckMenuItemByCommandW32(hmenu, ID_SYSTEM_PRINTER, g_sim.IsPrinterEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_SYSTEM_RTIME8, g_sim.IsRTime8Enabled());

	VDCheckMenuItemByCommandW32(hmenu, ID_VIEW_SHOWFPS, g_showFps);

	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	ATGTIAEmulator::ArtifactMode artmode = gtia.GetArtifactingMode();
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_VIDEO_NOARTIFACTING, artmode == ATGTIAEmulator::kArtifactNone);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_VIDEO_NTSCARTIFACTING, artmode == ATGTIAEmulator::kArtifactNTSC);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_VIDEO_NTSCARTIFACTINGHI, artmode == ATGTIAEmulator::kArtifactNTSCHi);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_VIDEO_PALARTIFACTING, artmode == ATGTIAEmulator::kArtifactPAL);
	VDCheckMenuItemByCommandW32(hmenu, ID_VIDEO_FRAMEBLENDING, gtia.IsBlendModeEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_VIDEO_INTERLACE, gtia.IsInterlaceEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_VIDEO_VBXE, g_sim.GetVBXE() != NULL);
	VDCheckMenuItemByCommandW32(hmenu, ID_VIDEO_VBXESHAREDMEMORY, g_sim.IsVBXESharedMemoryEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_VIDEO_VBXEALTPAGE, g_sim.IsVBXEAltPageEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_SYSTEM_WARPSPEED, g_sim.IsTurboModeEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_SYSTEM_PAUSEWHENINACTIVE, g_pauseInactive);
	VDCheckMenuItemByCommandW32(hmenu, ID_VIEW_VSYNC, gtia.IsVsyncEnabled());

	if (g_pDisplay) {
		IVDVideoDisplay::FilterMode filterMode = g_pDisplay->GetFilterMode();
		VDCheckRadioMenuItemByCommandW32(hmenu, ID_FILTERMODE_POINT, filterMode == IVDVideoDisplay::kFilterPoint);
		VDCheckRadioMenuItemByCommandW32(hmenu, ID_FILTERMODE_BILINEAR, filterMode == IVDVideoDisplay::kFilterBilinear);
		VDCheckRadioMenuItemByCommandW32(hmenu, ID_FILTERMODE_BICUBIC, filterMode == IVDVideoDisplay::kFilterBicubic);
		VDCheckRadioMenuItemByCommandW32(hmenu, ID_FILTERMODE_ANYSUITABLE, filterMode == IVDVideoDisplay::kFilterAnySuitable);
	}

	VDCheckRadioMenuItemByCommandW32(hmenu, ID_STRETCHMODE_FITTOWINDOW, g_displayStretchMode == kATDisplayStretchMode_Unconstrained);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_STRETCHMODE_PRESERVEASPECTRATIO, g_displayStretchMode == kATDisplayStretchMode_PreserveAspectRatio);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_STRETCHMODE_SQUAREPIXELS, g_displayStretchMode == kATDisplayStretchMode_SquarePixels);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_STRETCHMODE_INTEGRALSQUAREPIXELS, g_displayStretchMode == kATDisplayStretchMode_Integral);

	ATGTIAEmulator::OverscanMode ovmode = gtia.GetOverscanMode();
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_OVERSCAN_NORMAL, ovmode == ATGTIAEmulator::kOverscanNormal);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_OVERSCAN_EXTENDED, ovmode == ATGTIAEmulator::kOverscanExtended);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_OVERSCAN_FULL, ovmode == ATGTIAEmulator::kOverscanFull);

	VDCheckMenuItemByCommandW32(hmenu, ID_OVERSCAN_PALEXTENDED, g_sim.GetGTIA().IsOverscanPALExtended());

	ATHardwareMode hardwareMode = g_sim.GetHardwareMode();
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_HARDWARE_800, hardwareMode == kATHardwareMode_800);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_HARDWARE_800XL, hardwareMode == kATHardwareMode_800XL);

	ATKernelMode kernelMode = g_sim.GetKernelMode();
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FIRMWARE_DEFAULT, kernelMode == kATKernelMode_Default);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FIRMWARE_OSA, kernelMode == kATKernelMode_OSA);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FIRMWARE_OSB, kernelMode == kATKernelMode_OSB);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FIRMWARE_800XL, kernelMode == kATKernelMode_XL);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FIRMWARE_OTHER, kernelMode == kATKernelMode_Other);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FIRMWARE_LLE, kernelMode == kATKernelMode_LLE);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FIRMWARE_HLE, kernelMode == kATKernelMode_HLE);
	VDCheckMenuItemByCommandW32(hmenu, ID_FIRMWARE_BASIC, g_sim.IsBASICEnabled());

	ATMemoryMode memMode = g_sim.GetMemoryMode();
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_48K, memMode == kATMemoryMode_48K);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_52K, memMode == kATMemoryMode_52K);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_64K, memMode == kATMemoryMode_64K);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_128K, memMode == kATMemoryMode_128K);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_320K, memMode == kATMemoryMode_320K);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_576K, memMode == kATMemoryMode_576K);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_1088K, memMode == kATMemoryMode_1088K);

	VDCheckMenuItemByCommandW32(hmenu, ID_MEMORY_RANDOMIZE, g_sim.IsRandomFillEnabled());

	VDCheckMenuItemByCommandW32(hmenu, ID_SYSTEM_FPPATCH, g_sim.IsFPPatchEnabled());

	VDCheckMenuItemByCommandW32(hmenu, ID_CASSETTE_SIOPATCH, g_sim.IsCassetteSIOPatchEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_CASSETTE_AUTOBOOT, g_sim.IsCassetteAutoBootEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_CASSETTE_LOADDATAASAUDIO, g_sim.GetCassette().IsLoadDataAsAudioEnabled());

	VDCheckMenuItemByCommandW32(hmenu, ID_DEBUG_AUTORELOADROMS, g_sim.IsROMAutoReloadEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_DEBUG_AUTOLOADKERNELSYMBOLS, g_sim.IsAutoLoadKernelSymbolsEnabled());

	bool debuggerEnabled = ATIsDebugConsoleActive();
	VDCheckMenuItemByCommandW32(hmenu, ID_DEBUG_ENABLEDEBUGGER, debuggerEnabled);
	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_CALLSTACK, debuggerEnabled);
	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_CONSOLE, debuggerEnabled);
	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_DISASSEMBLY, debuggerEnabled);
	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_HISTORY, debuggerEnabled);
	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_REGISTERS, debuggerEnabled);
	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_MEMORY, debuggerEnabled);

	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_FULLSCREEN, g_enhancedText == 0);
	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_COPYFRAMETOCLIPBOARD, gtia.GetLastFrameBuffer() != NULL);

	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_PASTE, !!IsClipboardFormatAvailable(CF_TEXT));

	VDCheckMenuItemByCommandW32(hmenu, ID_CHEAT_DISABLEPMCOLLISIONS, !gtia.ArePMCollisionsEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_CHEAT_DISABLEPFCOLLISIONS, !gtia.ArePFCollisionsEnabled());

	const bool running = g_sim.IsRunning();
	VDEnableMenuItemByCommandW32(hmenu, ID_DEBUG_RUN, !running);
	VDEnableMenuItemByCommandW32(hmenu, ID_DEBUG_BREAK, running);
	VDEnableMenuItemByCommandW32(hmenu, ID_DEBUG_STEPINTO, !running);
	VDEnableMenuItemByCommandW32(hmenu, ID_DEBUG_STEPOUT, !running);
	VDEnableMenuItemByCommandW32(hmenu, ID_DEBUG_STEPOVER, !running);

	VDCheckMenuItemByCommandW32(hmenu, ID_VERIFIER_ENABLED, g_sim.IsVerifierEnabled());

	ATDiskEmulator& disk = g_sim.GetDiskDrive(0);
	VDCheckMenuItemByCommandW32(hmenu, ID_DISKDRIVE_ENABLED, disk.IsEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_DISKDRIVE_SIOPATCH, g_sim.IsDiskSIOPatchEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_DISKDRIVE_ACCURATESECTORTIMING, disk.IsAccurateSectorTimingEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_DISKDRIVE_SHOWSECTORCOUNTER, g_sim.IsDiskSectorCounterEnabled());

	ATPokeyEmulator& pokey = g_sim.GetPokey();
	ATPokeyEmulator::SerialBurstMode burstMode = pokey.GetSerialBurstMode();
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_DISKDRIVE_BURSTDISABLED, burstMode == ATPokeyEmulator::kSerialBurstMode_Disabled);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_DISKDRIVE_BURSTSTANDARD, burstMode == ATPokeyEmulator::kSerialBurstMode_Standard);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_DISKDRIVE_BURSTPOLLED, burstMode == ATPokeyEmulator::kSerialBurstMode_Polled);

	VDCheckMenuItemByCommandW32(hmenu, ID_AUDIO_STEREO, g_sim.IsDualPokeysEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_AUDIO_NONLINEARMIXING, pokey.IsNonlinearMixingEnabled());

	VDCheckMenuItemByCommandW32(hmenu, ID_AUDIO_CHANNEL1, pokey.IsChannelEnabled(0));
	VDCheckMenuItemByCommandW32(hmenu, ID_AUDIO_CHANNEL2, pokey.IsChannelEnabled(1));
	VDCheckMenuItemByCommandW32(hmenu, ID_AUDIO_CHANNEL3, pokey.IsChannelEnabled(2));
	VDCheckMenuItemByCommandW32(hmenu, ID_AUDIO_CHANNEL4, pokey.IsChannelEnabled(3));

	VDCheckRadioMenuItemByCommandW32(hmenu, ID_VIDEO_STANDARDVIDEO, g_enhancedText == 0);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_VIDEO_ENHANCEDTEXT, g_enhancedText == 1);

	VDEnableMenuItemByCommandW32(hmenu, ID_RECORD_STOPRECORDING, g_pAudioWriter != NULL);
	VDCheckMenuItemByCommandW32(hmenu, ID_RECORD_RECORDRAWAUDIO, g_pAudioWriter != NULL);

	ATUpdatePortMenus();
}

void OnActivateApp(HWND hwnd, WPARAM wParam) {
	g_winActive = (wParam != 0);

	if (!wParam)
		ATSetFullscreen(false);
}

int ATGetInputCodeForRawKey(WPARAM wParam, LPARAM lParam) {
	const bool ext = (lParam & (1 << 24)) != 0;
	int code = LOWORD(wParam);

	switch(code) {
		case VK_RETURN:
			if (ext)
				code = kATInputCode_KeyNumpadEnter;
			break;

		case VK_SHIFT:
			code = ext ? kATInputCode_KeyRShift : kATInputCode_KeyLShift;
			break;

		case VK_CONTROL:
			code = ext ? kATInputCode_KeyRControl : kATInputCode_KeyLControl;
			break;
	}

	return code;
}

bool OnKeyDown(HWND hwnd, WPARAM wParam, LPARAM lParam) {
	ATInputManager *im = g_sim.GetInputManager();

	const int key = LOWORD(wParam);
	const bool alt = (GetKeyState(VK_MENU) < 0);

	if (!alt) {
		int inputCode = ATGetInputCodeForRawKey(wParam, lParam);

		if (im->IsInputMapped(0, inputCode)) {
			im->OnButtonDown(0, inputCode);
			return true;
		}
	}

	const bool isRepeat = (lParam & 0xffff) > 0;
	const bool shift = (GetKeyState(VK_SHIFT) < 0);
	const bool ctrl = (GetKeyState(VK_CONTROL) < 0);
	const uint8 ctrlmod = (ctrl ? 0x80 : 0x00);
	const uint8 shiftmod = (shift ? 0x40 : 0x00);
	const uint8 modifier = ctrlmod + shiftmod;

	switch(key) {
		case VK_CAPITAL:
			// drop injected CAPS LOCK keys
			if (lParam & 0x00ff0000)
				g_sim.GetPokey().PushKey(0x3C + modifier, isRepeat);
			return true;

		case VK_SHIFT:
			g_sim.GetPokey().SetShiftKeyState(true);
			break;

		case VK_F1:
			if (shift) {
				// used to be toggle joystick
			} else if (ctrl) {
				OnCommandFilterModeNext();
			} else if (alt) {
				OnCommand(ID_INPUT_SWAPPORTASSIGNMENTS);
			} else {
				g_sim.SetTurboModeEnabled(true);
			}
			return true;

		case VK_F2:
			if (alt)
				return false;

			g_sim.GetGTIA().SetConsoleSwitch(0x01, true);
			return true;

		case VK_F3:
			if (!alt) {
				g_sim.GetGTIA().SetConsoleSwitch(0x02, true);
				return true;
			}
			break;

		case VK_F4:
			if (!alt) {
				g_sim.GetGTIA().SetConsoleSwitch(0x04, true);
				return true;
			}

			// Note: Particularly important that we don't eat Alt+F4 here.
			break;

		case VK_F5:
			if (!alt && !ctrl) {
				if (shift) {
					g_sim.ColdReset();
					g_sim.Resume();
					return true;
				} else {
					g_sim.WarmReset();
					g_sim.Resume();
					return true;
				}
			}
			break;

		case VK_F6:
			if (!alt) {
				g_sim.GetPokey().PushKey(0x11 | modifier, isRepeat);
				return true;
			}
			break;

		case VK_F7:
			if (ctrl) {
				g_sim.SetPALMode(!g_sim.IsPALMode());
				return true;
			} else if (!shift && !alt) {
				g_sim.GetPokey().PushBreak();
				return true;
			}
			break;

		case VK_F8:
			if (shift) {
				OnCommandAnticVisualizationNext();
			} else if (ctrl) {
				OnCommandGTIAVisualizationNext();
			} else {
				ATOpenConsole();

				if (g_sim.IsRunning())
					ATGetDebugger()->Break();
				else
					ATGetDebugger()->Run(kATDebugSrcMode_Same);
			}
			return true;

		case VK_UP:
			g_sim.GetPokey().PushKey(0x8E + shiftmod, isRepeat);
			return true;

		case VK_DOWN:
			g_sim.GetPokey().PushKey(0x8F + shiftmod, isRepeat);
			return true;

		case VK_LEFT:
			g_sim.GetPokey().PushKey(0x86 + shiftmod, isRepeat);
			return true;

		case VK_RIGHT:
			g_sim.GetPokey().PushKey(0x87 + shiftmod, isRepeat);
			return true;

		case VK_CANCEL:
		case VK_PAUSE:
			if (ctrl) {
				ATOpenConsole();
				ATGetDebugger()->Break();
			} else {
				g_sim.GetPokey().PushBreak();
			}
			return true;

		case 'C':
			if (alt) {
				OnCommand(ID_VIEW_COPYFRAMETOCLIPBOARD);
				return true;
			}

			break;

		case VK_TAB:
			if (!alt) {
				g_sim.GetPokey().PushKey(0x2C | modifier, isRepeat);
				return true;
			}
			break;

		case VK_BACK:
			if (!alt) {
				g_sim.GetPokey().PushKey(0x34 | modifier, isRepeat);
				return true;
			}
			break;

		case VK_RETURN:
			if (alt) {
				OnCommandToggleFullScreen();
			} else {
				g_sim.GetPokey().PushKey(0x0C | modifier, isRepeat);
			}
			return true;

		case VK_ESCAPE:
			if (!alt) {
				g_sim.GetPokey().PushKey(0x1C | modifier, isRepeat);
				return true;
			}
			break;

		case VK_END:	// Fuji
			if (!alt) {
				g_sim.GetPokey().PushKey(0x27 | modifier, isRepeat);
				return true;
			}

		case VK_NEXT:	// Help
			if (!alt) {
				g_sim.GetPokey().PushKey(0x11 | modifier, isRepeat);
				return true;
			}
			break;

		case VK_OEM_1:		// ;: in US
			if (!alt && ctrl) {
				g_sim.GetPokey().PushKey(0x82 | shiftmod, isRepeat);
				return true;
			}

		case VK_OEM_PLUS:	// + any country
			if (!alt && ctrl) {
				g_sim.GetPokey().PushKey(0x86, isRepeat);
				return true;
			}

		case VK_OEM_4:		// [{ in US
			if (!alt && ctrl) {
				g_sim.GetPokey().PushKey(0xE0, isRepeat);	// ignore shift
				return true;
			}

		case VK_OEM_6:		// ]} in US
			if (!alt && ctrl) {
				g_sim.GetPokey().PushKey(0xE2, isRepeat);	// ignore shift
				return true;
			}

		case VK_OEM_COMMA:	// , any country
			if (!alt && ctrl && !shift) {
				g_sim.GetPokey().PushKey(0xA0, isRepeat);
				return true;
			}
			break;

		case VK_OEM_PERIOD:	// . any country
			if (!alt && ctrl && !shift) {
				g_sim.GetPokey().PushKey(0xA2, isRepeat);
				return true;
			}
			break;

		case VK_OEM_2:		// /? in US
			if (!alt && ctrl) {
				g_sim.GetPokey().PushKey(0xA6 | shiftmod, isRepeat);
				return true;
			}
			break;

		case VK_DELETE:
			if (!alt) {
				if (!ctrl && !shift)
					g_sim.GetPokey().PushKey(0x76, isRepeat);
				else
					g_sim.GetPokey().PushKey(0x36 | modifier, isRepeat);
				return true;
			}
			break;

		case VK_INSERT:
			if (!alt) {
				if (!ctrl && !shift)
					g_sim.GetPokey().PushKey(0x77, isRepeat);
				else
					g_sim.GetPokey().PushKey(0x37 | modifier, isRepeat);
				return true;
			}
			break;

		case VK_SPACE:
			if (!alt && (ctrl || shift)) {
				g_sim.GetPokey().PushKey(0x21 | modifier, isRepeat);
				return true;
			}
	}

	if (!alt) {
		// translate ctrl keys
		if (key >= 'A' && key <= 'Z') {
			if (ctrl) {
				static const uint8 kAlphaScanCodeTable[26]={
					/* A */ 0xBF,
					/* B */ 0x95,
					/* C */ 0x92,
					/* D */ 0xBA,
					/* E */ 0xAA,
					/* F */ 0xB8,
					/* G */ 0xBD,
					/* H */ 0xB9,
					/* I */ 0x8D,
					/* J */ 0x81,
					/* K */ 0x85,
					/* L */ 0x80,
					/* M */ 0xA5,
					/* N */ 0xA3,
					/* O */ 0x88,
					/* P */ 0x8A,
					/* Q */ 0xAF,
					/* R */ 0xA8,
					/* S */ 0xBE,
					/* T */ 0xAD,
					/* U */ 0x8B,	
					/* V */ 0x90,
					/* W */ 0xAE,
					/* X */ 0x96,
					/* Y */ 0xAB,
					/* Z */ 0x97,
				};

				uint8 scanCode = kAlphaScanCodeTable[key - 'A'];

				if (shift)
					scanCode |= 0x40;

				g_sim.GetPokey().PushKey(scanCode, isRepeat);
				return true;
			}
		}

		if (key >= '0' && key <= '9') {
			if (ctrl) {
				static const uint8 kNumberScanCodeTable[10]={
					/* 0 */ 0xB2,
					/* 1 */ 0x9F,
					/* 2 */ 0x9E,
					/* 3 */ 0x9A,
					/* 4 */ 0x98,
					/* 5 */ 0x9D,
					/* 6 */ 0x9B,
					/* 7 */ 0xB3,
					/* 8 */ 0xB5,
					/* 9 */ 0xB0,
				};

				uint8 scanCode = kNumberScanCodeTable[key - '0'];

				if (shift)
					scanCode |= 0x40;

				g_sim.GetPokey().PushKey(scanCode, isRepeat);
				return true;
			}
		}
	}

	return false;
}

bool OnKeyUp(HWND hwnd, WPARAM wParam, LPARAM lParam) {
	ATInputManager *im = g_sim.GetInputManager();

	const int key = LOWORD(wParam);
	const bool alt = (GetKeyState(VK_MENU) < 0);

	if (!alt) {
		int inputCode = ATGetInputCodeForRawKey(wParam, lParam);

		if (im->IsInputMapped(0, inputCode)) {
			im->OnButtonUp(0, inputCode);
			return true;
		}
	}

	switch(key) {
		case VK_CAPITAL:
			if (GetKeyState(VK_CAPITAL) & 1) {
				// force caps lock back off
				keybd_event(VK_CAPITAL, 0, 0, NULL);
			}
			return true;

		case VK_SHIFT:
			g_sim.GetPokey().SetShiftKeyState(false);
			break;

		case VK_F2:
			if (!alt) {
				g_sim.GetGTIA().SetConsoleSwitch(0x01, false);
				return true;
			}
			break;

		case VK_F3:
			if (!alt) {
				g_sim.GetGTIA().SetConsoleSwitch(0x02, false);
				return true;
			}
			break;

		case VK_F4:
			if (!alt) {
				g_sim.GetGTIA().SetConsoleSwitch(0x04, false);
				return true;
			}
			break;

		case VK_F1:
			g_sim.SetTurboModeEnabled(false);
			return true;
	}

	return false;
}

void ProcessKey(char c, bool repeat, bool allowQueue) {
	uint8 ch;
	switch(c) {

	case 'l': ch = 0x00; break;
	case 'L': ch = 0x40; break;

	case 'j': ch = 0x01; break;
	case 'J': ch = 0x41; break;

	case ';': ch = 0x02; break;
	case ':': ch = 0x42; break;

	case 'k': ch = 0x05; break;
	case 'K': ch = 0x45; break;

	case '+': ch = 0x06; break;
	case '\\':ch = 0x46; break;

	case '*': ch = 0x07; break;
	case '^': ch = 0x47; break;

	case 'o': ch = 0x08; break;
	case 'O': ch = 0x48; break;

	case 'p': ch = 0x0A; break;
	case 'P': ch = 0x4A; break;

	case 'u': ch = 0x0B; break;
	case 'U': ch = 0x4B; break;

	case 'i': ch = 0x0D; break;
	case 'I': ch = 0x4D; break;

	case '-': ch = 0x0E; break;
	case '_': ch = 0x4E; break;

	case '=': ch = 0x0F; break;
	case '|': ch = 0x4F; break;

	case 'v': ch = 0x10; break;
	case 'V': ch = 0x50; break;

	case 'c': ch = 0x12; break;
	case 'C': ch = 0x52; break;

	case 'b': ch = 0x15; break;
	case 'B': ch = 0x55; break;

	case 'x': ch = 0x16; break;
	case 'X': ch = 0x56; break;

	case 'z': ch = 0x17; break;
	case 'Z': ch = 0x57; break;

	case '4': ch = 0x18; break;
	case '$': ch = 0x58; break;

	case '3': ch = 0x1A; break;
	case '#': ch = 0x5A; break;

	case '6': ch = 0x1B; break;
	case '&': ch = 0x5B; break;

	case '5': ch = 0x1D; break;
	case '%': ch = 0x5D; break;

	case '2': ch = 0x1E; break;
	case '"': ch = 0x5E; break;

	case '1': ch = 0x1F; break;
	case '!': ch = 0x5F; break;

	case ',': ch = 0x20; break;
	case '[': ch = 0x60; break;

	case ' ': ch = 0x21; break;

	case '.': ch = 0x22; break;
	case ']': ch = 0x62; break;
	case '`': ch = 0xa2; break;

	case 'n': ch = 0x23; break;
	case 'N': ch = 0x63; break;

	case 'm': ch = 0x25; break;
	case 'M': ch = 0x65; break;

	case '/': ch = 0x26; break;
	case '?': ch = 0x66; break;

	case 'r': ch = 0x28; break;
	case 'R': ch = 0x68; break;

	case 'e': ch = 0x2A; break;
	case 'E': ch = 0x6A; break;

	case 'y': ch = 0x2B; break;
	case 'Y': ch = 0x6B; break;

	case 't': ch = 0x2D; break;
	case 'T': ch = 0x6D; break;

	case 'w': ch = 0x2E; break;
	case 'W': ch = 0x6E; break;

	case 'q': ch = 0x2F; break;
	case 'Q': ch = 0x6F; break;

	case '9': ch = 0x30; break;
	case '(': ch = 0x70; break;

	case '0': ch = 0x32; break;
	case ')': ch = 0x72; break;

	case '7': ch = 0x33; break;
	case '\'':ch = 0x73; break;

	case '8': ch = 0x35; break;
	case '@': ch = 0x75; break;

	case '<': ch = 0x36; break;
	case '>': ch = 0x37; break;

	case 'f': ch = 0x38; break;
	case 'F': ch = 0x78; break;

	case 'h': ch = 0x39; break;
	case 'H': ch = 0x79; break;

	case 'd': ch = 0x3A; break;
	case 'D': ch = 0x7A; break;

	case 'g': ch = 0x3D; break;
	case 'G': ch = 0x7D; break;

	case 's': ch = 0x3E; break;
	case 'S': ch = 0x7E; break;

	case 'a': ch = 0x3F; break;
	case 'A': ch = 0x7F; break;

	default:
		return;
	}

	g_sim.GetPokey().PushKey(ch, repeat, allowQueue);
}

void OnChar(HWND hwnd, WPARAM wParam, LPARAM lParam) {
	int code = LOWORD(wParam);
	if (code <= 0 || code > 127)
		return;

	ProcessKey((char)code, (lParam & 0x40000000) != 0, false);
}

///////////////////////////////////////////////////////////////////////////

class ATDisplayPane : public ATDisplayBasePane {
public:
	ATDisplayPane();
	~ATDisplayPane();

	void OnSize();

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	bool OnCreate();
	void OnDestroy();
	void WarpCapturedMouse();
	void UpdateTextDisplay(bool enabled);
	void RepaintTextDisplay(HDC hdc, const bool *lineRedrawFlags);
	void UpdateTextModeFont();

	HWND	mhwndDisplay;
	IVDVideoDisplay *mpDisplay;
	int		mLastHoverX;
	int		mLastHoverY;
	int		mLastTrackMouseX;
	int		mLastTrackMouseY;

	bool	mbTextModeEnabled;
	HFONT	mTextModeFont;
	HFONT	mTextModeWideFont;
	int		mTextCharW;
	int		mTextCharH;

	uint32	mTextLastForeColor;
	uint32	mTextLastBackColor;
	uint32	mTextLastBorderColor;
	int		mTextLastTotalHeight;
	int		mTextLastLineCount;
	uint8	mTextLastData[30][40];
};

ATDisplayPane::ATDisplayPane()
	: mhwndDisplay(NULL)
	, mpDisplay(NULL)
	, mLastHoverX(INT_MIN)
	, mLastHoverY(INT_MIN)
	, mLastTrackMouseX(0)
	, mLastTrackMouseY(0)
	, mbTextModeEnabled(false)
	, mTextModeFont((HFONT)GetStockObject(DEFAULT_GUI_FONT))
	, mTextCharW(16)
	, mTextCharH(16)
	, mTextLastForeColor(0)
	, mTextLastBackColor(0)
	, mTextLastBorderColor(0)
	, mTextLastTotalHeight(-1)
{
	mPreferredDockCode = kATContainerDockCenter;

	memset(mTextLastData, 0, sizeof mTextLastData);
}

ATDisplayPane::~ATDisplayPane() {
}

LRESULT ATDisplayPane::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_CREATE:
			UpdateTextModeFont();
			break;

		case WM_DESTROY:
			g_adjustColorsDialog.Destroy();
			ATShutdownPortMenus();
			if (mTextModeFont) {
				DeleteObject(mTextModeFont);
				mTextModeFont = NULL;
			}
			break;

		case MYWM_PRESYSKEYDOWN:
			// Right-Alt kills capture.
			if (wParam == VK_MENU && (lParam & (1 << 24))) {
				if (::GetCapture() == mhwnd) {
					::ReleaseCapture();
					return true;
				}
			}
			// fall through
		case MYWM_PREKEYDOWN:
			if (OnKeyDown(mhwnd, wParam, lParam))
				return true;
			return false;

		case MYWM_PRESYSKEYUP:
		case MYWM_PREKEYUP:
			if (OnKeyUp(mhwnd, wParam, lParam))
				return true;
			return false;

		case WM_CHAR:
			OnChar(mhwnd, wParam, lParam);
			return 0;

		case WM_PARENTNOTIFY:
			switch(LOWORD(wParam)) {
			case WM_LBUTTONDOWN:
			case WM_MBUTTONDOWN:
			case WM_RBUTTONDOWN:
				SetFocus(mhwnd);
				break;
			}
			break;

		case WM_LBUTTONUP:
			if (::GetCapture() == mhwnd) {
				ATInputManager *im = g_sim.GetInputManager();

				if (im->IsMouseMapped())
					im->OnButtonUp(0, kATInputCode_MouseLMB);
			}
			break;

		case WM_MBUTTONUP:
			if (::GetCapture() == mhwnd) {
				ATInputManager *im = g_sim.GetInputManager();

				if (im->IsMouseMapped())
					im->OnButtonUp(0, kATInputCode_MouseMMB);
			}
			break;

		case WM_RBUTTONUP:
			if (::GetCapture() == mhwnd) {
				ATInputManager *im = g_sim.GetInputManager();

				if (im->IsMouseMapped())
					im->OnButtonUp(0, kATInputCode_MouseRMB);
			}
			break;

		case WM_XBUTTONUP:
			if (::GetCapture() == mhwnd) {
				ATInputManager *im = g_sim.GetInputManager();

				if (im->IsMouseMapped()) {
					if (HIWORD(wParam) == XBUTTON1)
						im->OnButtonUp(0, kATInputCode_MouseX1B);
					else if (HIWORD(wParam) == XBUTTON2)
						im->OnButtonUp(0, kATInputCode_MouseX2B);
				}
			}
			break;

		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONDBLCLK:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONDBLCLK:
		case WM_XBUTTONDOWN:
		case WM_XBUTTONDBLCLK:
			::SetFocus(mhwnd);

			{
				ATInputManager *im = g_sim.GetInputManager();

				if (im->IsMouseMapped()) {
					if (::GetCapture() == mhwnd) {
						switch(msg) {
							case WM_LBUTTONDOWN:
							case WM_LBUTTONDBLCLK:
								im->OnButtonDown(0, kATInputCode_MouseLMB);
								break;
							case WM_MBUTTONDOWN:
							case WM_MBUTTONDBLCLK:
								im->OnButtonDown(0, kATInputCode_MouseMMB);
								break;
							case WM_RBUTTONDOWN:
							case WM_RBUTTONDBLCLK:
								im->OnButtonDown(0, kATInputCode_MouseRMB);
								break;
							case WM_XBUTTONDOWN:
							case WM_XBUTTONDBLCLK:
								if (HIWORD(wParam) == XBUTTON1)
									im->OnButtonDown(0, kATInputCode_MouseX1B);
								else if (HIWORD(wParam) == XBUTTON2)
									im->OnButtonDown(0, kATInputCode_MouseX2B);
								break;
						}
					} else if (g_sim.IsRunning()) {
						::SetCapture(mhwnd);
						::SetCursor(NULL);

						g_mouseCaptured = true;

						DWORD pos = ::GetMessagePos();

						mLastTrackMouseX = (short)LOWORD(pos);
						mLastTrackMouseY = (short)HIWORD(pos);

						WarpCapturedMouse();
					}
				}
			}
			break;

		case WM_SETCURSOR:
			if (g_fullscreen) {
				SetCursor(NULL);
				return TRUE;
			} else if (::GetCapture() == mhwnd) {
				::SetCursor(NULL);
				return TRUE;
			} else {
				DWORD pos = ::GetMessagePos();
				int x = (short)LOWORD(pos);
				int y = (short)HIWORD(pos);

				if (x == mLastHoverX || y == mLastHoverY) {
					::SetCursor(NULL);
					return TRUE;
				}

				if (!g_sim.IsRunning()) {
					::SetCursor(::LoadCursor(NULL, IDC_NO));
					return TRUE;
				}
			}
			break;

		case WM_MOUSEMOVE:
			{
				int x = (short)LOWORD(lParam);
				int y = (short)HIWORD(lParam);

				if (::GetCapture() == mhwnd) {
					int dx = x - mLastTrackMouseX;
					int dy = y - mLastTrackMouseY;

					if (dx | dy) {
						ATInputManager *im = g_sim.GetInputManager();

						im->OnMouseMove(0, dx, dy);

						WarpCapturedMouse();
					}
				} else {
					if (x != mLastHoverX || y != mLastHoverY) {
						mLastHoverX = INT_MIN;
						mLastHoverY = INT_MIN;
					}

					if (g_sim.IsRunning()) {
						TRACKMOUSEEVENT tme = {sizeof(TRACKMOUSEEVENT)};
						tme.dwFlags = TME_HOVER;
						tme.hwndTrack = mhwnd;
						tme.dwHoverTime = HOVER_DEFAULT;

						TrackMouseEvent(&tme);
					}
				}
			}
			break;

		case WM_MOUSEHOVER:
			{
				DWORD pos = ::GetMessagePos();
				mLastHoverX = (short)LOWORD(pos);
				mLastHoverY = (short)HIWORD(pos);

				::SetCursor(NULL);
			}
			return 0;

		case WM_KILLFOCUS:
			if (::GetCapture() == mhwnd)
				::ReleaseCapture();
			break;

		case WM_CAPTURECHANGED:
			g_mouseCaptured = false;
			break;

		case WM_ERASEBKGND:
			return 0;

		case WM_PAINT:
			if (mbTextModeEnabled) {
				PAINTSTRUCT ps;
				if (HDC hdc = BeginPaint(mhwnd, &ps)) {
					RepaintTextDisplay(hdc, NULL);
					EndPaint(mhwnd, &ps);
				}
				return 0;
			}
			break;
	}

	return ATUIPane::WndProc(msg, wParam, lParam);
}

bool ATDisplayPane::OnCreate() {
	if (!ATUIPane::OnCreate())
		return false;

	mhwndDisplay = (HWND)VDCreateDisplayWindowW32(WS_EX_NOPARENTNOTIFY, WS_CHILD|WS_VISIBLE, 0, 0, 0, 0, (VDGUIHandle)mhwnd);
	if (!mhwndDisplay)
		return false;

	g_hwndDisplay = mhwndDisplay;

	mpDisplay = VDGetIVideoDisplay((VDGUIHandle)mhwndDisplay);
	g_pDisplay = mpDisplay;

	mpDisplay->SetReturnFocus(true);
	mpDisplay->SetFilterMode(g_dispFilterMode);
	mpDisplay->SetAccelerationMode(IVDVideoDisplay::kAccelResetInForeground);
	g_sim.GetGTIA().SetVideoOutput(mpDisplay);
	return true;
}

void ATDisplayPane::OnDestroy() {
	if (mpDisplay) {
		g_sim.GetGTIA().SetVideoOutput(NULL);

		mpDisplay->Destroy();
		mpDisplay = NULL;
		g_pDisplay = NULL;
		mhwndDisplay = NULL;
		g_hwndDisplay = NULL;
	}

	ATUIPane::OnDestroy();
}

void ATDisplayPane::OnSize() {
	if (mhwndDisplay) {
		RECT r;
		GetClientRect(mhwnd, &r);

		if (mpDisplay) {
			vdrect32 rd(r.left, r.top, r.right, r.bottom);
			sint32 w = rd.width();
			sint32 h = rd.height();
			int sw;
			int sh;

			g_sim.GetGTIA().GetFrameSize(sw, sh);

			if (w && h) {
				if (g_displayStretchMode == kATDisplayStretchMode_PreserveAspectRatio) {
					const float kRawFrameAspect = (float)sw / (float)sh;
					const float kDesiredAspect = (g_sim.IsPALMode() ? 1.03964f : 0.857141f) * kRawFrameAspect;
					float aspect = (float)w / (float)h;

					if (aspect > kDesiredAspect) {
						sint32 w2 = (sint32)(0.5f + h * kDesiredAspect);

						rd.left = (w - w2) >> 1;
						rd.right = rd.left + w2;
					} else {
						sint32 h2 = (sint32)(0.5f + w / kDesiredAspect);

						rd.top = (h - h2) >> 1;
						rd.bottom = rd.top + h2;
					}
				} else if (g_displayStretchMode == kATDisplayStretchMode_SquarePixels || g_displayStretchMode == kATDisplayStretchMode_Integral) {
					int ratio = std::min<int>(w / sw, h / sh);

					if (ratio < 1 || g_displayStretchMode == kATDisplayStretchMode_SquarePixels) {
						if (w*sh < h*sw) {		// (w / sw) < (h / sh) -> w*sh < h*sw
							// width is smaller ratio -- compute restricted height
							int restrictedHeight = (sh * w + (sw >> 1)) / sw;

							rd.top = (h - restrictedHeight) >> 1;
							rd.bottom = rd.top + restrictedHeight;
						} else {
							// height is smaller ratio -- compute restricted width
							int restrictedWidth = (sw * h + (sh >> 1)) / sh;

							rd.left = (w - restrictedWidth) >> 1;
							rd.right = rd.left+ restrictedWidth;
						}
					} else {
						int finalWidth = sw * ratio;
						int finalHeight = sh * ratio;

						rd.left = (w - finalWidth) >> 1;
						rd.right = rd.left + finalWidth;

						rd.top = (h - finalHeight) >> 1;
						rd.bottom = rd.top + finalHeight;
					}
				}
			}

			mpDisplay->SetDestRect(&rd, 0);
		}

		SetWindowPos(mhwndDisplay, NULL, 0, 0, r.right, r.bottom, SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
	}

	if (mbTextModeEnabled)
		InvalidateRect(mhwnd, NULL, TRUE);
}

void ATDisplayPane::WarpCapturedMouse() {
	RECT r;

	if (::GetClientRect(mhwnd, &r)) {
		mLastTrackMouseX = r.right >> 1;
		mLastTrackMouseY = r.bottom >> 1;
		POINT pt = {mLastTrackMouseX, mLastTrackMouseY};
		::ClientToScreen(mhwnd, &pt);
		::SetCursorPos(pt.x, pt.y);
	}
}

void ATDisplayPane::UpdateTextDisplay(bool enabled) {
	if (!enabled) {
		if (mbTextModeEnabled) {
			mbTextModeEnabled = false;

			::ShowWindow(mhwndDisplay, SW_SHOWNOACTIVATE);
		}

		return;
	}

	bool forceInvalidate = false;
	if (!mbTextModeEnabled) {
		mbTextModeEnabled = true;
		forceInvalidate = true;

		::ShowWindow(mhwndDisplay, SW_HIDE);
	}

	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	COLORREF colorBack = VDSwizzleU32(gtia.GetPlayfieldColor24(2)) >> 8;
	COLORREF colorFore = VDSwizzleU32(gtia.GetPlayfieldColorPF2H()) >> 8;
	COLORREF colorBorder = VDSwizzleU32(gtia.GetBackgroundColor24()) >> 8;

	if (mTextLastBackColor != colorBack) {
		mTextLastBackColor = colorBack;
		forceInvalidate = true;
	}

	if (mTextLastForeColor != colorFore) {
		mTextLastForeColor = colorFore;
		forceInvalidate = true;
	}

	if (mTextLastBorderColor != colorBorder) {
		mTextLastBorderColor = colorBorder;
		forceInvalidate = true;
	}

	// update data from ANTIC
	ATAnticEmulator& antic = g_sim.GetAntic();
	const ATAnticEmulator::DLHistoryEntry *history = g_sim.GetAntic().GetDLHistory();

	int line = 0;
	bool redrawFlags[30] = {false};
	bool linesDirty = false;
	for(int y=8; y<240; ++y) {
		const ATAnticEmulator::DLHistoryEntry& hval = history[y];

		if (!hval.mbValid || (hval.mControl & 0x0F) != 2)
			continue;

		uint32 pfAddr = hval.mPFAddress;

		uint8 *lastData = mTextLastData[line];
		uint8 data[40];
		for(int i=0; i<40; ++i) {
			uint8 c = g_sim.DebugAnticReadByte(pfAddr + i);

			data[i] = c;
		}

		if (forceInvalidate || line >= mTextLastLineCount || memcmp(data, lastData, 40)) {
			memcpy(lastData, data, 40);
			redrawFlags[line] = true;
			linesDirty = true;
		}

		++line;
	}

	mTextLastLineCount = line;

	if (forceInvalidate || linesDirty) {
		if (HDC hdc = GetDC(mhwnd)) {
			RepaintTextDisplay(hdc, forceInvalidate ? NULL : redrawFlags);
			ReleaseDC(mhwnd, hdc);
		}
	}
}

void ATDisplayPane::RepaintTextDisplay(HDC hdc, const bool *lineRedrawFlags) {
	int saveHandle = SaveDC(hdc);

	if (!saveHandle)
		return;

	const COLORREF colorBack = mTextLastBackColor;
	const COLORREF colorFore = mTextLastForeColor;
	const COLORREF colorBorder = mTextLastBorderColor;

	SelectObject(hdc, mTextModeFont);
	SetTextAlign(hdc, TA_TOP | TA_LEFT);
	SetBkMode(hdc, OPAQUE);

	RECT rClient;
	GetClientRect(mhwnd, &rClient);

	int py = 0;
	for(int line = 0; line < mTextLastLineCount; ++line) {
		uint8 *data = mTextLastData[line];

		static const uint8 kInternalToATASCIIXorTab[4]={
			0x20, 0x60, 0x40, 0x00
		};

		if (!lineRedrawFlags || lineRedrawFlags[line]) {
			char buf[41];
			bool inverted[41];
			for(int i=0; i<40; ++i) {
				uint8 c = data[i];

				c ^= kInternalToATASCIIXorTab[(c >> 5) & 3];

				if ((uint8)((c & 0x7f) - 0x20) >= 0x5f)
					c = (c & 0x80) + 0x20;

				buf[i] = c & 0x7f;
				inverted[i] = (c & 0x80) != 0;
			}

			buf[40] = 0;
			inverted[40] = !inverted[39];

			int x = 0;
			while(x < 40) {
				bool invertSpan = inverted[x];
				int xe = x + 1;

				while(inverted[xe] == invertSpan)
					++xe;

				if (invertSpan) {
					SetTextColor(hdc, colorBack);
					SetBkColor(hdc, colorFore);
				} else {
					SetTextColor(hdc, colorFore);
					SetBkColor(hdc, colorBack);
				}

				TextOutA(hdc, mTextCharW * x, py, buf + x, xe - x);

				x = xe;
			}

			RECT rClear = { mTextCharW * x, py, rClient.right, py + mTextCharH };
			SetBkColor(hdc, colorBorder);
			ExtTextOutW(hdc, 0, py, ETO_OPAQUE, &rClear, L"", 0, NULL);
		}

		py += mTextCharH;
	}

	if (mTextLastTotalHeight != py || !lineRedrawFlags) {
		mTextLastTotalHeight = py;

		RECT rClear = { 0, py, rClient.right, rClient.bottom };
		SetBkColor(hdc, colorBorder);
		ExtTextOutW(hdc, 0, py, ETO_OPAQUE, &rClear, L"", 0, NULL);
	}

	RestoreDC(hdc, saveHandle);
}

void ATDisplayPane::UpdateTextModeFont() {
	if (mTextModeFont)
		DeleteObject(mTextModeFont);

	mTextModeFont = CreateFontIndirectW(&g_enhancedTextFont);
	if (!mTextModeFont)
		mTextModeFont = CreateFontW(16, 0, 0, 0, 0, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FF_DONTCARE | DEFAULT_PITCH, L"Lucida Console");

	mTextCharW = 16;
	mTextCharH = 16;
	if (HDC hdc = GetDC(mhwnd)) {
		HGDIOBJ hOldFont = SelectObject(hdc, mTextModeFont);
		if (hOldFont) {
			TEXTMETRICW tm;
			if (GetTextMetricsW(hdc, &tm)) {
				mTextCharW = tm.tmAveCharWidth;
				mTextCharH = tm.tmHeight;
			}

			SelectObject(hdc, hOldFont);
		}
		ReleaseDC(mhwnd, hdc);
	}

	InvalidateRect(mhwnd, NULL, TRUE);
}

///////////////////////////////////////////////////////////////////////////

class ATMainWindow : public ATContainerWindow {
public:
	ATMainWindow();
	~ATMainWindow();

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	void OnCopyData(HWND hwndReply, const COPYDATASTRUCT& cds);
	void OnDropFiles(HDROP hdrop);
};

ATMainWindow::ATMainWindow() {
}

ATMainWindow::~ATMainWindow() {
}

LRESULT ATMainWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	__try {
		switch(msg) {
			case WM_CREATE:
				if (ATContainerWindow::WndProc(msg, wParam, lParam) < 0)
					return -1;

				ATActivateUIPane(kATUIPaneId_Display, true);
				DragAcceptFiles(mhwnd, TRUE);
				return 0;

			case WM_CLOSE:
				ATSavePaneLayout(NULL);
				ATUISaveWindowPlacement(mhwnd, "Main window");
				break;

			case WM_DESTROY:
				DragAcceptFiles(mhwnd, FALSE);
				PostQuitMessage(0);
				break;

			case WM_ACTIVATEAPP:
				OnActivateApp(mhwnd, wParam);
				break;

			case WM_COMMAND:
				if (OnCommand(LOWORD(wParam)))
					return 0;
				break;

			case WM_INITMENU:
				OnInitMenu((HMENU)wParam);
				return 0;

			case WM_SETCURSOR:
				if (g_fullscreen) {
					SetCursor(NULL);
					return TRUE;
				}
				break;

			case WM_APP + 100:
				{
					MSG& globalMsg = *(MSG *)lParam;

					if (g_hAccel && TranslateAccelerator(mhwnd, g_hAccel, &globalMsg))
						return TRUE;
				}
				break;

			case WM_COPYDATA:
				{
					HWND hwndReply = (HWND)wParam;
					COPYDATASTRUCT *cds = (COPYDATASTRUCT *)lParam;

					OnCopyData(hwndReply, *cds);
				}
				return TRUE;

			case WM_DROPFILES:
				OnDropFiles((HDROP)wParam);
				return 0;

			case WM_DEVICECHANGE:
				g_sim.GetJoystickManager().RescanForDevices();
				break;
		}
	} __except(ATExceptionFilter(GetExceptionCode(), GetExceptionInformation())) {
	}

	return ATContainerWindow::WndProc(msg, wParam, lParam);
}

void ATMainWindow::OnCopyData(HWND hwndReply, const COPYDATASTRUCT& cds) {
	if (cds.dwData != 0xA7000000 || !cds.cbData || !cds.lpData)
		return;

	vdfastvector<wchar_t> s;

	s.resize(cds.cbData / sizeof(wchar_t) + 1, 0);
	memcpy(s.data(), cds.lpData, (s.size() - 1) * sizeof(wchar_t));

	uint32 rval = 0;
	MyError e;

	try {
		rval = ATExecuteAutotestCommand(s.data(), NULL);
	} catch(MyError& f) {
		e.swap(f);
	}

	if (hwndReply) {
		VDStringW err(VDTextAToW(e.gets()));

		vdfastvector<char> buf(sizeof(uint32) + sizeof(wchar_t) * err.size());
		memcpy(buf.data(), &rval, sizeof(uint32));
		memcpy(buf.data() + sizeof(uint32), err.data(), err.size() * sizeof(wchar_t));

		COPYDATASTRUCT cds2;
		cds2.dwData = 0xA7000001;
		cds2.cbData = buf.size();
		cds2.lpData = buf.data();

		::SendMessage(hwndReply, WM_COPYDATA, (WPARAM)mhwnd, (LPARAM)&cds2);
	}
}

void ATMainWindow::OnDropFiles(HDROP hdrop) {
	UINT count = DragQueryFileW(hdrop, 0xFFFFFFFF, NULL, 0);

	if (count) {
		UINT len = DragQueryFileW(hdrop, 0, NULL, 0);

		vdfastvector<wchar_t> buf(len+1, 0);
		if (DragQueryFileW(hdrop, 0, buf.data(), len+1)) {
			try {
				g_sim.Load(buf.data());
			} catch(const MyError& e) {
				e.post(g_hwnd, "Altirra Error");
			}
		}
	}

	DragFinish(hdrop);
}

class D3D9Lock : public VDD3D9Client {
public:
	D3D9Lock() : mpMgr(NULL) {}

	void Lock() {
		if (!mpMgr)
			mpMgr = VDInitDirect3D9(this);
	}

	void Unlock() {
		if (mpMgr) {
			VDDeinitDirect3D9(mpMgr, this);
			mpMgr = NULL;
		}
	}

	void OnPreDeviceReset() {}
	void OnPostDeviceReset() {}
private:
	VDD3D9Manager *mpMgr;
} g_d3d9Lock;

void ReadCommandLine(HWND hwnd, VDCommandLine& cmdLine) {
	bool coldReset = false;
	bool debugMode = false;

	try {
		ATDiskEmulator& disk = g_sim.GetDiskDrive(0);

		if (cmdLine.FindAndRemoveSwitch(L"ntsc"))
			g_sim.SetPALMode(false);

		if (cmdLine.FindAndRemoveSwitch(L"pal"))
			g_sim.SetPALMode(true);

		if (cmdLine.FindAndRemoveSwitch(L"burstio"))
			g_sim.GetPokey().SetSerialBurstMode(ATPokeyEmulator::kSerialBurstMode_Standard);
		if (cmdLine.FindAndRemoveSwitch(L"burstiopolled"))
			g_sim.GetPokey().SetSerialBurstMode(ATPokeyEmulator::kSerialBurstMode_Polled);
		else if (cmdLine.FindAndRemoveSwitch(L"noburstio"))
			g_sim.GetPokey().SetSerialBurstMode(ATPokeyEmulator::kSerialBurstMode_Disabled);

		if (cmdLine.FindAndRemoveSwitch(L"siopatch")) {
			g_sim.SetDiskSIOPatchEnabled(true);
			g_sim.SetCassetteSIOPatchEnabled(true);
		} else if (cmdLine.FindAndRemoveSwitch(L"nosiopatch")) {
			g_sim.SetDiskSIOPatchEnabled(false);
			g_sim.SetCassetteSIOPatchEnabled(false);
		}

		if (cmdLine.FindAndRemoveSwitch(L"casautoboot")) {
			g_sim.SetCassetteAutoBootEnabled(true);
		} else if (cmdLine.FindAndRemoveSwitch(L"nocasautoboot")) {
			g_sim.SetCassetteAutoBootEnabled(false);
		}

		if (cmdLine.FindAndRemoveSwitch(L"accuratedisk"))
			disk.SetAccurateSectorTimingEnabled(true);
		else if (cmdLine.FindAndRemoveSwitch(L"noaccuratedisk"))
			disk.SetAccurateSectorTimingEnabled(false);

		if (cmdLine.FindAndRemoveSwitch(L"stereo")) {
			g_sim.SetDualPokeysEnabled(true);
			coldReset = true;
		} else if (cmdLine.FindAndRemoveSwitch(L"nostereo")) {
			g_sim.SetDualPokeysEnabled(false);
			coldReset = true;
		}

		if (cmdLine.FindAndRemoveSwitch(L"basic")) {
			g_sim.SetBASICEnabled(true);
			coldReset = true;
		} else if (cmdLine.FindAndRemoveSwitch(L"nobasic")) {
			g_sim.SetBASICEnabled(false);
			coldReset = true;
		}

		if (cmdLine.FindAndRemoveSwitch(L"vbxe")) {
			g_sim.SetVBXEEnabled(true);
			coldReset = true;
		} else if (cmdLine.FindAndRemoveSwitch(L"novbxe")) {
			g_sim.SetVBXEEnabled(false);
			coldReset = true;
		}

		if (cmdLine.FindAndRemoveSwitch(L"vbxeshared")) {
			g_sim.SetVBXEEnabled(true);
			coldReset = true;
		} else if (cmdLine.FindAndRemoveSwitch(L"novbxeshared")) {
			g_sim.SetVBXEEnabled(false);
			coldReset = true;
		}

		if (cmdLine.FindAndRemoveSwitch(L"vbxealtpage")) {
			g_sim.SetVBXEAltPageEnabled(true);
			coldReset = true;
		} else if (cmdLine.FindAndRemoveSwitch(L"novbxealtpage")) {
			g_sim.SetVBXEAltPageEnabled(false);
			coldReset = true;
		}

		const wchar_t *arg;
		if (cmdLine.FindAndRemoveSwitch(L"hardware", arg)) {
			if (!_wcsicmp(arg, L"800"))
				g_sim.SetHardwareMode(kATHardwareMode_800);
			else if (!_wcsicmp(arg, L"800xl"))
				g_sim.SetHardwareMode(kATHardwareMode_800XL);
			else
				throw MyError("Command line error: Invalid hardware mode '%ls'", arg);
		}

		if (cmdLine.FindAndRemoveSwitch(L"kernel", arg)) {
			if (!_wcsicmp(arg, L"default"))
				g_sim.SetKernelMode(kATKernelMode_Default);
			else if (!_wcsicmp(arg, L"osb"))
				g_sim.SetKernelMode(kATKernelMode_OSB);
			else if (!_wcsicmp(arg, L"xl"))
				g_sim.SetKernelMode(kATKernelMode_XL);
			else if (!_wcsicmp(arg, L"lle"))
				g_sim.SetKernelMode(kATKernelMode_LLE);
			else if (!_wcsicmp(arg, L"hle"))
				g_sim.SetKernelMode(kATKernelMode_HLE);
			else if (!_wcsicmp(arg, L"other"))
				g_sim.SetKernelMode(kATKernelMode_Other);
			else
				throw MyError("Command line error: Invalid kernel mode '%ls'", arg);
		}

		if (cmdLine.FindAndRemoveSwitch(L"memsize", arg)) {
			if (!_wcsicmp(arg, L"48K"))
				g_sim.SetMemoryMode(kATMemoryMode_48K);
			else if (!_wcsicmp(arg, L"52K"))
				g_sim.SetMemoryMode(kATMemoryMode_52K);
			else if (!_wcsicmp(arg, L"64K"))
				g_sim.SetMemoryMode(kATMemoryMode_64K);
			else if (!_wcsicmp(arg, L"128K"))
				g_sim.SetMemoryMode(kATMemoryMode_128K);
			else if (!_wcsicmp(arg, L"320K"))
				g_sim.SetMemoryMode(kATMemoryMode_320K);
			else if (!_wcsicmp(arg, L"576K"))
				g_sim.SetMemoryMode(kATMemoryMode_576K);
			else if (!_wcsicmp(arg, L"1088K"))
				g_sim.SetMemoryMode(kATMemoryMode_1088K);
			else
				throw MyError("Command line error: Invalid memory mode '%ls'", arg);
		}

		if (cmdLine.FindAndRemoveSwitch(L"vsync"))
			g_sim.GetGTIA().SetVsyncEnabled(true);
		else if (cmdLine.FindAndRemoveSwitch(L"novsync"))
			g_sim.GetGTIA().SetVsyncEnabled(false);

		if (cmdLine.FindAndRemoveSwitch(L"debug"))
			debugMode = true;

		if (cmdLine.FindAndRemoveSwitch(L"?")) {
			MessageBox(g_hwnd,
				"Usage: Altirra [switches] <disk/cassette/cartridge image>\n"
				"\n"
				"/resetall - Reset all settings to default\n"
				"/baseline - Reset hardware settings to default\n"
				"/ntsc - select NTSC timing\n"
				"/pal - select PAL timing\n"
				"/[no]burstio|burstiopolled - control SIO burst I/O mode\n"
				"/[no]siopatch - control SIO kernel patch\n"
				"/[no]casautoboot - control cassette auto-boot\n"
				"/[no]accuratedisk - control accurate sector timing\n"
				"/[no]basic - enable/disable BASIC ROM\n"
				"/[no]vbxe - enable/disable VBXE support\n"
				"/[no]vbxeshared - enable/disable VBXE shared memory\n"
				"/[no]vbxealtpage - enable/disable VBXE $D7xx register window\n"
				"/kernel:default|osb|xl|lle|hle|other - select kernel ROM\n"
				"/hardware:800|800xl - select hardware type\n"
				"/memsize:48K|52K|64K|128K|320K|576K|1088K - select memory size\n"
				"/[no]stereo - enable dual pokeys\n"
				"/gdi - force GDI display mode\n"
				"/ddraw - force DirectDraw display mode\n"
				"/opengl - force OpenGL display mode\n"
				"/[no]vsync - synchronize to vertical blank\n"
				"/debug - launch in debugger mode"
				"/f - start full screen"
				,
				"Altirra command-line help", MB_OK | MB_ICONINFORMATION);
		}

		VDCommandLineIterator it;
		if (cmdLine.GetNextNonSwitchArgument(it, arg)) {
			g_sim.Load(arg);
			coldReset = true;// required to set up cassette autoboot
		}
	} catch(const MyError& e) {
		e.post(hwnd, "Altirra error");
	}

	if (coldReset)
		g_sim.ColdReset();

	if (debugMode)
		ATShowConsole();
}

void LoadSettingsEarly(bool useHardwareBaseline) {
	g_enhancedTextFont.lfHeight			= 16;
    g_enhancedTextFont.lfWidth			= 0;
    g_enhancedTextFont.lfEscapement		= 0;
    g_enhancedTextFont.lfOrientation	= 0;
    g_enhancedTextFont.lfWeight			= 0;
    g_enhancedTextFont.lfItalic			= FALSE;
    g_enhancedTextFont.lfUnderline		= FALSE;
    g_enhancedTextFont.lfStrikeOut		= FALSE;
	g_enhancedTextFont.lfCharSet		= DEFAULT_CHARSET;
	g_enhancedTextFont.lfOutPrecision	= OUT_DEFAULT_PRECIS;
	g_enhancedTextFont.lfClipPrecision	= CLIP_DEFAULT_PRECIS;
    g_enhancedTextFont.lfQuality		= DEFAULT_QUALITY;
	g_enhancedTextFont.lfPitchAndFamily	= FF_DONTCARE | DEFAULT_PITCH;
    wcscpy(g_enhancedTextFont.lfFaceName, L"Lucida Console");

	VDRegistryAppKey key("Settings");
	VDStringW family;
	int fontSize;
	if (key.getString("Enhanced video: Font family", family)
		&& (fontSize = key.getInt("Enhanced video: Font size", 0))) {

		g_enhancedTextFont.lfHeight = fontSize;
		vdwcslcpy(g_enhancedTextFont.lfFaceName, family.c_str(), sizeof(g_enhancedTextFont.lfFaceName)/sizeof(g_enhancedTextFont.lfFaceName[0]));
	}

	g_displayStretchMode = (ATDisplayStretchMode)key.getEnumInt("Display: Stretch mode", kATDisplayStretchModeCount, g_displayStretchMode);
}

void SaveMountedImages() {
	VDRegistryAppKey key("Mounted Images", true);
	VDStringA name;
	VDStringW imagestr;

	for(int i=0; i<8; ++i) {
		ATDiskEmulator& disk = g_sim.GetDiskDrive(i);
		name.sprintf("Disk %d", i);

		const wchar_t *path = disk.GetPath();

		if (path && disk.IsDiskBacked()) {
			wchar_t c = 'R';

			if (disk.IsWriteEnabled()) {
				if (disk.IsAutoFlushEnabled())
					c = 'W';
				else
					c = 'V';
			}

			imagestr.sprintf(L"%c%ls", c, path);
			key.setString(name.c_str(), imagestr.c_str());
		} else {
			key.removeValue(name.c_str());
		}
	}

	ATCartridgeEmulator *cart = g_sim.GetCartridge();
	const wchar_t *cartPath = NULL;
	if (cart)
		cartPath = cart->GetPath();

	if (cartPath)
		key.setString("Cartridge", cartPath);
	else
		key.removeValue("Cartridge");
}

void LoadMountedImages() {
	VDRegistryAppKey key("Mounted Images");
	VDStringA name;
	VDStringW imagestr;

	for(int i=0; i<8; ++i) {
		name.sprintf("Disk %d", i);

		if (key.getString(name.c_str(), imagestr) && imagestr.size() > 1) {
			ATDiskEmulator& disk = g_sim.GetDiskDrive(i);

			wchar_t mode = imagestr[0];
			bool writeEnabled;
			bool autoFlush;

			if (mode == L'V') {
				writeEnabled = true;
				autoFlush = false;
			} else if (mode == L'R') {
				writeEnabled = false;
				autoFlush = false;
			} else if (mode == L'W') {
				writeEnabled = true;
				autoFlush = true;
			} else
				continue;

			try {
				disk.LoadDisk(imagestr.c_str() + 1);
			} catch(const MyError&) {
			}

			disk.SetWriteFlushMode(writeEnabled, autoFlush);
		}
	}

	if (key.getString("Cartridge", imagestr)) {
		try {
			g_sim.LoadCartridge(imagestr.c_str());
		} catch(const MyError&) {
		}
	}
}

void LoadSettings(bool useHardwareBaseline) {
	VDRegistryAppKey key("Settings");

	ATPokeyEmulator& pokey = g_sim.GetPokey();
	ATDiskEmulator& disk = g_sim.GetDiskDrive(0);

	if (!useHardwareBaseline) {
		g_sim.SetKernelMode((ATKernelMode)key.getEnumInt("Kernel mode", kATKernelModeCount, g_sim.GetKernelMode()));
		g_sim.SetHardwareMode((ATHardwareMode)key.getEnumInt("Hardware mode", kATHardwareModeCount, g_sim.GetHardwareMode()));
		g_sim.SetBASICEnabled(key.getBool("BASIC enabled", g_sim.IsBASICEnabled()));
		g_sim.SetPALMode(key.getBool("PAL mode", g_sim.IsPALMode()));
		g_sim.SetMemoryMode((ATMemoryMode)key.getEnumInt("Memory mode", kATMemoryModeCount, g_sim.GetMemoryMode()));
		g_sim.SetCassetteSIOPatchEnabled(key.getBool("Cassette: SIO patch enabled", g_sim.IsCassetteSIOPatchEnabled()));
		g_sim.SetCassetteAutoBootEnabled(key.getBool("Cassette: Auto-boot enabled", g_sim.IsCassetteAutoBootEnabled()));
		g_sim.SetFPPatchEnabled(key.getBool("Kernel: Floating-point patch enabled", g_sim.IsFPPatchEnabled()));
		pokey.SetSerialBurstMode((ATPokeyEmulator::SerialBurstMode)key.getEnumInt("POKEY: Burst mode", ATPokeyEmulator::kSerialBurstModeCount, pokey.GetSerialBurstMode()));
		g_sim.SetVBXEEnabled(key.getBool("GTIA: VBXE support", g_sim.GetVBXE() != NULL));
		g_sim.SetVBXESharedMemoryEnabled(key.getBool("GTIA: VBXE shared memory", g_sim.IsVBXESharedMemoryEnabled()));
		g_sim.SetVBXEAltPageEnabled(key.getBool("GTIA: VBXE alternate page", g_sim.IsVBXEAltPageEnabled()));
		g_sim.SetDiskSIOPatchEnabled(key.getBool("Disk: SIO patch enabled", g_sim.IsDiskSIOPatchEnabled()));
		disk.SetAccurateSectorTimingEnabled(key.getBool("Disk: Accurate sector timing", disk.IsAccurateSectorTimingEnabled()));
		g_sim.SetDualPokeysEnabled(key.getBool("Audio: Dual POKEYs enabled", g_sim.IsDualPokeysEnabled()));

		g_enhancedText = key.getInt("Video: Enhanced text mode", 0);
		if (g_enhancedText > 1)
			g_enhancedText = 1;

		g_sim.SetPrinterEnabled(key.getBool("Printer: Enabled", g_sim.IsPrinterEnabled()));
		g_sim.SetRTime8Enabled(key.getBool("Misc: R-Time 8 Enabled", g_sim.IsRTime8Enabled()));

		g_sim.SetRandomFillEnabled(key.getBool("Memory: Randomize on start", g_sim.IsRandomFillEnabled()));
	}

	g_sim.SetTurboModeEnabled(key.getBool("Turbo mode", g_sim.IsTurboModeEnabled()));
	g_pauseInactive = key.getBool("Pause when inactive", g_pauseInactive);

	g_sim.GetCassette().SetLoadDataAsAudioEnable(key.getBool("Cassette: Load data as audio", g_sim.GetCassette().IsLoadDataAsAudioEnabled()));

	float volume = VDGetIntAsFloat(key.getInt("Audio: Volume", VDGetFloatAsInt(0.5f)));
	if (!(volume >= 0.0f && volume <= 1.0f))
		volume = 0.5f;
	pokey.SetVolume(volume);

	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	gtia.SetArtifactingMode((ATGTIAEmulator::ArtifactMode)key.getEnumInt("GTIA: Artifacting mode", ATGTIAEmulator::kArtifactCount, gtia.GetArtifactingMode()));
	gtia.SetOverscanMode((ATGTIAEmulator::OverscanMode)key.getEnumInt("GTIA: Overscan mode", ATGTIAEmulator::kOverscanCount, gtia.GetOverscanMode()));
	gtia.SetOverscanPALExtended(key.getBool("GTIA: PAL extended height", gtia.IsOverscanPALExtended()));
	gtia.SetBlendModeEnabled(key.getBool("GTIA: Frame blending", gtia.IsBlendModeEnabled()));
	gtia.SetBlendModeEnabled(key.getBool("GTIA: Interlace", gtia.IsInterlaceEnabled()));

	ATColorParams colpa(gtia.GetColorParams());
	colpa.mHueStart = VDGetIntAsFloat(key.getInt("Colors: Hue Start", VDGetFloatAsInt(colpa.mHueStart)));
	colpa.mHueRange = VDGetIntAsFloat(key.getInt("Colors: Hue Range", VDGetFloatAsInt(colpa.mHueRange)));
	colpa.mBrightness = VDGetIntAsFloat(key.getInt("Colors: Brightness", VDGetFloatAsInt(colpa.mBrightness)));
	colpa.mContrast = VDGetIntAsFloat(key.getInt("Colors: Contrast", VDGetFloatAsInt(colpa.mContrast)));
	colpa.mSaturation = VDGetIntAsFloat(key.getInt("Colors: Saturation", VDGetFloatAsInt(colpa.mSaturation)));
	colpa.mArtifactHue = VDGetIntAsFloat(key.getInt("Colors: Artifact Hue", VDGetFloatAsInt(colpa.mArtifactHue)));
	colpa.mArtifactSat = VDGetIntAsFloat(key.getInt("Colors: Artifact Saturation", VDGetFloatAsInt(colpa.mArtifactSat)));
	colpa.mArtifactBias = VDGetIntAsFloat(key.getInt("Colors: Artifact Bias", VDGetFloatAsInt(colpa.mArtifactBias)));
	gtia.SetColorParams(colpa);

	g_sim.SetDiskSectorCounterEnabled(key.getBool("Disk: Sector counter enabled", g_sim.IsDiskSectorCounterEnabled()));

	ATCPUEmulator& cpu = g_sim.GetCPU();
	cpu.SetHistoryEnabled(key.getBool("CPU: History enabled", cpu.IsHistoryEnabled()));
	cpu.SetPathfindingEnabled(key.getBool("CPU: Pathfinding enabled", cpu.IsPathfindingEnabled()));
	cpu.SetStopOnBRK(key.getBool("CPU: Stop on BRK", cpu.GetStopOnBRK()));
	cpu.SetCPUMode((ATCPUMode)key.getEnumInt("CPU: Chip type", kATCPUModeCount, cpu.GetCPUMode()));

	IATHardDiskEmulator *hd = g_sim.GetHardDisk();
	if (hd) {
		VDStringW path;
		if (key.getString("Hard disk: Host path", path))
			hd->SetBasePath(path.c_str());

		hd->SetEnabled(key.getBool("Hard disk: Enabled", hd->IsEnabled()));
		hd->SetReadOnly(key.getBool("Hard disk: Read only", hd->IsReadOnly()));
	}

	pokey.SetNonlinearMixingEnabled(key.getBool("Audio: Non-linear mixing", pokey.IsNonlinearMixingEnabled()));

	g_dispFilterMode = (IVDVideoDisplay::FilterMode)key.getEnumInt("Display: Filter mode", IVDVideoDisplay::kFilterBicubic + 1, g_dispFilterMode);
	if (g_pDisplay)
		g_pDisplay->SetFilterMode(g_dispFilterMode);

	g_showFps = key.getBool("View: Show FPS", g_showFps);
	gtia.SetVsyncEnabled(key.getBool("View: Vertical sync", gtia.IsVsyncEnabled()));

	VDRegistryAppKey imapKey("Settings\\Input maps");
	g_sim.GetInputManager()->Load(imapKey);

	ATReloadPortMenus();

	if (!useHardwareBaseline)
		LoadMountedImages();
}

void SaveSettings() {
	VDRegistryAppKey key("Settings");

	key.setInt("Kernel mode", g_sim.GetKernelMode());
	key.setInt("Hardware mode", g_sim.GetHardwareMode());
	key.setBool("BASIC enabled", g_sim.IsBASICEnabled());
	key.setBool("PAL mode", g_sim.IsPALMode());
	key.setInt("Memory mode", g_sim.GetMemoryMode());
	key.setBool("Turbo mode", g_sim.IsTurboModeEnabled());
	key.setBool("Pause when inactive", g_pauseInactive);

	key.setBool("Memory: Randomize on start", g_sim.IsRandomFillEnabled());

	key.setBool("Kernel: Floating-point patch enabled", g_sim.IsFPPatchEnabled());

	key.setBool("Cassette: SIO patch enabled", g_sim.IsCassetteSIOPatchEnabled());
	key.setBool("Cassette: Auto-boot enabled", g_sim.IsCassetteAutoBootEnabled());
	key.setBool("Cassette: Load data as audio", g_sim.GetCassette().IsLoadDataAsAudioEnabled());

	ATPokeyEmulator& pokey = g_sim.GetPokey();
	key.setInt("Audio: Volume", VDGetFloatAsInt(pokey.GetVolume()));
	key.setInt("POKEY: Burst mode", pokey.GetSerialBurstMode());
	
	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	key.setInt("GTIA: Artifacting mode", gtia.GetArtifactingMode());
	key.setInt("GTIA: Overscan mode", gtia.GetOverscanMode());
	key.setBool("GTIA: PAL extended height", gtia.IsOverscanPALExtended());
	key.setBool("GTIA: Frame blending", gtia.IsBlendModeEnabled());
	key.setBool("GTIA: Interlace", gtia.IsInterlaceEnabled());
	key.setBool("GTIA: VBXE support", g_sim.GetVBXE() != NULL);
	key.setBool("GTIA: VBXE shared memory", g_sim.IsVBXESharedMemoryEnabled());
	key.setBool("GTIA: VBXE alternate page", g_sim.IsVBXEAltPageEnabled());

	ATColorParams colpa(gtia.GetColorParams());
	key.setInt("Colors: Hue Start", VDGetFloatAsInt(colpa.mHueStart));
	key.setInt("Colors: Hue Range", VDGetFloatAsInt(colpa.mHueRange));
	key.setInt("Colors: Brightness", VDGetFloatAsInt(colpa.mBrightness));
	key.setInt("Colors: Contrast", VDGetFloatAsInt(colpa.mContrast));
	key.setInt("Colors: Saturation", VDGetFloatAsInt(colpa.mSaturation));
	key.setInt("Colors: Artifact Hue", VDGetFloatAsInt(colpa.mArtifactHue));
	key.setInt("Colors: Artifact Saturation", VDGetFloatAsInt(colpa.mArtifactSat));
	key.setInt("Colors: Artifact Bias", VDGetFloatAsInt(colpa.mArtifactBias));

	ATDiskEmulator& disk = g_sim.GetDiskDrive(0);
	key.setBool("Disk: SIO patch enabled", g_sim.IsDiskSIOPatchEnabled());
	key.setBool("Disk: Accurate sector timing", disk.IsAccurateSectorTimingEnabled());
	key.setBool("Disk: Sector counter enabled", g_sim.IsDiskSectorCounterEnabled());

	key.setBool("Audio: Dual POKEYs enabled", g_sim.IsDualPokeysEnabled());
	key.setBool("Audio: Non-linear mixing", pokey.IsNonlinearMixingEnabled());

	ATCPUEmulator& cpu = g_sim.GetCPU();
	key.setBool("CPU: History enabled", cpu.IsHistoryEnabled());
	key.setBool("CPU: Pathfinding enabled", cpu.IsPathfindingEnabled());
	key.setBool("CPU: Stop on BRK", cpu.GetStopOnBRK());
	key.setInt("CPU: Chip type", cpu.GetCPUMode());

	IATHardDiskEmulator *hd = g_sim.GetHardDisk();
	if (hd) {
		key.setString("Hard disk: Host path", hd->GetBasePath());
		key.setBool("Hard disk: Enabled", hd->IsEnabled());
		key.setBool("Hard disk: Read only", hd->IsReadOnly());
	}

	key.setInt("Display: Filter mode", g_dispFilterMode);
	key.setInt("Display: Stretch mode", g_displayStretchMode);

	key.setInt("Video: Enhanced text mode", g_enhancedText);

	key.setBool("View: Show FPS", g_showFps);
	key.setBool("View: Vertical sync", gtia.IsVsyncEnabled());

	key.setBool("Printer: Enabled", g_sim.IsPrinterEnabled());
	key.setBool("Misc: R-Time 8 Enabled", g_sim.IsRTime8Enabled());

	SaveInputMaps();
	SaveMountedImages();
}

void SaveInputMaps() {
	VDRegistryAppKey imapKey("Settings\\Input maps");
	g_sim.GetInputManager()->Save(imapKey);
}

class ATUIWindowCaptionUpdater {
public:
	ATUIWindowCaptionUpdater();

	void Update(HWND hwnd, bool running, int ticks, double fps);
	void CheckForStateChange(bool force);

protected:
	bool mbLastRunning;
	bool mbLastCaptured;

	VDStringA	mBasePrefix;
	VDStringA	mPrefix;
	VDStringA	mBuffer;

	ATHardwareMode	mLastHardwareMode;
	ATKernelMode	mLastKernelMode;
	ATMemoryMode	mLastMemoryMode;
	bool			mbLastBASICState;
	bool			mbLastPALState;
	bool			mbLastVBXEState;
	bool			mbForceUpdate;
	bool			mbLastDebugging;
	bool			mbLastShowFPS;
};

ATUIWindowCaptionUpdater::ATUIWindowCaptionUpdater()
	: mbLastRunning(false)
	, mbLastCaptured(false)
	, mLastHardwareMode(kATHardwareModeCount)
	, mLastKernelMode(kATKernelModeCount)
	, mLastMemoryMode(kATMemoryModeCount)
	, mbLastBASICState(false)
	, mbLastPALState(false)
	, mbLastVBXEState(false)
	, mbLastDebugging(false)
	, mbForceUpdate(false)
{
	mBasePrefix = "Altirra "AT_VERSION AT_VERSION_DEBUG;

	CheckForStateChange(true);
}

void ATUIWindowCaptionUpdater::Update(HWND hwnd, bool running, int ticks, double fps) {
	bool forceUpdate = false;

	if (mbLastRunning != running) {
		mbLastRunning = running;
		forceUpdate = true;
	}

	if (mbLastCaptured != g_mouseCaptured) {
		mbLastCaptured = g_mouseCaptured;
		forceUpdate = true;
	}

	CheckForStateChange(forceUpdate);

	mBuffer = mPrefix;

	if ((running && g_showFps) || mbForceUpdate) {
		mbForceUpdate = false;

		if (g_showFps)
			mBuffer.append_sprintf(" - %d (%.3f fps)", ticks, fps);

		if (g_mouseCaptured)
			mBuffer += " (mouse captured - right Alt to release)";

		SetWindowText(hwnd, mBuffer.c_str());
	}
}

void ATUIWindowCaptionUpdater::CheckForStateChange(bool force) {
	bool change = false;

	ATHardwareMode hwmode = g_sim.GetHardwareMode();
	if (mLastHardwareMode != hwmode) {
		mLastHardwareMode = hwmode;
		change = true;
	}

	ATKernelMode krmode = g_sim.GetKernelMode();
	if (mLastKernelMode != krmode) {
		mLastKernelMode = krmode;
		change = true;
	}

	ATMemoryMode mmmode = g_sim.GetMemoryMode();
	if (mLastMemoryMode != mmmode) {
		mLastMemoryMode = mmmode;
		change = true;
	}

	bool basic = g_sim.IsBASICEnabled();
	if (mbLastBASICState != basic) {
		mbLastBASICState = basic;
		change = true;
	}

	bool pal = g_sim.IsPALMode();
	if (mbLastPALState != pal) {
		mbLastPALState = pal;
		change = true;
	}

	bool vbxe = g_sim.GetVBXE() != NULL;
	if (mbLastVBXEState != vbxe) {
		mbLastVBXEState = vbxe;
		change = true;
	}

	bool debugging = ATIsDebugConsoleActive();
	if (mbLastDebugging != debugging) {
		mbLastDebugging = debugging;
		change = true;
	}

	if (mbLastShowFPS != g_showFps) {
		mbLastShowFPS = g_showFps;
		change = true;
	}

	if (!force && !change)
		return;

	mPrefix = mBasePrefix;

	if (ATIsDebugConsoleActive()) {
		if (mbLastRunning)
			mPrefix += " [running]";
		else
			mPrefix += " [stopped]";
	}

	switch(g_sim.GetHardwareMode()) {
		case kATHardwareMode_800:
			mPrefix += ": 800";
			break;

		case kATHardwareMode_800XL:
			mPrefix += ": XL/XE";
			break;
	}

	switch(g_sim.GetActualKernelMode()) {
		case kATKernelMode_OSA:
			mPrefix += " OS-A";
			break;

		case kATKernelMode_OSB:
			mPrefix += " OS-B";
			break;

		case kATKernelMode_XL:
			break;

		case kATKernelMode_Other:
			mPrefix += " Other";
			break;

		case kATKernelMode_HLE:
			mPrefix += " HLE";
			break;

		case kATKernelMode_LLE:
			mPrefix += " LLE";
			break;
	}

	if (pal)
		mPrefix += " PAL";
	else
		mPrefix += " NTSC";

	if (vbxe)
		mPrefix += "-VBXE";

	mPrefix += " / ";

	switch(g_sim.GetMemoryMode()) {
		case kATMemoryMode_48K:
			mPrefix += "48K";
			break;

		case kATMemoryMode_52K:
			mPrefix += "52K";
			break;

		case kATMemoryMode_64K:
			mPrefix += "64K";
			break;

		case kATMemoryMode_128K:
			mPrefix += "128K";
			break;

		case kATMemoryMode_320K:
			mPrefix += "320K";
			break;

		case kATMemoryMode_576K:
			mPrefix += "576K";
			break;

		case kATMemoryMode_1088K:
			mPrefix += "1088K";
			break;
	}

	if (basic)
		mPrefix += " / BASIC";

	mbForceUpdate = true;
}

int RunMainLoop2(HWND hwnd, VDCommandLine& cmdLine) {
	bool commandLineRead = false;
	int ticks = 0;

	ATAnticEmulator& antic = g_sim.GetAntic();
	uint32 lastFrame = antic.GetFrameCounter();
	LARGE_INTEGER secondTime;
	LARGE_INTEGER lastTime;
	LARGE_INTEGER errorBound;
	sint64 error = 0;
	QueryPerformanceFrequency(&secondTime);

	// NTSC: 1.7897725MHz master clock, 262 scanlines of 114 clocks each
	sint64 ntscFrameTime = VDRoundToInt64((double)secondTime.QuadPart / 59.9227);

	// PAL: 1.773447MHz master clock, 312 scanlines of 114 clocks each
	sint64 palFrameTime = VDRoundToInt64((double)secondTime.QuadPart / 49.8607);
	errorBound.QuadPart = palFrameTime * 5;
	QueryPerformanceCounter(&lastTime);

	LARGE_INTEGER lastStatusTime;
	QueryPerformanceCounter(&lastStatusTime);

	sint64 nextFrameTime;
	int nextTickUpdate = 60;
	bool nextFrameTimeValid = false;

	ATUIWindowCaptionUpdater winCaptionUpdater;
	winCaptionUpdater.Update(hwnd, true, 0, 0);

	MSG msg;
	for(;;) {
		for(int i=0; i<2; ++i) {
			DWORD flags = i ? PM_REMOVE : PM_QS_INPUT | PM_REMOVE;

			while(PeekMessage(&msg, NULL, 0, 0, flags)) {
				if (msg.message == WM_QUIT)
					return (int)msg.wParam;

				if (msg.hwnd) {
					if (msg.message == WM_SYSCHAR) {
						if (g_hwnd && msg.hwnd != g_hwnd && GetAncestor(msg.hwnd, GA_ROOT) == g_hwnd)
						{
							msg.hwnd = g_hwnd;
						}
					}

					HWND hwndOwner;
					switch(msg.message) {
						case WM_KEYDOWN:
						case WM_SYSKEYDOWN:
						case WM_KEYUP:
						case WM_SYSKEYUP:
						case WM_CHAR:
							hwndOwner = GetAncestor(msg.hwnd, GA_ROOT);
							if (hwndOwner) {
								if (SendMessage(hwndOwner, WM_APP + 100, 0, (LPARAM)&msg))
									continue;
							}
							break;
					}
			
					switch(msg.message) {
						case WM_KEYDOWN:
							if (SendMessage(msg.hwnd, MYWM_PREKEYDOWN, msg.wParam, msg.lParam))
								continue;
							break;
						case WM_SYSKEYDOWN:
							if (SendMessage(msg.hwnd, MYWM_PRESYSKEYDOWN, msg.wParam, msg.lParam))
								continue;
							break;
						case WM_KEYUP:
							if (SendMessage(msg.hwnd, MYWM_PREKEYUP, msg.wParam, msg.lParam))
								continue;
							break;
						case WM_SYSKEYUP:
							if (SendMessage(msg.hwnd, MYWM_PRESYSKEYUP, msg.wParam, msg.lParam))
								continue;
							break;
					}
				}

				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}

		if (!commandLineRead) {
			commandLineRead = true;

			ReadCommandLine(hwnd, cmdLine);
		}

		if (g_sim.IsRunning() && (g_winActive || !g_pauseInactive)) {
			uint32 frame = antic.GetFrameCounter();
			if (frame != lastFrame) {
				ATDisplayBasePane *pane = static_cast<ATDisplayBasePane *>(ATGetUIPane(kATUIPaneId_Display));
				if (pane)
					pane->UpdateTextDisplay(g_enhancedText != 0);

				lastFrame = frame;

				LARGE_INTEGER curTime;
				QueryPerformanceCounter(&curTime);

				++ticks;

				if (!g_fullscreen && (ticks - nextTickUpdate) >= 0) {
					double fps = 0;

					if (g_showFps)
						fps = (double)secondTime.QuadPart / (double)(curTime.QuadPart - lastStatusTime.QuadPart) * 60;

					winCaptionUpdater.Update(hwnd, true, ticks, fps);

					lastStatusTime = curTime;
					nextTickUpdate += 60;
				}

				sint64 delta = curTime.QuadPart - lastTime.QuadPart;

				error += delta - (g_sim.IsPALMode() ? palFrameTime : ntscFrameTime);
				if (error > errorBound.QuadPart || error < -errorBound.QuadPart)
					error = 0;

				lastTime = curTime;

				nextFrameTimeValid = false;
				if (g_sim.IsTurboModeEnabled())
					error = 0;
				else if (error < 0) {
					nextFrameTimeValid = true;
					nextFrameTime = curTime.QuadPart - error;
				}
			}

			if (nextFrameTimeValid) {
				LARGE_INTEGER curTime;
				QueryPerformanceCounter(&curTime);

				sint64 delta = nextFrameTime - curTime.QuadPart;

				if (delta <= 0 || delta > secondTime.QuadPart)
					nextFrameTimeValid = false;
				else {
					int ticks = (int)(delta * 1000 / secondTime.QuadPart);

					if (ticks <= 0)
						nextFrameTimeValid = false;
					else {
						::MsgWaitForMultipleObjects(0, NULL, FALSE, ticks, QS_ALLINPUT);
						continue;
					}
				}
			}

			ATSimulator::AdvanceResult ar = g_sim.Advance();

			if (ar != ATSimulator::kAdvanceResult_Stopped) {
				if (ar == ATSimulator::kAdvanceResult_WaitingForFrame)
					::MsgWaitForMultipleObjects(0, NULL, FALSE, 1, QS_ALLINPUT);

				continue;
			}
		} else {
			if (g_fullscreen)
				ATSetFullscreen(false);

			if (g_mouseCaptured)
				ReleaseCapture();

			winCaptionUpdater.Update(hwnd, false, 0, 0);
			nextTickUpdate = ticks - ticks % 60;
		}

		WaitMessage();
	}

	return 0;
}

int RunMainLoop(HWND hwnd, VDCommandLine& cmdLine) {
	int rc;

	__try {
		rc = RunMainLoop2(hwnd, cmdLine);
	} __except(ATExceptionFilter(GetExceptionCode(), GetExceptionInformation())) {
	}

	return rc;
}

int CALLBACK WinMain(HINSTANCE, HINSTANCE, LPSTR, int nCmdShow) {
	VDCommandLine cmdLine(GetCommandLineW());

	VDFastMemcpyAutodetect();
	CPUEnableExtensions(CPUCheckForExtensions());
	VDInitThunkAllocator();

	InitCommonControls();

	const bool resetAll = cmdLine.FindAndRemoveSwitch(L"resetall");
	if (resetAll)
		SHDeleteKey(HKEY_CURRENT_USER, "Software\\virtualdub.org\\Altirra");

	VDRegisterVideoDisplayControl();

	VDRegistryAppKey::setDefaultKey("Software\\virtualdub.org\\Altirra\\");

	const bool useHardwareBaseline = cmdLine.FindAndRemoveSwitch(L"baseline");

	LoadSettingsEarly(useHardwareBaseline);

	VDShaderEditorBaseWindow::Register();

	ATInitUIFrameSystem();

	bool fsmode = false;

	if (cmdLine.FindAndRemoveSwitch(L"f"))
		fsmode = true;

	if (cmdLine.FindAndRemoveSwitch(L"lockd3d"))
		g_d3d9Lock.Lock();

	if (cmdLine.FindAndRemoveSwitch(L"gdi"))
		VDVideoDisplaySetFeatures(false, false, false, false, false, false, false);
	else if (cmdLine.FindAndRemoveSwitch(L"ddraw"))
		VDVideoDisplaySetFeatures(true, true, false, false, false, false, false);
	else if (cmdLine.FindAndRemoveSwitch(L"opengl"))
		VDVideoDisplaySetFeatures(true, false, false, true, false, false, false);
	else
		VDVideoDisplaySetFeatures(true, true, false, false, true, false, false);
	//VDVideoDisplaySetD3DFXFileName(L"d:\\shaders\\tvela2.fx");
	VDVideoDisplaySetDebugInfoEnabled(false);

	VDInitFilespecSystem();
	VDLoadFilespecSystemData();

	ATInitUIPanes();
	ATInitProfilerUI();
	ATRegisterUIPaneType(kATUIPaneId_Display, VDRefCountObjectFactory<ATDisplayPane, ATUIPane>);

	g_hMenu = LoadMenu(NULL, MAKEINTRESOURCE(IDR_MENU1));
	g_hAccel = LoadAccelerators(NULL, MAKEINTRESOURCE(IDR_ACCELERATOR1));

	ATInitPortMenus(g_hMenu);

	g_hInst = VDGetLocalModuleHandleW32();

	g_pMainWindow = new ATMainWindow;
	g_pMainWindow->AddRef();
	HWND hwnd = (HWND)g_pMainWindow->Create(150, 100, 640, 480, NULL, false);

	HICON hIcon = (HICON)LoadImage(g_hInst, MAKEINTRESOURCE(IDI_APPICON), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED);
	if (hIcon)
		SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);

	HICON hSmallIcon = (HICON)LoadImage(g_hInst, MAKEINTRESOURCE(IDI_APPICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
	if (hSmallIcon)
		SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hSmallIcon);

	ATUIRestoreWindowPlacement(hwnd, "Main window", nCmdShow);

	if (!(GetWindowLong(hwnd, GWL_STYLE) & WS_VISIBLE))
		ShowWindow(hwnd, nCmdShow);

	g_sim.Init(hwnd);
	g_sim.GetInputManager()->SetConsoleCallback(&g_inputConsoleCallback);

	SetWindowText(hwnd, "Altirra");
	g_hwnd = hwnd;

	SetMenu(hwnd, g_hMenu);

	try {
		g_sim.LoadROMs();
	} catch(MyError& e) {
		e.post(hwnd, "Altirra Error");
	}

	g_sim.Resume();
	ATInitDebugger();

	LoadSettings(useHardwareBaseline);

	ATRestorePaneLayout(NULL);
	g_sim.ColdReset();

	timeBeginPeriod(1);

	if (fsmode)
		ATSetFullscreen(true);

	int returnCode = RunMainLoop(hwnd, cmdLine);

	timeEndPeriod(1);

	g_pAudioWriter = NULL;

	SaveSettings();
	VDSaveFilespecSystemData();

	g_sim.Shutdown();

	g_d3d9Lock.Unlock();

	g_pMainWindow->Destroy();
	g_pMainWindow->Release();
	g_pMainWindow = NULL;
	g_hwnd = NULL;

	ATShutdownUIPanes();
	ATShutdownUIFrameSystem();
	VDShutdownThunkAllocator();

	return returnCode;
}
