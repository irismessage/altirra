#include <stdafx.h>
#include <vd2/Tessa/Format.h>

uint32 VDTGetBytesPerBlockRow(VDTFormat format, uint32 w) {
	switch(format) {
		default:
			return 0;

		case kVDTF_R16G16B16A16F:
			return w << 4;

		case kVDTF_R8G8B8A8:
		case kVDTF_B8G8R8A8:
		case kVDTF_B8G8R8A8_sRGB:
			return w << 2;

		case kVDTF_U8V8:
		case kVDTF_L8A8:
		case kVDTF_R8G8:
		case kVDTF_B5G6R5:
		case kVDTF_B5G5R5A1:
		return w << 1;

		case kVDTF_R8:
		case kVDTF_L8:
			return w;
	}
}

uint32 VDTGetNumBlockRows(VDTFormat format, uint32 h) {
	switch(format) {
		default:
			return 0;

		case kVDTF_R8G8B8A8:
		case kVDTF_B8G8R8A8:
		case kVDTF_B8G8R8A8_sRGB:
		case kVDTF_U8V8:
		case kVDTF_L8A8:
		case kVDTF_R8G8:
		case kVDTF_B5G6R5:
		case kVDTF_B5G5R5A1:
		case kVDTF_R8:
		case kVDTF_L8:
		case kVDTF_R16G16B16A16F:
			return h;
	}
}
