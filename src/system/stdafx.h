// Detect the Windows SDK in use and select Windows 2000 baseline
// if the Vista SDK, else Windows 98 baseline.
#ifdef _MSC_VER
#include <ntverp.h>
#else
#define VER_PRODUCTBUILD 6001
#endif
#if VER_PRODUCTBUILD > 6000
#define _WIN32_WINNT 0x0500
#else
#define _WIN32_WINNT 0x0410
#endif

#include <vd2/system/vdtypes.h>
#include <vd2/system/atomic.h>
#include <vd2/system/thread.h>
#include <vd2/system/error.h>
#include <windows.h>
#include <process.h>
#include <vd2/system/win32/intrin.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <ctype.h>
