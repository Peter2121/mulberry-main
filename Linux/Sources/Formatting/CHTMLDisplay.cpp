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
#include "CFilter.h"
#include "CFocusBorder.h"
#include "CLog.h"
#include "CMIMESupport.h"
#include "CMbox.h"
#include "CMboxProtocol.h"
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
#include <cstring>
#include <string>
#include <vector>

#include <strings.h>

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

// ---------------------------------------------------------------------------
// Images in the mail
// ---------------------------------------------------------------------------

// The encoded size of an image part above which it is not read (the viewer
// itself refuses more than 16 MB decoded)
const unsigned long cMaxImagePartSize = 24 * 1024 * 1024;

// The URLs of attached images shown below the message: a Content-ID no mail
// part has ("cid:" + prefix + index + "@mulberry.invalid")
const char* const cAttachedImagePrefix = "cid:mulberry-attached-image-";

std::string EscapeHTML(const char* text)
{
	std::string html;
	for (const char* p = text; p && *p; p++)
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
		case '"':
			html += "&quot;";
			break;
		default:
			html += *p;
			break;
		}
	}
	return html;
}

// Collects the decoded bytes of a part (Mulberry's filters write to an LStream)
class CImageStream : public LStream
{
public:
	explicit CImageStream(size_t max_bytes) : mMaxBytes(max_bytes), mTooLarge(false) {}

	virtual ExceptionCode PutBytes(const void* inBuffer, SInt32& ioByteCount)
	{
		if (mData.size() + ioByteCount > mMaxBytes)
			mTooLarge = true;
		else
			mData.append(static_cast<const char*>(inBuffer), ioByteCount);
		return noErr;
	}

	std::string	mData;
	size_t		mMaxBytes;
	bool		mTooLarge;
};

// The parts that hold data (not multiparts); message/rfc822 parts are not
// entered: their images belong to them
void CollectParts(CAttachment* part, std::vector<CAttachment*>& parts)
{
	if (!part)
		return;
	CAttachmentList* children = part->GetParts();
	if (children)
	{
		for (CAttachmentList::iterator iter = children->begin(); iter != children->end(); iter++)
			CollectParts(*iter, parts);
	}
	else
		parts.push_back(part);
}

std::string PercentDecode(const std::string& text)
{
	std::string out;
	for (size_t i = 0; i < text.size(); i++)
	{
		if ((text[i] == '%') && (i + 2 < text.size()) && isxdigit((unsigned char) text[i + 1]) &&
			isxdigit((unsigned char) text[i + 2]))
		{
			out += (char) strtol(text.substr(i + 1, 2).c_str(), NULL, 16);
			i += 2;
		}
		else
			out += text[i];
	}
	return out;
}

// A Content-ID without its angle brackets and surrounding spaces
std::string BareContentId(const cdstring& id)
{
	std::string bare = id.c_str();
	size_t first = bare.find_first_not_of(" \t<");
	size_t last = bare.find_last_not_of(" \t>");
	return (first == std::string::npos) ? std::string() : bare.substr(first, last - first + 1);
}

