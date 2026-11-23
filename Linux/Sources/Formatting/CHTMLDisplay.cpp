/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
*/

// CHTMLDisplay: see CHTMLDisplay.h

#include "CHTMLDisplay.h"

#ifdef HTML_VIEWER

#include "CAttachment.h"
#include "CAttachmentList.h"
#include "CFocusBorder.h"
#include "CMessage.h"
#include "CMessageView.h"
#include "CMessageWindow.h"
#include "CScrollbarSet.h"
#include "CWindowStatesFwd.h"
#include "cdustring.h"

#include "htmlview/Document.h"

#include <cairo.h>
#include <cairo-xlib.h>

#include <JXColormap.h>
#include <JXDisplay.h>
#include <JXScrollbar.h>
#include <JXWindow.h>
#include <JXWindowPainter.h>
#include <jASCIIConstants.h>
#include <jXKeysym.h>

#include <X11/Xlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{

// JXWindowPainter keeps its drawable (the window, or its buffer pixmap
// when the window is double buffered) protected.  A pointer to the member,
// taken in a derived class, reads it without changing JX.
class CPainterDrawable : public JXWindowPainter
{
public:
	static Drawable Get(const JXWindowPainter& p)
	{
		return (p.*(&CPainterDrawable::GetDrawable))();
	}
};

// Header text (UTF-8) as an HTML block placed before the message's HTML;
// the parser moves it into the document's body.  font_size: in pixels.
std::string HeaderHTML(const char* header, double font_size)
{
	char size[32];
	std::snprintf(size, sizeof(size), "%.1fpx", font_size);
	std::string html = "<div style=\"display: block; white-space: pre-wrap; margin: 0 0 8px 0; "
					   "padding: 0 0 8px 0; border-bottom: 1px solid #c0c0c0; color: #000000; "
					   "background: #ffffff; text-align: left; font-style: normal; font-weight: normal; "
					   "line-height: normal; font-size: ";
	html += size;
	html += "\">";
	for (const char* p = header; *p; p++)
	{
		switch (*p)
		{
		case '&':
			html += "&amp;";
			break;
		case '<':
			html += "&lt;";
			break;
		case '>':
			html += "&gt;";
			break;
		case '\r':
			break;
		default:
			html += *p;
			break;
		}
	}
	// No empty line at the end of the block
	while (!html.empty() && (html.back() == '\n'))
		html.pop_back();
	html += "</div>";
	return html;
}

// The scrollbar set is created while the base class is constructed (it
// needs it), and then kept by the constructor
JXScrollbarSet* sCreatedScroller = NULL;

JXScrollbarSet* CreateScroller(JXContainer* enclosure,
							   const JXWidget::HSizingOption hSizing, const JXWidget::VSizingOption vSizing,
							   const JCoordinate x, const JCoordinate y,
							   const JCoordinate w, const JCoordinate h)
{
	sCreatedScroller = new CScrollbarSet(enclosure, hSizing, vSizing, x, y, w, h);
	return sCreatedScroller;
}

bool IsHTMLPart(const CAttachment* part)
{
	const CMIMEContent& content = part->GetContent();
	return (content.GetContentType() == eContentText) &&
		   (content.GetContentSubtype() == eContentSubHTML) &&
		   (content.GetContentDisposition() != eContentDispositionAttachment);
}

CAttachment* FindHTML(CAttachment* part)
{
	if (!part)
		return NULL;

	CAttachmentList* parts = part->GetParts();
	if (!parts)
		return IsHTMLPart(part) ? part : NULL;

	if (part->GetContent().GetContentSubtype() == eContentSubAlternative)
	{
		// The richest alternative comes last
		for (CAttachmentList::reverse_iterator iter = parts->rbegin(); iter != parts->rend(); iter++)
		{
			CAttachment* found = FindHTML(*iter);
			if (found)
				return found;
		}
		return NULL;
	}

	// Other multiparts (mixed, related, signed, ...): the main text is in
	// the first part that has something to display; HTML in later parts is
	// an attachment
	for (CAttachmentList::iterator iter = parts->begin(); iter != parts->end(); iter++)
	{
		if ((*iter)->FirstDisplayPart())
			return FindHTML(*iter);
	}
	return NULL;
}

}	// namespace

