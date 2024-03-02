#ifndef f_AT2_UIMANAGER_H
#define f_AT2_UIMANAGER_H

#include <vd2/VDDisplay/compositor.h>
#include <vd2/VDDisplay/renderer.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/system/event.h>

class ATUIWidget;
class ATUIContainer;
class IVDDisplayFont;
class VDDisplayImageView;

enum ATUIThemeFont {
	kATUIThemeFont_Default,
	kATUIThemeFont_Mono,
	kATUIThemeFont_MonoSmall,
	kATUIThemeFont_Menu,
	kATUIThemeFont_Tooltip,
	kATUIThemeFont_TooltipBold,
	kATUIThemeFontCount
};

enum ATUIStockImageIdx {
	kATUIStockImageIdx_MenuCheck,
	kATUIStockImageIdx_MenuRadio,
	kATUIStockImageIdx_MenuArrow,
	kATUIStockImageIdxCount
};

struct ATUIStockImage {
	VDDisplayImageView mImageView;
	VDPixmapBuffer mBuffer;
	int mOffsetX;
	int mOffsetY;
	int mWidth;
	int mHeight;
};

class IATUINativeDisplay {
public:
	virtual void Invalidate() = 0;
	virtual void CaptureCursor() = 0;
	virtual void ReleaseCursor() = 0;
	virtual void BeginModal() = 0;
	virtual void EndModal() = 0;
	virtual bool IsKeyDown(uint32 vk) = 0;
};

class ATUIManager : public IVDDisplayCompositor {
	ATUIManager(const ATUIManager&);
	ATUIManager& operator=(const ATUIManager&);
public:
	ATUIManager();
	~ATUIManager();

	int AddRef() { return 2; }
	int Release() { return 1; }

	void Init(IATUINativeDisplay *natDisplay);
	void Shutdown();

	ATUIContainer *GetMainWindow() const;
	uint32 GetCurrentCursorImageId() const { return mCursorImageId; }
	bool IsCursorCaptured() const { return mbCursorCaptured; }
	ATUIWidget *GetCursorCaptureWindow() const { return mbCursorCaptured ? mpCursorWindow : NULL; }
	bool IsInvalidated() const { return mbInvalidated; }

	void Resize(sint32 w, sint32 h);

	void SetFocusWindow(ATUIWidget *w);
	void CaptureCursor(ATUIWidget *w);

	void BeginModal(ATUIWidget *w);
	void EndModal();

	bool IsKeyDown(uint32 vk);

	bool OnMouseMove(sint32 x, sint32 y);
	bool OnMouseDownL(sint32 x, sint32 y);
	bool OnMouseUpL(sint32 x, sint32 y);
	void OnMouseLeave();

	bool OnKeyDown(uint32 vk);
	bool OnKeyUp(uint32 vk);
	bool OnChar(uint32 ch);

	IVDDisplayFont *GetThemeFont(ATUIThemeFont themeFont) const { return mpThemeFonts[themeFont]; }
	ATUIStockImage& GetStockImage(ATUIStockImageIdx stockImage) const { return *mpStockImages[stockImage]; }

public:
	void Attach(ATUIWidget *w);
	void Detach(ATUIWidget *w);
	void Invalidate(ATUIWidget *w);

public:
	void Composite(IVDDisplayRenderer& r, const VDDisplayCompositeInfo& compInfo);

protected:
	void UpdateCursorImage();
	bool UpdateCursorWindow(sint32 x, sint32 y);

	IATUINativeDisplay *mpNativeDisplay;
	ATUIContainer *mpMainWindow;
	ATUIWidget *mpCursorWindow;
	ATUIWidget *mpFocusWindow;
	ATUIWidget *mpModalWindow;
	bool mbCursorCaptured;
	uint32 mCursorImageId;

	bool mbInvalidated;

	struct ModalEntry {
		ATUIWidget *mpPreviousModal;
	};

	typedef vdfastvector<ModalEntry> ModalStack;
	ModalStack mModalStack;

	IVDDisplayFont *mpThemeFonts[kATUIThemeFontCount];

	ATUIStockImage *mpStockImages[kATUIStockImageIdxCount];
};

#endif
