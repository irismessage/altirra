#ifndef AT_VERSION_H
#define AT_VERSION_H

// Below are default values used in local builds. They are superceded by a
// header created by the release script during packaging.

// Readable version string.
#define AT_VERSION					"dev"

// Set if this is a pre-release build, cleared if this is a release build.
#define AT_VERSION_PRERELEASE		1

// Set if this is a local dev build, unset for packaged builds. 
#define AT_VERSION_DEV				1

// Version encoded as monotonic number for comparison purposes (4 x u16).
#define AT_VERSION_COMPARE_VALUE	(UINT64_C(0))

#endif
