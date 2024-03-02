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

#ifndef f_AT_UIDISKEXPLORER_WIN32_H
#define f_AT_UIDISKEXPLORER_WIN32_H

class IATDropTargetNotify {
public:
	virtual ATDiskFSKey GetDropTargetParentKey() const = 0;
	virtual void OnFSModified() = 0;
	virtual void WriteFile(const char *filename, const void *data, uint32 len, const VDDate& creationTime) = 0;
};

class IATUIDiskExplorerDataObjectW32 : public IUnknown {
public:
	virtual IDataObject *AsDataObject() = 0;
	virtual void AddFile(const ATDiskFSKey& fskey, uint32 bytes, const VDExpandedDate *date, const wchar_t *filename) = 0;
};

class IATUIDiskExplorerDropTargetW32 : public IUnknown {
public:
	virtual IDropTarget *AsDropTarget() = 0;
	virtual void SetFS(IATDiskFS *fs) = 0;
};

void ATUICreateDiskExplorerDataObjectW32(IATDiskFS *fs, IATUIDiskExplorerDataObjectW32 **target);
void ATUICreateDiskExplorerDropTargetW32(HWND hwnd, IATDropTargetNotify *notify, IATUIDiskExplorerDropTargetW32 **target);

#endif
