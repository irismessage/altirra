//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2021 Avery Lee
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

#ifndef f_AT_UPDATEFEED_H
#define f_AT_UPDATEFEED_H

#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>

struct ATXMLSubsetHashedStr {
	uint32 mHash;
	const char *mpStr;
	size_t mLen;

	bool operator==(const ATXMLSubsetHashedStr& other) const {
		return mHash == other.mHash && mLen == other.mLen && !memcmp(mpStr, other.mpStr, mLen);
	}

	bool operator!=(const ATXMLSubsetHashedStr& other) const {
		return mHash != other.mHash || mLen != other.mLen || memcmp(mpStr, other.mpStr, mLen);
	}

	template<size_t N>
	consteval ATXMLSubsetHashedStr(const char (&s)[N])
		: ATXMLSubsetHashedStr(&s[0], N - 1)
	{
	}

	constexpr ATXMLSubsetHashedStr(const char *s, size_t n) {
		mHash = 0;
		mpStr = s;
		mLen = n;

		for(size_t i=0; i<n; ++i)
			mHash = (mHash * 33) + (unsigned char)s[i];
	}
};

struct ATXMLSubsetHashedStrHash {
	size_t operator()(const ATXMLSubsetHashedStr& v) const {
		return (size_t)v.mHash;
	}
};

enum class ATUpdateFeedName : uint32 {
	CDATA,
	Invalid
};

struct ATUpdateFeedDocNode {
	ATUpdateFeedName mName;
	uint32 mNextOffset;
	union {
		uint32 mFirstChild;
		uint32 mTextOffset;
	};
	union {
		uint32 mAttributeCount;
		uint32 mTextLength;
	};
};

struct ATUpdateFeedNodeIter;

struct ATUpdateFeedNodeRef {
	const ATUpdateFeedDocNode *mpNode = nullptr;

	bool operator==(const ATUpdateFeedNodeRef& other) const { return mpNode == other.mpNode; }
	bool operator!=(const ATUpdateFeedNodeRef& other) const { return mpNode != other.mpNode; }

	operator bool() const {
		return mpNode != nullptr;
	}

	ATUpdateFeedNodeRef& operator++() {
		if (mpNode->mNextOffset)
			mpNode += mpNode->mNextOffset;
		else
			mpNode = nullptr;

		return *this;
	}

	ATUpdateFeedNodeRef operator++(int) {
		auto p = *this;

		++*this;

		return p;
	}

	bool IsCDATA() const { return mpNode && mpNode->mName == ATUpdateFeedName::CDATA; }
	bool IsElement(ATUpdateFeedName name) const { return mpNode && mpNode->mName == name; }

	ATUpdateFeedNodeRef GetAttribute(ATUpdateFeedName name) const;

	ATUpdateFeedNodeRef operator*() const {
		return mpNode && mpNode->mName != ATUpdateFeedName::CDATA && mpNode->mFirstChild ? ATUpdateFeedNodeRef{ mpNode + mpNode->mFirstChild } : ATUpdateFeedNodeRef{ nullptr };
	}

	ATUpdateFeedNodeRef operator/(ATUpdateFeedName name) const;

	inline ATUpdateFeedNodeIter begin() const;
	inline ATUpdateFeedNodeIter end() const;
};

struct ATUpdateFeedNodeIter {
	bool operator==(const ATUpdateFeedNodeIter& other) const { return mRef == other.mRef; }
	bool operator!=(const ATUpdateFeedNodeIter& other) const { return mRef != other.mRef; }

	ATUpdateFeedNodeRef operator*() const { return mRef; }

	ATUpdateFeedNodeIter& operator++() {
		++mRef;
		return *this;
	}

	ATUpdateFeedNodeIter operator++(int) {
		return ATUpdateFeedNodeIter{mRef++};
	}

	ATUpdateFeedNodeRef mRef;
};

inline ATUpdateFeedNodeIter ATUpdateFeedNodeRef::begin() const {
	return ATUpdateFeedNodeIter(**this);
}

inline ATUpdateFeedNodeIter ATUpdateFeedNodeRef::end() const {
	return ATUpdateFeedNodeIter();
}

struct ATUpdateFeedDoc {
	bool IsEmpty() const { return mNodes.empty(); }
	ATUpdateFeedNodeRef GetRoot() const { return {mNodes.data()}; }
	VDStringSpanA GetText(const ATUpdateFeedNodeRef& nodeRef) const {
		return nodeRef.IsCDATA() ? VDStringSpanA(mTextBuffer.data() + nodeRef.mpNode->mTextOffset, mTextBuffer.data() + nodeRef.mpNode->mTextOffset + nodeRef.mpNode->mTextLength) : VDStringSpanA();
	}

	VDStringSpanA GetAttributeValue(const ATUpdateFeedNodeRef& elementRef, ATUpdateFeedName attrName) const {
		auto nodeRef = elementRef.GetAttribute(attrName);
		return nodeRef ? VDStringSpanA(mTextBuffer.data() + nodeRef.mpNode->mTextOffset, mTextBuffer.data() + nodeRef.mpNode->mTextOffset + nodeRef.mpNode->mTextLength) : VDStringSpanA();
	}

	ATUpdateFeedName GetNameToken(const ATXMLSubsetHashedStr& hs) const;

	ATUpdateFeedName CreateNameToken(const char *s) {
		return CreateNameToken(ATXMLSubsetHashedStr(s, strlen(s)));
	}

	ATUpdateFeedName CreateNameToken(const char *s, size_t n) {
		return CreateNameToken(ATXMLSubsetHashedStr(s, n));
	}

	ATUpdateFeedName CreateNameToken(const ATXMLSubsetHashedStr& hs);

	vdhashmap<ATXMLSubsetHashedStr, ATUpdateFeedName, ATXMLSubsetHashedStrHash> mNameTable;
	vdfastvector<ATUpdateFeedDocNode> mNodes;
	vdblock<char> mTextBuffer;
};

struct ATUpdateFeedItem {
	VDStringW mTitle;
	VDStringW mLink;
	ATUpdateFeedDoc mDoc;
};

struct ATUpdateFeedInfo {
public:
	VDStringW mCurrentVersionStr;
	VDStringW mLatestVersionStr;
	uint64 mCurrentVersion;
	uint64 mLatestVersion;
	ATUpdateFeedItem mLatestReleaseItem;
	ATUpdateFeedItem mEndOfLifeItem;

	bool Parse(const void *data, size_t len);
};

#endif