// The part an image URL refers to: "cid:" a Content-ID (RFC 2392), else a
// relative name matched against the parts' file names
CAttachment* FindImagePart(CMessage* msg, const std::string& url)
{
	std::vector<CAttachment*> parts;
	CollectParts(msg->GetBody(), parts);

	if ((url.size() > 4) && (strncasecmp(url.c_str(), "cid:", 4) == 0))
	{
		std::string id = PercentDecode(url.substr(4));
		for (std::vector<CAttachment*>::iterator iter = parts.begin(); iter != parts.end(); iter++)
		{
			if (BareContentId((*iter)->GetContent().GetContentId()) == id)
				return *iter;
		}
		// Some mailers change the case
		for (std::vector<CAttachment*>::iterator iter = parts.begin(); iter != parts.end(); iter++)
		{
			if (strcasecmp(BareContentId((*iter)->GetContent().GetContentId()).c_str(), id.c_str()) == 0)
				return *iter;
		}
		return NULL;
	}

	// A name: the last path segment, without query or fragment
	std::string name = url.substr(0, url.find_first_of("?#"));
	size_t slash = name.find_last_of('/');
	if (slash != std::string::npos)
		name.erase(0, slash + 1);
	name = PercentDecode(name);
	if (name.empty())
		return NULL;
	for (std::vector<CAttachment*>::iterator iter = parts.begin(); iter != parts.end(); iter++)
	{
		if ((name == (*iter)->GetName().c_str()) || (name == (*iter)->GetContent().GetMappedName().c_str()))
			return *iter;
	}
	for (std::vector<CAttachment*>::iterator iter = parts.begin(); iter != parts.end(); iter++)
	{
		if ((strcasecmp(name.c_str(), (*iter)->GetName().c_str()) == 0) ||
			(strcasecmp(name.c_str(), (*iter)->GetContent().GetMappedName().c_str()) == 0))
			return *iter;
	}
	return NULL;
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
	  mDocumentHeight(0),
	  mMsgView(NULL),
	  mMsgWindow(NULL),
	  mLayoutWidth(-1),
	  mImageMessage(NULL)
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

bool CHTMLDisplay::SetContent(const unichar_t* html, const char* header, const SFontInfo& font, long font_scale,
							  CMessage* msg)
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

	// The documents ask for their images while they load and lay out: the
	// message is only used then (LoadImageData())
	mImageMessage = msg;
	mPartData.clear();
	mReferencedParts.clear();
	mAttachedImages.clear();

	std::unique_ptr<htmlview::Document> document(new htmlview::Document(settings, this));
	bool loaded = (document->Load(text) == htmlview::Document::Result::Ok);

	// Image parts the HTML does not refer to: a second document below it
	std::unique_ptr<htmlview::Document> attachments;
	if (loaded && msg)
	{
		std::string attached = AttachedImagesHTML(msg, settings);
		if (!attached.empty())
		{
			attachments.reset(new htmlview::Document(settings, this));
			if (attachments->Load(attached) != htmlview::Document::Result::Ok)
				attachments.reset();
		}
	}

	if (loaded)
	{
		mDocument = std::move(document);
		mAttachments = std::move(attachments);
		mLayoutWidth = -1;
		Layout();
	}

	mImageMessage = NULL;
	mPartData.clear();
	mReferencedParts.clear();
	mAttachedImages.clear();

	if (!loaded)
	{
		ClearContent();
		return false;
	}
	ScrollTo(0, 0);
	Refresh();
	return true;
}

// The image parts of a message the HTML did not refer to, as an HTML
// document: each under a caption with its name, as wide as the view at most
std::string CHTMLDisplay::AttachedImagesHTML(CMessage* msg, const htmlview::Settings& settings)
{
	std::vector<CAttachment*> parts;
	CollectParts(msg->GetBody(), parts);

	char size[32];
	std::snprintf(size, sizeof(size), "%.1fpx", settings.default_font_size / 1.2);
	std::string images;
	for (std::vector<CAttachment*>::iterator iter = parts.begin(); iter != parts.end(); iter++)
	{
		CAttachment* part = *iter;
		if ((part->GetContent().GetContentType() != eContentImage) || mReferencedParts.count(part) ||
			(part->GetContent().GetContentSize() > cMaxImagePartSize))
			continue;

		// Only images the viewer can show (format, size limits)
		std::string data;
		if (!ReadPartData(part, data) || !htmlview::IsDisplayableImage(data, settings))
			continue;

		cdstring name = part->GetName();
		if (name.empty())
			name = part->GetContent().GetMappedName();
		char url[96];
		std::snprintf(url, sizeof(url), "%s%lu@mulberry.invalid", cAttachedImagePrefix,
					  (unsigned long) mAttachedImages.size());
		mAttachedImages.push_back(part);

		images += "<div style=\"border-top: 1px solid #c0c0c0; margin: 12px 0 4px 0; padding: 4px 0 0 0\">";
		images += EscapeHTML(name.c_str());
		images += "</div><img src=\"";
		images += url;
		images += "\" style=\"display: block; max-width: 100%; height: auto; border: 0\">";
	}
	if (images.empty())
		return images;

	return std::string("<html><body style=\"margin: 8px; color: #808080; font-size: ") + size + "\">" +
		   images + "</body></html>";
}

