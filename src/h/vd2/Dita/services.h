#ifndef f_VD2_DITA_SERVICES_H
#define f_VD2_DITA_SERVICES_H

#include <ctype.h>

#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>

struct VDFileDialogOption {
	enum {
		kEnd,
		kBool,
		kInt,
		kEnabledInt,
		kReadOnly,
		kSelectedFilter,
		kConfirmFile
	};

	int				mType;
	int				mDstIdx;
	const wchar_t	*mpLabel;
	int				mMin;
	int				mMax;
};

const VDStringW VDGetLoadFileName(long nKey, VDGUIHandle ctxParent, const wchar_t *pszTitle, const wchar_t *pszFilters, const wchar_t *pszExt, const VDFileDialogOption *pOptions = NULL, int *pOptVals = NULL);
const VDStringW VDGetSaveFileName(long nKey, VDGUIHandle ctxParent, const wchar_t *pszTitle, const wchar_t *pszFilters, const wchar_t *pszExt, const VDFileDialogOption *pOptions = NULL, int *pOptVals = NULL);
void VDSetLastLoadSavePath(long nKey, const wchar_t *path);
const VDStringW VDGetLastLoadSavePath(long nKey);
void VDSetLastLoadSaveFileName(long nKey, const wchar_t *fileName);

const VDStringW VDGetDirectory(long nKey, VDGUIHandle ctxParent, const wchar_t *pszTitle);

void VDLoadFilespecSystemData();
void VDSaveFilespecSystemData();
void VDClearFilespecSystemData();

void VDInitFilespecSystem();

#endif
