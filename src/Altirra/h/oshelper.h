#ifndef AT_OSHELPER_H
#define AT_OSHELPER_H

#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>

struct VDPixmap;
class VDStringW;

bool ATLoadKernelResource(int id, void *dst, uint32 offset, uint32 size);
bool ATLoadKernelResourceLZPacked(int id, vdfastvector<uint8>& data);
bool ATLoadMiscResource(int id, vdfastvector<uint8>& data);
void ATFileSetReadOnlyAttribute(const wchar_t *path, bool readOnly);

void ATCopyFrameToClipboard(void *hwnd, const VDPixmap& px);
void ATSaveFrame(void *hwnd, const VDPixmap& px, const wchar_t *filename);

void ATCopyTextToClipboard(void *hwnd, const char *s);

void ATUISaveWindowPlacement(void *hwnd, const char *name);
void ATUIRestoreWindowPlacement(void *hwnd, const char *name, int nCmdShow);

void ATUIEnableEditControlAutoComplete(void *hwnd);

VDStringW ATGetHelpPath();
void ATShowHelp(void *hwnd, const wchar_t *filename);

bool ATIsUserAdministrator();

void ATGenerateGuid(uint8 guid[16]);

#endif
