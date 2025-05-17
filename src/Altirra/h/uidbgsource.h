//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2022 Avery Lee
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

#ifndef f_AT_UIDBGSOURCE_H
#define f_AT_UIDBGSOURCE_H

#include <map>
#include <vd2/system/filewatcher.h>
#include "debugger.h"
#include "texteditor.h"
#include "uidbgpane.h"

class ATSourceWindow : public ATUIDebuggerPaneWindow
					 , public IATDebuggerClient
					 , public IVDTextEditorCallback
					 , public IVDTextEditorColorizer
					 , public IVDFileWatcherCallback
					 , public IATSourceWindow
{
public:
	ATSourceWindow(uint32 id, const wchar_t *name);
	~ATSourceWindow();

	static bool CreatePane(uint32 id, ATUIPane **pp);

	void LoadFile(const wchar_t *s, const wchar_t *alias);

	const wchar_t *GetFullPath() const override {
		return mFullPath.c_str();
	}

	const wchar_t *GetPath() const override {
		return mPath.c_str();
	}

	const wchar_t *GetPathAlias() const override {
		return mPathAlias.empty() ? NULL : mPathAlias.c_str();
	}

	VDStringW ReadLine(int lineIndex) override;

protected:
	virtual bool OnPaneCommand(ATUIPaneCommandId id);

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	bool OnCreate();
	void OnDestroy();
	void OnSize();
	void OnFontsUpdated();
	bool OnCommand(uint32 id, uint32 extcode) override;

	void OnTextEditorUpdated() override;
	void OnTextEditorScrolled(int firstVisiblePara, int lastVisiblePara, int visibleParaCount, int totalParaCount) override;
	void OnLinkSelected(uint32 selectionCode, int para, int offset) override;

	void RecolorLine(int line, const wchar_t *text, int length, IVDTextEditorColorization *colorization) override;

	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state);
	void OnDebuggerEvent(ATDebugEvent eventId);
	int GetLineForAddress(uint32 addr);
	bool OnFileUpdated(const wchar_t *path);

	void	ToggleBreakpoint();

	void	ActivateLine(int line) override;
	void	FocusOnLine(int line) override;
	void	SetPCLine(int line);
	void	SetFramePCLine(int line);

	sint32	GetCurrentLineAddress() const;
	bool	BindToSymbols();
	void	MergeSymbols();

	VDStringA mModulePath;
	uint32	mModuleId;
	uint16	mFileId;
	sint32	mLastPC = -1;
	uint32	mLastFrameExtPC = ~UINT32_C(0);

	int		mPCLine = -1;
	int		mFramePCLine = -1;

	uint32	mEventCallbackId = 0;

	vdrefptr<IVDTextEditor>	mpTextEditor;
	HWND	mhwndTextEditor;
	VDStringW	mFullPath;
	VDStringW	mPath;
	VDStringW	mPathAlias;

	using AddressLookup = vdhashmap<uint32, uint32>;
	AddressLookup	mSymbolAddressToLineLookup;
	AddressLookup	mSymbolLineToAddressLookup;

	AddressLookup	mFileAddressToLineLookup;
	AddressLookup	mFileLineToAddressLookup;

	using LineToAddressLookup = std::map<uint32, uint32>;
	LineToAddressLookup	mLineToAddressLookup;

	using AddressToLineLookup = std::map<uint32, uint32>;
	AddressToLineLookup	mAddressToLineLookup;

	VDFileWatcher	mFileWatcher;
};

#endif
