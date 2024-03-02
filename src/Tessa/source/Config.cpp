#include <vd2/Tessa/Config.h>

bool g_VDTLibraryOverridesEnabled = false;

void VDTSetLibraryOverridesEnabled(bool enabled) {
	g_VDTLibraryOverridesEnabled = enabled;
}

bool VDTGetLibraryOverridesEnabled() {
	return g_VDTLibraryOverridesEnabled;
}
