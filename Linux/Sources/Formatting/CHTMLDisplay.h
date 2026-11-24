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
#include "CLog.h"

#include "htmlview/Document.h"
#include "htmlview/Fetcher.h"

#include <JXScrollableWidget.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class CAttachment;
class CMessage;
class CMessageView;
class CMessageWindow;
class JXScrollbarSet;
struct SFontInfo;
namespace htmlview { class ImageCache; }

class CHTMLDisplay : public CCommander,		// Commander must be first so it gets destroyed last
					 public JXScrollableWidget,
					 public htmlview::Host,
					 public htmlview::FetchHost
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
	// above it).  Images it refers to are looked for among the parts of msg
	// ("cid:" Content-ID, or file name); remote ones are only taken from the
	// memory cache, unless LoadRemoteImages() was called for msg.  Image
	// parts it does not refer to are shown below it (as Thunderbird does).
	// Returns false when the viewer rejects the document (too large, too
	// many elements, nested too deeply): show it as text.
	bool	SetContent(const unichar_t* html, const char* header, const SFontInfo& font, long font_scale,
					   CMessage* msg);
	void	ClearContent();

	// Scrolls one page down (up if shift_key) if possible
	bool	SpacebarScroll(bool shift_key);

	// Scroll position (top left of the visible area)
	void	GetScrollPos(long& h, long& v) const;
	void	SetScrollPos(long h, long v);

	// Remote images (LITEHTML-VIEWER-PLAN.md, stage 5): never loaded unless
	// asked for, per message, with LoadRemoteImages() (the "Load Remote
	// Images" button); those already downloaded in this session come from
	// the memory cache, without network access.  The button's label, and
	// whether it is shown and can be clicked:
	//   "Load Remote Images: XX"      XX images not loaded yet
	//   "Loading Remote Images: n/XX" n of XX received
	//   "Loaded Remote Images: YY"    YY images loaded and drawn
	cdstring	GetRemoteImagesLabel(bool& visible, bool& enabled) const;
	void		LoadRemoteImages();

	// The part of a message to show as HTML: the main text part if it is
	// HTML, or the HTML alternative of the main text; NULL if none
	static CAttachment* FindHTMLPart(CMessage* msg);

	// CCommander
	virtual bool	HandleChar(const int key, const JXKeyModifiers& modifiers);

	// htmlview::Host: images in the mail
	virtual bool	LoadImageData(const std::string& url, std::string& data);

	// htmlview::FetchHost: remote images downloaded, network log
	virtual void	FetchDone(const std::string& url, bool ok, const std::string& data,
							  const std::string& error);
	virtual bool	LogEnabled() const;
	virtual void	Log(const std::string& text);

	// Downloads in progress (called by an idle task)
	void	PumpRemoteImages();

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
	std::unique_ptr<htmlview::Document>	mAttachments;		// attached images, below mDocument
	int									mDocumentHeight;	// where mAttachments starts
	CMessageView*						mMsgView;
	CMessageWindow*						mMsgWindow;
	JCoordinate							mLayoutWidth;
	// While SetContent() loads the documents
	CMessage*							mImageMessage;
	std::map<CAttachment*, std::string>	mPartData;			// decoded image parts read
	std::set<CAttachment*>				mReferencedParts;	// parts the HTML refers to
	std::vector<CAttachment*>			mAttachedImages;	// parts shown below the HTML

	bool	ReadPartData(CAttachment* part, std::string& data);
	std::string	AttachedImagesHTML(CMessage* msg, const htmlview::Settings& settings);

	// Remote images of mDocument
	class CRemoteImagesTask;
	std::unique_ptr<htmlview::Fetcher>	mFetcher;			// while downloading
	CRemoteImagesTask*					mRemoteTask;		// pumps mFetcher
	CMessage*							mRemoteMessage;		// the message shown (only compared)
	bool								mRemoteRequested;	// its images were asked for
	std::set<std::string>				mLoadedURLs;		// remote images given to mDocument
	size_t								mFetchTotal;		// images being downloaded
	size_t								mFetchDone;			// of them, finished
	bool								mRelayoutPending;	// images arrived: lay out again
	bool								mRedrawPending;		// images arrived: draw again
	unsigned long						mLastRelayout;		// time (ms) of the last one
	mutable CLog						mNetworkLog;		// Mulberry's HTTP log

	bool	ApplyCachedRemoteImages();						// true: lay out again
	void	StopRemoteImages();
	void	RemoteImagesChanged();							// tells the message view/window
	static htmlview::ImageCache&	RemoteImageCache();		// for the session, all views

	void	Layout();
	static double	FontSizeInPixels(Display* display, double points);
};

#endif	// HTML_VIEWER

#endif
