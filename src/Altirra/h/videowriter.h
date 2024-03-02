#ifndef f_AT_VIDEOWRITER_H
#define f_AT_VIDEOWRITER_H

#include "gtia.h"
#include "pokey.h"

struct VDPixmap;
class VDFraction;

enum ATVideoEncoding {
	kATVideoEncoding_Raw,
	kATVideoEncoding_RLE,
	kATVideoEncoding_ZMBV,
	kATVideoEncodingCount
};

class IATVideoWriter : public IATGTIAVideoTap, public IATPokeyAudioTap {
public:
	virtual ~IATVideoWriter() {}

	virtual void Init(const wchar_t *filename, ATVideoEncoding venc, uint32 w, uint32 h, const VDFraction& frameRate, const uint32 *palette, const VDFraction& samplingRate, bool stereo, double timestampRate, IATUIRenderer *r) = 0;
	virtual void Shutdown() = 0;
};

void ATCreateVideoWriter(IATVideoWriter **w);

#endif	// f_AT_VIDEOWRITER_H
