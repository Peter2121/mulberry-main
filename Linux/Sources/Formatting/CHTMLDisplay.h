/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// CHTMLDisplay: shows a message part as HTML ("Format: as HTML") with the
// HTML viewer core (Libraries/HTMLView: litehtml, Cairo, HarfBuzz), drawing
// with Cairo on the JX window.  Read-only: no selection yet.
//
// Built only with the HTML viewer (configure --enable-html-viewer, the
// default, defines HTML_VIEWER); without it Mulberry keeps its text
// rendering of HTML.

#ifndef __CHTMLDISPLAY__MULBERRY__
#define __CHTMLDISPLAY__MULBERRY__

#ifdef HTML_VIEWER

#include "CCommander.h"

#include <JXScrollableWidget.h>

#include <memory>

namespace htmlview
{
	class Document;
}

class CAttachment;
class CMessage;
class CMessageView;
class CMessageWindow;
class JXScrollbarSet;
struct SFontInfo;

class CHTMLDisplay : public CCommander,		// Commander must be first so it gets destroyed last
					 public JXScrollableWidget
{
public:
	CHTMLDisplay(JXContainer* enclosure,
				 const HSizingOption hSizing, const VSizingOption vSizing,
				 const JCoordinate x, const JCoordinate y,
				 const JCoordinate w, const JCoordinate h);
	virtual ~CHTMLDisplay();

	void	SetMessageView(CMessageView* view)
		{ mMsgView = view; }
	void	SetMessageWindow(CMessageWindow* wnd)
		{ mMsgWindow = wnd; }

	// The scrollbar set enclosing this view: shown and hidden, moved and
	// resized, instead of the view itself
	JXScrollbarSet*	GetScroller() const
		{ return mScroller; }

	// Shows HTML: the text of a message part (UTF-16, as decoded by
	// Mulberry), after an optional header text (UTF-8, shown as plain text
	// above it).  Returns false when the viewer rejects the document
	// (too large, too many elements, nested too deeply): show it as text.
	bool	SetContent(const unichar_t* html, const char* header, const SFontInfo& font, long font_scale);
	void	ClearContent();

	// Scrolls one page down (up if shift_key) if possible
	bool	SpacebarScroll(bool shift_key);

	// Scroll position (top left of the visible area)
	void	GetScrollPos(long& h, long& v) const;
	void	SetScrollPos(long h, long v);

	// The part of a message to show as HTML: the main text part if it is
	// HTML, or the HTML alternative of the main text; NULL if none
	static CAttachment* FindHTMLPart(CMessage* msg);

	// CCommander
	virtual bool	HandleChar(const int key, const JXKeyModifiers& modifiers);

protected:
	virtual void	Draw(JXWindowPainter& p, const JRect& rect);
	virtual void	DrawBorder(JXWindowPainter& p, const JRect& frame);
	virtual void	ApertureResized(const JCoordinate dw, const JCoordinate dh);

	virtual void	HandleMouseDown(const JPoint& pt, const JXMouseButton button,
									const JSize clickCount,
									const JXButtonStates& buttonStates,
									const JXKeyModifiers& modifiers);
	virtual void	HandleKeyPress(const int key, const JXKeyModifiers& modifiers);
	virtual void	HandleFocusEvent();
	virtual void	HandleWindowFocusEvent();

private:
	JXScrollbarSet*						mScroller;
	std::unique_ptr<htmlview::Document>	mDocument;
	CMessageView*						mMsgView;
	CMessageWindow*						mMsgWindow;
	JCoordinate							mLayoutWidth;

	void	Layout();
	static double	FontSizeInPixels(Display* display, double points);
};

#endif	// HTML_VIEWER

#endif