void CHTMLDisplay::ClearContent()
{
	mDocument.reset();
	mAttachments.reset();
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
	const JCoordinate layout_width = std::max(width, (JCoordinate) 50);
	mDocumentHeight = mDocument->Layout(layout_width);
	JCoordinate bounds_width = std::max(width, (JCoordinate) mDocument->GetWidth());
	int height = mDocumentHeight;
	if (mAttachments)
	{
		height += mAttachments->Layout(layout_width);
		bounds_width = std::max(bounds_width, (JCoordinate) mAttachments->GetWidth());
	}
	SetBounds(bounds_width, std::max(height, 1));
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
		if (mAttachments && (clip.bottom > mDocumentHeight))
			mAttachments->Draw(cr, 0, mDocumentHeight, area);
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

// An image of the document: a part of the message, decoded (base64,
// quoted-printable) by Mulberry's filters and read from the server if needed
bool CHTMLDisplay::LoadImageData(const std::string& url, std::string& data)
{
	if (!mImageMessage || !mImageMessage->GetBody())
		return false;

	// An attached image shown below the message
	const size_t prefix_length = std::strlen(cAttachedImagePrefix);
	if (url.compare(0, prefix_length, cAttachedImagePrefix) == 0)
	{
		unsigned long index = std::strtoul(url.c_str() + prefix_length, NULL, 10);
		return (index < mAttachedImages.size()) && ReadPartData(mAttachedImages[index], data);
	}

	CAttachment* part = FindImagePart(mImageMessage, url);
	if (!part)
		return false;
	mReferencedParts.insert(part);
	return ReadPartData(part, data);
}

// The decoded data of an image part (cached while SetContent() runs)
bool CHTMLDisplay::ReadPartData(CAttachment* part, std::string& data)
{
	std::map<CAttachment*, std::string>::const_iterator cached = mPartData.find(part);
	if (cached != mPartData.end())
	{
		data = cached->second;
		return !data.empty();
	}

	if (!mImageMessage || part->IsMultipart() || part->IsMessage() ||
		(part->GetContent().GetContentType() == eContentText) ||
		(part->GetContent().GetContentSize() > cMaxImagePartSize))
		return false;

	// Parts are read through the top message's mailbox
	CMessage* top = mImageMessage;
	while (top->GetOwner())
		top = top->GetOwner();
	if (!top->GetMbox())
		return false;

	bool use_binary = top->GetMbox()->GetProtocol() && top->GetMbox()->GetProtocol()->HasBinary();
	CFilter* filter = CMIMESupport::GetFilter(part, true, use_binary);
	if (!filter)
		return false;
	CImageStream stream(cMaxImagePartSize);
	filter->SetStream(&stream);
	filter->SetForNetwork(lendl);

	bool done = false;
	try
	{
		top->GetMbox()->ReadAttachment(top, part, filter, true);
		done = !stream.mTooLarge && !stream.mData.empty();
	}
	catch (...)
	{
		// A network error: no image (the message view reports the error
		// on its next operation)
		CLOG_LOGCATCH(...);
	}
	delete filter;

	// Kept (also when it failed) for the second document and for the
	// attached images
	if (!done)
		stream.mData.clear();
	mPartData[part] = stream.mData;
	data.swap(stream.mData);
	return done;
}

CAttachment* CHTMLDisplay::FindHTMLPart(CMessage* msg)
{
	return (msg && msg->GetBody()) ? FindHTML(msg->GetBody()) : NULL;
}

#endif	// HTML_VIEWER
