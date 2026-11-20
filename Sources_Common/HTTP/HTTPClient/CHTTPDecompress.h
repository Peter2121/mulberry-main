/*
    Copyright (c) 2026 Mulberry contributors. All rights reserved.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/


// CHTTPDecompress.h — HTTP Content-Encoding decompression streambuf

#ifndef __CHTTPDECOMPRESS__MULBERRY__
#define __CHTTPDECOMPRESS__MULBERRY__

#include <zlib.h>

#ifdef HAVE_BROTLI
#include <brotli/decode.h>
#endif

#ifdef HAVE_ZSTD
#include <zstd.h>
#endif

#include <memory>
#include <ostream>
#include <streambuf>

namespace http {

class inflate_streambuf : public std::streambuf
{
public:
	enum EEncoding
	{
		eNone = 0,
		eGzip,
		eDeflate,
		eBrotli,
		eZstd
	};

	inflate_streambuf(std::ostream& output, EEncoding encoding);
	~inflate_streambuf();

protected:
	virtual int overflow(int c);
	virtual int sync();

private:
	static const size_t cBufSize = 8192;

	std::ostream&	mOutput;
	EEncoding		mEncoding;

	z_stream		mZStream;
	bool			mZStreamInit;

#ifdef HAVE_BROTLI
	BrotliDecoderState*	mBrotliState;
#endif

#ifdef HAVE_ZSTD
	ZSTD_DStream*	mZstdState;
#endif

	char			mInBuf[cBufSize];
	size_t			mInBufLen;
	size_t			mTotalIn;
	size_t			mTotalOut;

	void Flush();
	void FlushZlib();
	void FlushBrotli();
	void FlushZstd();
};

// Writes everything to a main stream and, if not NULL, a second one
class tee_streambuf : public std::streambuf
{
public:
	tee_streambuf(std::ostream& output, std::ostream* copy)
		: mOutput(output), mCopy(copy) {}

protected:
	virtual int overflow(int c);
	virtual std::streamsize xsputn(const char* s, std::streamsize n);
	virtual int sync();

private:
	std::ostream&	mOutput;
	std::ostream*	mCopy;
};

// Destination for the body of one HTTP response.
//
// A compressed body is a single compressed stream, however many chunks it
// arrives in (Transfer-Encoding: chunked), so it must go through one
// decompressor for the whole response. Raw bytes from the network are written
// to stream(); raw_log() is the stream to log those raw bytes to. For a
// compressed body raw_log() is NULL and the decompressed text is written to the
// log instead, so protocol logs stay readable.
//
// Call finish() after the last byte: it decodes what is left and throws
// CGeneralException if the compressed data was corrupt. (std::ostream turns
// exceptions thrown while writing into badbit, so errors would otherwise be
// silently lost and the body truncated.)
class response_body
{
public:
	response_body(std::ostream& output, inflate_streambuf::EEncoding encoding, std::ostream* log);

	std::ostream& stream()
	{
		return mDecoderStream ? *mDecoderStream : mOutput;
	}
	std::ostream* raw_log() const
	{
		return mDecoder ? NULL : mLog;
	}

	void finish();

private:
	std::ostream&						mOutput;
	std::ostream*						mLog;

	// Declaration order matters: the decoder writes into the tee, so it must be destroyed first
	std::unique_ptr<tee_streambuf>		mTee;
	std::unique_ptr<std::ostream>		mTeeStream;
	std::unique_ptr<inflate_streambuf>	mDecoder;
	std::unique_ptr<std::ostream>		mDecoderStream;

	response_body(const response_body&);
	response_body& operator=(const response_body&);
};

}	// namespace http

#endif	// __CHTTPDECOMPRESS__MULBERRY__
