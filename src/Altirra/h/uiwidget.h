#ifndef f_AT2_UIWIDGET_H
#define f_AT2_UIWIDGET_H

#include <vd2/system/vectors.h>
#include <vd2/VDDisplay/rendercache.h>

class IVDDisplayRenderer;
class VDDisplayTextRenderer;

class ATUIContainer;
class ATUIManager;

enum ATUICursorImage {
	kATUICursorImage_None,
	kATUICursorImage_Arrow,
	kATUICursorImage_IBeam
};

// Currently these need to match the Win32 definitions.
enum ATUIVirtKey {
	kATUIVK_Back	= 0x08,		// Backspace
	kATUIVK_Return	= 0x0D,
	kATUIVK_Shift	= 0x10,
	kATUIVK_Control	= 0x11,
	kATUIVK_Alt		= 0x12,
	kATUIVK_0		= 0x30,
	kATUIVK_A		= 0x41,
	kATUIVK_End		= 0x23,
	kATUIVK_Home	= 0x24,
	kATUIVK_Left	= 0x25,		// Left arrow
	kATUIVK_Up		= 0x26,		// Up arrow
	kATUIVK_Right	= 0x27,		// Right arrow
	kATUIVK_Down	= 0x28,		// Down arrow
	kATUIVK_Delete	= 0x2E
};

enum ATUIDockMode {
	kATUIDockMode_None,
	kATUIDockMode_Left,
	kATUIDockMode_Right,
	kATUIDockMode_Top,
	kATUIDockMode_Bottom,
	kATUIDockMode_Fill
};

class ATUIWidget : public vdrefcount {
public:
	ATUIWidget();
	~ATUIWidget();

	void Destroy();
	void Focus();

	bool IsCursorCaptured() const;
	void CaptureCursor();
	void ReleaseCursor();

	void SetFillColor(uint32 color);
	void SetAlphaFillColor(uint32 color);

	uint32 GetCursorImage() const { return mCursorImage; }
	void SetCursorImage(uint32 id) { mCursorImage = id; }

	const vdrect32& GetArea() const { return mArea; }
	void SetPosition(const vdpoint32& pt);
	void SetSize(const vdsize32& sz);
	void SetArea(const vdrect32& r);

	ATUIDockMode GetDockMode() const { return mDockMode; }
	void SetDockMode(ATUIDockMode mode);

	// Control visibility.
	//
	// An invisible window neither draws nor responds to input. Visibility is inherited
	// and an invisible parent window will hide all of its children.

	bool IsVisible() const { return mbVisible; }
	void SetVisible(bool visible);

	// Hit transparency.
	//
	// A window that is hit transparent does not respond to the cursor and passes mouse
	// input through to the next sibling. However, it still renders. Hit transparency
	// is _not_ inherited by children, which can receive input regardless of the parent.

	bool IsHitTransparent() const { return mbHitTransparent; }
	void SetHitTransparent(bool trans) { mbHitTransparent = trans; }

	ATUIManager *GetManager() const { return mpManager; }
	ATUIContainer *GetParent() const { return mpParent; }

	bool IsSameOrAncestorOf(ATUIWidget *w) const;

	virtual ATUIWidget *HitTest(vdpoint32 pt);

	virtual void OnMouseMove(sint32 x, sint32 y);
	virtual void OnMouseDownL(sint32 x, sint32 y);
	virtual void OnMouseUpL(sint32 x, sint32 y);
	virtual void OnMouseLeave();

	virtual bool OnKeyDown(uint32 vk);
	virtual bool OnKeyUp(uint32 vk);
	virtual bool OnChar(uint32 vk);

	virtual void OnCreate();
	virtual void OnDestroy();
	virtual void OnSize();

	virtual void OnKillFocus();
	virtual void OnSetFocus();

	void Draw(IVDDisplayRenderer& rdr);

public:
	void SetParent(ATUIManager *mgr, ATUIContainer *parent);

protected:
	void Invalidate();

	virtual void Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) = 0;

	ATUIManager *mpManager;
	ATUIContainer *mpParent;
	vdrect32 mArea;
	uint32 mFillColor;
	uint32 mCursorImage;
	ATUIDockMode mDockMode;

	bool mbVisible;
	bool mbFastClip;
	bool mbHitTransparent;

	VDDisplaySubRenderCache mRenderCache;
};

#endif