CHTMLDisplay::CHTMLDisplay(JXContainer* enclosure,
						   const HSizingOption hSizing, const VSizingOption vSizing,
						   const JCoordinate x, const JCoordinate y,
						   const JCoordinate w, const JCoordinate h)
	: CCommander(enclosure),
	  JXScrollableWidget(CreateScroller(enclosure, hSizing, vSizing, x, y, w, h),
						 enclosure, hSizing, vSizing, 0, 0, w, h),
	  mScroller(sCreatedScroller),
	  mMsgView(NULL),
	  mMsgWindow(NULL),
	  mLayoutWidth(-1)
{
	sCreatedScroller = NULL;

	// Move into the scroll area of the scrollbar set
	SetEnclosure(mScroller->GetScrollEnclosure());
	FitToEnclosure();
	// No border inside a focus border (as CTextDisplay)
	if (dynamic_cast<CFocusBorder*>(enclosure))
		mScroller->SetBorderWidth(0);

	SetBorderWidth(0);
	// Keys, including Tab (next new message in the preview)
	WantInput(kTrue, kTrue);
	SetBackColor(GetColormap()->GetWhiteColor());
	SetFocusColor(GetColormap()->GetWhiteColor());
}

CHTMLDisplay::~CHTMLDisplay()
{
}

double CHTMLDisplay::FontSizeInPixels(Display* display, double points)
{
	// As Xft converts the point sizes JX gives it: the Xft.dpi resource,
	// else the screen's physical resolution
	double dpi = 0;
	const char* resource = XGetDefault(display, "Xft", "dpi");
	if (resource)
		dpi = std::atof(resource);
	if (dpi <= 0)
	{
		int screen = DefaultScreen(display);
		if (DisplayHeightMM(display, screen) > 0)
			dpi = DisplayHeight(display, screen) * 25.4 / DisplayHeightMM(display, screen);
	}
	if (dpi <= 0)
		dpi = 96;
	return points * dpi / 72.0;
}

bool CHTMLDisplay::SetContent(const unichar_t* html, const char* header, const SFontInfo& font, long font_scale)
{
	Display* display = *GetDisplay();

	htmlview::Settings settings;
	if (!font.fontname.empty())
		settings.default_font_family = font.fontname.c_str();
	double points = std::max(1.0, std::pow(1.2, (double) font_scale) * (font.size ? font.size : 12));
	settings.default_font_size = FontSizeInPixels(display, points);
	settings.screen_width = DisplayWidth(display, DefaultScreen(display));
	settings.screen_height = DisplayHeight(display, DefaultScreen(display));

	std::string text;
	// The header one size step smaller than the text (as the font size
	// buttons' "-1")
	if (header && *header)
		text = HeaderHTML(header, settings.default_font_size / 1.2);
	if (html)
	{
		cdustring utf16(html);
		cdstring utf8 = utf16.ToUTF8();
		text.append(utf8.c_str(), utf8.length());
	}

	std::unique_ptr<htmlview::Document> document(new htmlview::Document(settings));
	if (document->Load(text) != htmlview::Document::Result::Ok)
	{
		ClearContent();
		return false;
	}

	mDocument = std::move(document);
	mLayoutWidth = -1;
	Layout();
	ScrollTo(0, 0);
	Refresh();
	return true;
}

void CHTMLDisplay::ClearContent()
{
	mDocument.reset();
	mLayoutWidth = -1;
	Layout();
	Refresh();
}

// Lays the document out for the visible width, if it changed
void CHTMLDisplay::Layout()
{
	const JCoordinate width = GetApertureWidth();
	if (!mDocument)
	{
		SetBounds(width, GetApertureHeight());
		return;
	}
	if (width == mLayoutWidth)
		return;

	mLayoutWidth = width;
	const int height = mDocument->Layout(std::max(width, (JCoordinate) 50));
	SetBounds(std::max(width, (JCoordinate) mDocument->GetWidth()), std::max(height, 1));
	Refresh();
}

void CHTMLDisplay::ApertureResized(const JCoordinate dw, const JCoordinate dh)
{
	JXScrollableWidget::ApertureResized(dw, dh);
	Layout();
}

void CHTMLDisplay::Draw(JXWindowPainter& p, const JRect& rect)
{
	// Cairo draws on the painter's drawable: the window, or its buffer
	// pixmap, both in window coordinates and at the window's depth
	Drawable drawable = CPainterDrawable::Get(p);
	if (drawable == None)
		return;
	const JRect window_bounds = GetWindow()->GetBounds();
	cairo_surface_t* surface = cairo_xlib_surface_create(*GetDisplay(), drawable, GetColormap()->GetVisual(),
														 window_bounds.width(), window_bounds.height());
	cairo_t* cr = cairo_create(surface);

	// Local coordinates (the document's) to window coordinates, clipped to
	// what is to be drawn
	const JPoint& origin = p.GetOrigin();
	cairo_translate(cr, origin.x, origin.y);
	JRect clip;
	if (!JIntersection(rect, p.GetClipRect(), &clip))
	{
		cairo_destroy(cr);
		cairo_surface_destroy(surface);
		return;
	}
	cairo_rectangle(cr, clip.left, clip.top, clip.width(), clip.height());
	cairo_clip(cr);

	// Mail without its own background is shown on white
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_paint(cr);

	if (mDocument)
	{
		htmlview::Rect area;
		area.x = clip.left;
		area.y = clip.top;
		area.width = clip.width();
		area.height = clip.height();
		mDocument->Draw(cr, 0, 0, area);
	}

	cairo_destroy(cr);
	cairo_surface_flush(surface);
	cairo_surface_destroy(surface);
}

