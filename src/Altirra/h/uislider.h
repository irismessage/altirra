#ifndef f_AT_UISLIDER_H
#define f_AT_UISLIDER_H

#include "uicontainer.h"
#include "callback.h"

class ATUIButton;

class ATUISlider : public ATUIContainer {
public:
	enum {
		kActionPagePrior = kActionCustom,
		kActionPageNext,
		kActionLinePrior,
		kActionLineNext
	};

	ATUISlider();
	~ATUISlider();

	void SetVertical(bool vert);
	void SetPos(sint32 pos);
	void SetPageSize(sint32 pageSize);
	void SetLineSize(sint32 lineSize) { mLineSize = lineSize; }
	void SetRange(sint32 minVal, sint32 maxVal);

	void AutoSize();

	ATCallbackHandler2<void, ATUISlider *, sint32>& OnValueChangedEvent() { return mValueChangedEvent; }

public:
	virtual void OnCreate();
	virtual void OnDestroy();
	virtual void OnSize();
	virtual void OnMouseDownL(sint32 x, sint32 y);
	virtual void OnMouseMove(sint32 x, sint32 y);
	virtual void OnMouseUpL(sint32 x, sint32 y);

	virtual void OnActionStart(uint32 trid);
	virtual void OnActionRepeat(uint32 trid);

	virtual void OnCaptureLost();

protected:
	virtual void Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h);

	void OnButtonLowerPressed(ATUIButton *);
	void OnButtonRaisePressed(ATUIButton *);
	void OnButtonReleased(ATUIButton *);

	void SetPosInternal(sint32 pos, bool notify);

	sint32 mMin;
	sint32 mMax;
	sint32 mPageSize;
	sint32 mLineSize;
	sint32 mPos;
	float mFloatPos;
	sint32 mPixelPos;

	sint32 mThumbSize;
	sint32 mTrackMin;
	sint32 mTrackSize;

	bool mbVertical;
	bool mbDragging;
	sint32 mDragOffset;

	ATUIButton *mpButtonLower;
	ATUIButton *mpButtonRaise;

	ATCallbackHandler2<void, ATUISlider *, sint32> mValueChangedEvent;
};

#endif
