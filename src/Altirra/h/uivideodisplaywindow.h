#ifndef f_AT_UIVIDEODISPLAY_H
#define f_AT_UIVIDEODISPLAY_H

#include "uiwidget.h"
#include "uicontainer.h"
#include "callback.h"
#include "simeventmanager.h"

class IATUIEnhancedTextEngine;
class ATUILabel;
class ATUIOnScreenKeyboard;
class ATXEP80Emulator;
class ATSimulatorEventManager;

class ATUIVideoDisplayWindow : public ATUIContainer, public IATSimulatorCallback {
public:
	enum {
		kActionOpenOSK = kActionCustom,
		kActionCloseOSK
	};

	ATUIVideoDisplayWindow();
	~ATUIVideoDisplayWindow();

	bool Init(ATSimulatorEventManager& sem);
	void Shutdown();

	void Copy();

	void ToggleCaptureMouse();
	void ReleaseMouse();
	void CaptureMouse();

	void OpenOSK();
	void CloseOSK();

	bool IsTextSelected() const { return !mDragPreviewSpans.empty(); }

	void SetDisplayRect(const vdrect32& r);

	void ClearXorRects();
	void AddXorRect(int x1, int y1, int x2, int y2);

	void SetXEP(ATXEP80Emulator *xep);
	void SetEnhancedTextEngine(IATUIEnhancedTextEngine *p) { mpEnhTextEngine = p; }

	ATCallbackHandler0<void>& OnAllowContextMenuEvent() { return mAllowContextMenuEvent; }
	ATCallbackHandler1<void, const vdpoint32&>& OnDisplayContextMenuEvent() { return mDisplayContextMenuEvent; }

public:
	virtual void OnSimulatorEvent(ATSimulatorEvent ev);

protected:
	virtual ATUITouchMode GetTouchModeAtPoint(const vdpoint32& pt) const;
	virtual void OnMouseDown(sint32 x, sint32 y, uint32 vk, bool dblclk);
	virtual void OnMouseUp(sint32 x, sint32 y, uint32 vk);
	virtual void OnMouseRelativeMove(sint32 dx, sint32 dy);
	virtual void OnMouseMove(sint32 x, sint32 y);
	virtual void OnMouseLeave();
	virtual void OnMouseHover(sint32 x, sint32 y);

	virtual bool OnContextMenu(const vdpoint32 *pt);

	virtual bool OnKeyDown(const ATUIKeyEvent& event);
	virtual bool OnKeyUp(const ATUIKeyEvent& event);
	virtual bool OnChar(const ATUICharEvent& event);

	virtual void OnActionStart(uint32 id);

	virtual void OnCreate();
	virtual void OnDestroy();
	virtual void OnSize();

	virtual void OnSetFocus();
	virtual void OnKillFocus();
	virtual void OnCaptureLost();

	virtual void Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h);

	bool ProcessKeyDown(const ATUIKeyEvent& event, bool enableKeyInput);
	bool ProcessKeyUp(const ATUIKeyEvent& event, bool enableKeyInput);
	void ProcessVirtKey(int vkey, uint8 keycode, bool repeat);

	uint32 ComputeCursorImage(const vdpoint32& pt) const;
	void UpdateMousePosition(int x, int y);
	const vdrect32 GetXEP80Area() const;
	bool MapPixelToBeamPosition(int x, int y, float& hcyc, float& vcyc, bool clamp) const;
	bool MapPixelToBeamPosition(int x, int y, int& xc, int& yc, bool clamp) const;
	void MapBeamPositionToPixel(int xc, int yc, int& x, int& y) const;
	void UpdateDragPreview(int x, int y);
	void UpdateDragPreviewXEP80(int x, int y);
	void UpdateDragPreviewAntic(int x, int y);
	void UpdateDragPreviewRects();
	void ClearDragPreview();
	int GetModeLineYPos(int ys, bool checkValidCopyText) const;
	int ReadText(char *dst, int yc, int startChar, int numChars) const;
	void ClearCoordinateIndicator();
	void SetCoordinateIndicator(int x, int y);

	uint32 mLastVkeyPressed;
	uint32 mLastVkeySent;

	vdrect32 mDisplayRect;

	bool	mbDragActive;
	int		mDragAnchorX;
	int		mDragAnchorY;

	struct XorRect {
		int mX1;
		int mY1;
		int mX2;
		int mY2;
	};

	typedef vdfastvector<XorRect> XorRects;
	XorRects mXorRects;

	bool	mbCoordIndicatorActive;
	bool	mbCoordIndicatorEnabled;
	vdrect32	mHoverTipArea;
	bool		mbHoverTipActive;

	struct TextSpan {
		int mX;
		int mY;
		int mWidth;
		int mHeight;
		int mCharX;
		int mCharWidth;
	};

	typedef vdfastvector<TextSpan> TextSpans;
	TextSpans mDragPreviewSpans;

	IATUIEnhancedTextEngine *mpEnhTextEngine;
	ATUIOnScreenKeyboard *mpOSK;

	ATSimulatorEventManager *mpSEM;
	ATXEP80Emulator *mpXEP;
	VDDisplayImageView mXEPImageView;
	uint32 mXEPChangeCount;
	uint32 mXEPLayoutChangeCount;
	uint32 mXEPDataReceivedCount;

	ATUILabel *mpUILabelBadSignal;

	ATCallbackHandler0<void> mAllowContextMenuEvent;
	ATCallbackHandler1<void, const vdpoint32&> mDisplayContextMenuEvent;
};

#endif