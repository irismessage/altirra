#ifndef f_AT_HOSTFILEUTILS_H
#define f_AT_HOSTFILEUTILS_H

bool ATHostDeviceIsDevice(const wchar_t *s);

inline bool ATHostDeviceIsPathWild(const wchar_t *s) {
	return wcschr(s, L'*') || wcschr(s, L'?');
}

inline bool ATHostDeviceIsValidPathChar(uint8 c) {
	return c == '_' || (uint8)(c - '0') < 10 || (uint8)(c - 'A') < 26;
}

inline bool ATHostDeviceIsValidPathCharWide(wchar_t c) {
	return c == '_' || (uint32)(c - '0') < 10 || (uint32)(c - 'A') < 26;
}

void ATHostDeviceEncodeName(char encodedName[13], const wchar_t *hostName, bool useLongNameEncoding);

#endif