void CHTMLDisplay::DrawBorder(JXWindowPainter& p, const JRect& frame)
{
}

void CHTMLDisplay::HandleMouseDown(const JPoint& pt, const JXMouseButton button,
								   const JSize clickCount,
								   const JXButtonStates& buttonStates,
								   const JXKeyModifiers& modifiers)
{
	if (button == kJXLeftButton)
		Focus();

	// Mouse wheel
	JXScrollableWidget::HandleMouseDown(pt, button, clickCount, buttonStates, modifiers);
}

void CHTMLDisplay::HandleKeyPress(const int key, const JXKeyModifiers& modifiers)
{
	if (!HandleChar(key, modifiers))
		// Home, End, Page Up/Down, arrows
		JXScrollableWidget::HandleKeyPress(key, modifiers);
}

// The keys of the text view (CSpacebarEditView)
bool CHTMLDisplay::HandleChar(const int key, const JXKeyModifiers& modifiers)
{
	switch (key)
	{
	// Scroll down, then to the next message
	case ' ':
		{
			JXScrollbar* hScrollbar = NULL;
			JXScrollbar* vScrollbar = NULL;
			GetScrollbars(&hScrollbar, &vScrollbar);
			if (!vScrollbar || !vScrollbar->IsVisible() || vScrollbar->IsAtMax())
			{
				// Control key => delete and read next
				if (modifiers.control())
				{
					if (mMsgWindow)
						mMsgWindow->OnMessageDeleteRead();
					else if (mMsgView)
						mMsgView->OnMessageDeleteRead();
				}
				else if (modifiers.shift())
				{
					if (mMsgWindow)
						mMsgWindow->OnMessageReadPrev();
					else if (mMsgView)
						mMsgView->OnMessageReadPrev();
				}
				else
				{
					if (mMsgWindow)
						mMsgWindow->OnMessageReadNext();
					else if (mMsgView)
						mMsgView->OnMessageReadNext();
				}
			}
			else
				JXScrollableWidget::HandleKeyPress(XK_Page_Down, modifiers);
		}
		return true;

	// Select next new in preview
	case '\t':
		if (mMsgView)
		{
			mMsgView->OnMessageReadNextNew();
			return true;
		}
		break;

	// Delete and next
	case kJDeleteKey:
	case kJForwardDeleteKey:
		if (mMsgWindow)
			mMsgWindow->OnMessageDeleteRead();
		else if (mMsgView)
			mMsgView->OnMessageDeleteRead();
		return true;

	default:;
	}

	// Single key shortcuts of the message view or window
	return CCommander::HandleChar(key, modifiers);
}

bool CHTMLDisplay::SpacebarScroll(bool shift_key)
{
	JXScrollbar* hScrollbar = NULL;
	JXScrollbar* vScrollbar = NULL;
	GetScrollbars(&hScrollbar, &vScrollbar);
	if (vScrollbar && vScrollbar->IsVisible())
	{
		if (shift_key && !vScrollbar->IsAtMin())
		{
			vScrollbar->StepPage(-1.0);
			return true;
		}
		if (!shift_key && !vScrollbar->IsAtMax())
		{
			vScrollbar->StepPage(+1.0);
			return true;
		}
	}
	return false;
}

void CHTMLDisplay::GetScrollPos(long& h, long& v) const
{
	const JRect aperture = GetAperture();
	h = aperture.left;
	v = aperture.top;
}

void CHTMLDisplay::SetScrollPos(long h, long v)
{
	ScrollTo(h, v);
}

void CHTMLDisplay::HandleFocusEvent()
{
	JXScrollableWidget::HandleFocusEvent();

	// Make it the commander target
	SetTarget(this);
}

void CHTMLDisplay::HandleWindowFocusEvent()
{
	JXScrollableWidget::HandleWindowFocusEvent();

	// Make it the commander target
	SetTarget(this);
}

CAttachment* CHTMLDisplay::FindHTMLPart(CMessage* msg)
{
	return (msg && msg->GetBody()) ? FindHTML(msg->GetBody()) : NULL;
}

#endif	// HTML_VIEWER
