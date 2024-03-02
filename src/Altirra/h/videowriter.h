#ifndef f_AT_VIDEOWRITER_H
#define f_AT_VIDEOWRITER_H

#include <vd2/system/vdtypes.h>

struct VDPixmap;
class VDFraction;
class IATGTIAVideoTap;
class IATAudioTap;

enum ATVideoEncoding : uint8 {
	kATVideoEncoding_Raw,
	kATVideoEncoding_RLE,
	kATVideoEncoding_ZMBV,
	kATVideoEncoding_WMV7,
	kATVideoEncoding_WMV9,
	kATVideoEncoding_H264_AAC,
	kATVideoEncoding_H264_MP3,
	kATVideoEncodingCount
};

enum class ATVideoRecordingResamplingMode : uint8 {
	Nearest,
	SharpBilinear,
	Bilinear,
	Count
};

enum class ATVideoRecordingAspectRatioMode : uint8 {
	None,
	IntegerOnly,
	FullCorrection,
	Count
};

enum class ATVideoRecordingScalingMode : uint8 {
	None,
	Scale480Narrow,
	Scale480Wide,
	Scale720Narrow,
	Scale720Wide,
	Count
};

class IATVideoWriter {
public:
	virtual ~IATVideoWriter() = default;

	virtual IATGTIAVideoTap *AsVideoTap() = 0;
	virtual IATAudioTap *AsAudioTap() = 0;

	virtual void CheckExceptions() = 0;

	virtual void Init(const wchar_t *filename, ATVideoEncoding venc,
		uint32 videoBitRate, uint32 audioBitRate,
		uint32 w, uint32 h, const VDFraction& frameRate, double pixelAspectRatio,
		ATVideoRecordingResamplingMode resamplingMode,
		ATVideoRecordingScalingMode scalingMode,
		const uint32 *palette, double samplingRate, bool stereo, double timestampRate, bool halfRate, bool encodeAllFrames, IATUIRenderer *r) = 0;
	virtual void Shutdown() = 0;
};

void ATCreateVideoWriter(IATVideoWriter **w);

#endif	// f_AT_VIDEOWRITER_H
