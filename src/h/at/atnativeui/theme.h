//	Altirra - Atari 800/800XL/5200 emulator
//	Native UI library - system theme support
//	Copyright (C) 2008-2019 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#ifndef f_AT_ATNATIVEUI_THEME_H
#define f_AT_ATNATIVEUI_THEME_H

#include <vd2/system/function.h>

bool ATUIIsDarkThemeActive();
void ATUISetDarkThemeActive(bool enabled);

void ATUIInitThemes();
void ATUIShutdownThemes();

void ATUINotifyThemeChanged();
void ATUIRegisterThemeChangeNotification(const vdfunction<void()> *fn);
void ATUIUnregisterThemeChangeNotification(const vdfunction<void()> *fn);

void ATUIUpdateThemeColors();

// Theme color collection
//
// These are common colors used for UI rendering, many of which can be
// but are not necessarily based on system colors. All colors are in
// standard #RRGGBB form, which is also the standard for our platform-
// agnostic rendering APIs, but requires swizzling for Win32 GDI.
//
// The "foreground" colors are used for both text and monochromatic line
// art, such as line dividers or simple icons.
//
// These colors are preferred over Win32 system colors, as we must
// override as many as we can to force dark mode, since as of this writing
// Windows 10 provides no assistance for doing so for Win32 programs.
//
struct ATUIThemeColors {
	// Colors for static content, e.g. dialogs.
	uint32 mStaticBg;
	uint32 mStaticFg;

	// Colors for disabled content, i.e. disabled button.
	uint32 mDisabledFg;

	// Colors for primary/editable content, such as a text edit control.
	uint32 mContentBg;
	uint32 mContentFg;

	// Colors for highlights in a focused control.
	uint32 mHighlightedBg;
	uint32 mHighlightedFg;

	// Colors for item highlights in a non-focused control, for those that
	// still show the highlight.
	uint32 mInactiveHiBg;
	uint32 mInactiveHiFg;

	uint32 mInactiveTextBg;

	// Active and inactive caption bar colors. Gradients run horizontally
	// from Caption1 on the left to Caption2 on the right. If caption
	// gradients are disabled in the system, the caption colors are the same.
	uint32 mActiveCaption1;
	uint32 mActiveCaption2;
	uint32 mActiveCaptionText;
	uint32 mInactiveCaption1;
	uint32 mInactiveCaption2;
	uint32 mInactiveCaptionText;

	// Color for monochromatic icons in the caption bar, like the close button
	// for dockable panes.
	uint32 mCaptionIcon;

	// Color for clickable text links (typically blue).
	uint32 mHyperlinkText;

	// Color for comment text in syntax-highlighted text views (typically green).
	uint32 mCommentText;

	// Color for special keywords / directives (typically blue).
	uint32 mKeywordText;

	// Color for headings/titles.
	uint32 mHeadingText;

	// Direct location when stepping (nominally black on yellow).
	uint32 mDirectLocationFg;
	uint32 mDirectLocationBg;

	// Direct location when stepping (nominally black on green).
	uint32 mIndirectLocationFg;
	uint32 mIndirectLocationBg;

	// Strong marker / breakpoint (nominally black on red).
	uint32 mHiMarkedFg;
	uint32 mHiMarkedBg;

	uint32 mPendingHiMarkedFg;
	uint32 mPendingHiMarkedBg;

	uint32 mHardPosEdge;
	uint32 mSoftPosEdge;
	uint32 mSoftNegEdge;
	uint32 mHardNegEdge;
	uint32 mButtonBg;
	uint32 mButtonPushedBg;
	uint32 mButtonCheckedBg;
	uint32 mButtonFg;

	uint32 mFocusedBg;
};

const ATUIThemeColors& ATUIGetThemeColors();

#endif
