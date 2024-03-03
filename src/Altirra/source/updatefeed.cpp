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

#include "stdafx.h"
#include "updatefeed.h"
#include "version.h"

bool ATUpdateVerifyFeedSignature(const void *signature, const void *data, size_t len);

struct ATBase64DecodingTable {
	uint8 mLookup[256];

	constexpr ATBase64DecodingTable()
		: mLookup()
	{
		for(uint8& v : mLookup)
			v = 128;

		for(int i=0; i<26; ++i) {
			mLookup['A' + i] = i;
			mLookup['a' + i] = i + 26;
		}

		for(int i=0; i<10; ++i)
			mLookup['0' + i] = i + 52;

		mLookup[+'+'] = 62;
		mLookup[+'/'] = 63;
		mLookup[+'='] = 64;
	}
};

constexpr ATBase64DecodingTable kATBase64DecodingTable {};

bool ATDecodeBase64(uint8 *dst, size_t dstLen, const char *src, size_t srcLen) {
	if (srcLen & 3)
		return false;

	size_t numFullGroups = dstLen / 3;
	size_t numGroups = srcLen >> 2;
	if ((dstLen + 2) / 3 != numGroups)
		return false;

	if (!dstLen)
		return true;

	for(size_t i = 0; i < numFullGroups; ++i) {
		uint8 v0 = kATBase64DecodingTable.mLookup[(unsigned char)src[0]];
		uint8 v1 = kATBase64DecodingTable.mLookup[(unsigned char)src[1]];
		uint8 v2 = kATBase64DecodingTable.mLookup[(unsigned char)src[2]];
		uint8 v3 = kATBase64DecodingTable.mLookup[(unsigned char)src[3]];
		uint8 chk = v0 | v1 | v2 | v3;

		if (chk & 0x80)
			return false;

		uint32 v
			= ((uint32)v0 << 18)
			+ ((uint32)v1 << 12)
			+ ((uint32)v2 <<  6)
			+ ((uint32)v3 <<  0);

		dst[0] = (uint8)(v >> 16);
		dst[1] = (uint8)(v >>  8);
		dst[2] = (uint8)(v >>  0);

		src += 4;
		dst += 3;
	}

	size_t dstRem = dstLen % 3;
	if (dstRem) {
		uint8 v0 = kATBase64DecodingTable.mLookup[(unsigned char)src[0]];
		uint8 v1 = kATBase64DecodingTable.mLookup[(unsigned char)src[1]];
		uint8 v2 = kATBase64DecodingTable.mLookup[(unsigned char)src[2]];
		uint8 v3 = kATBase64DecodingTable.mLookup[(unsigned char)src[3]];
		uint8 chk = v0 | v1 | v2 | v3;

		if (chk & 0x80)
			return false;

		if (v3 < 0x40)
			return false;

		dst[0] = (v0 << 2) + (v1 >> 4);

		if (dstRem >= 2)
			dst[1] = (v1 << 4) + (v2 >> 2);
		else if (v2 < 0x40)
			return false;
	}

	return true;
}

ATUpdateFeedNodeRef ATUpdateFeedNodeRef::GetAttribute(ATUpdateFeedName name) const {
	if (!mpNode || mpNode->mName == ATUpdateFeedName::CDATA)
		return {};

	for(uint32 i = 0; i < mpNode->mAttributeCount; ++i) {
		if (mpNode[i+1].mName == name)
			return {&mpNode[i+1]};
	}

	return {};
}

ATUpdateFeedNodeRef ATUpdateFeedNodeRef::operator/(ATUpdateFeedName name) const {
	if (!mpNode || mpNode->mName == ATUpdateFeedName::CDATA || !mpNode->mFirstChild)
		return {};

	const auto *p = mpNode + mpNode->mFirstChild;
	for(;;) {
		if (p->mName == name)
			return {p};

		if (!p->mNextOffset)
			return {};

		p += p->mNextOffset;
	}
}

ATUpdateFeedName ATUpdateFeedDoc::GetNameToken(const ATXMLSubsetHashedStr& hs) const {
	auto it = mNameTable.find(hs);

	return it != mNameTable.end() ? it->second : ATUpdateFeedName::Invalid;
}

ATUpdateFeedName ATUpdateFeedDoc::CreateNameToken(const ATXMLSubsetHashedStr& hs) {
	auto r = mNameTable.insert({hs, ATUpdateFeedName::CDATA});
	if (r.second)
		r.first->second = (ATUpdateFeedName)(mNameTable.size() + 1);

	return r.first->second;
}

class ATUpdateFeedParser {
public:
	bool Parse(const void *p, size_t n, bool useHeader, ATUpdateFeedDoc& doc);

private:
	ATUpdateFeedDocNode ParseCDATA(char term);
	bool ParseName(ATUpdateFeedName& name);
	bool ParseExpected(char c);
	bool ParseExpected(const char *s, size_t n);
	bool ParseChar(char& c);
	bool ParseWhitespace();
	bool VerifySignature(const char *sigStart, size_t sigLen, const char *hashStart, size_t hashLen);

	char *mpSrc = nullptr;
	char *mpSrcStart = nullptr;
	char *mpSrcEnd = nullptr;
	ATUpdateFeedDoc *mpDoc = nullptr;
};

bool ATUpdateFeedParser::Parse(const void *p, size_t n, bool useHeader, ATUpdateFeedDoc& doc) {
	mpDoc = &doc;
	doc.mTextBuffer.resize(n);
	if (n)
		memcpy(doc.mTextBuffer.data(), p, n);

	mpSrc = doc.mTextBuffer.data();
	mpSrcStart = mpSrc;
	mpSrcEnd = mpSrc + n;

	if (useHeader) {
		// Parse XML declaration and signature header.
		//
		// This is designed to minimize the amount of code that must execute on
		// untrusted data. We require an exact form of XML declaration followed by
		// a comment containing a Base64-encoded RSASSA signature, which contains a
		// signed SHA256 hash of the entire document with the signature comment
		// replaced with spaces. This means that we do not do any complex tag
		// parsing until after the signature has been validated from a trusted
		// source.
		//
		static constexpr char kSigHeader[] = "<?xml version=\"1.0\" encoding=\"utf-8\"?><!-- sig:";
		if (!ParseExpected(kSigHeader, (sizeof kSigHeader) - 1))
			return false;

		char *sigStart = mpSrc;

		for(;;) {
			char c;
			if (!ParseChar(c))
				return false;

			if (c == ' ')
				break;
		}

		char *sigEnd = mpSrc - 1;

		if (!ParseExpected("-->", 3))
			return false;

		vdfastvector<char> sigBuf(sigStart, sigEnd);

		const size_t sigLen = (size_t)(sigEnd - sigStart);
		memset(sigStart - 9, ' ', sigLen + 13);

		if (!VerifySignature(sigBuf.data(), sigBuf.size(), mpSrcStart, (size_t)(mpSrcEnd - mpSrcStart)))
			return false;
	}

	// parse element tree
	struct ElementPointer {
		sint32 mIndex;
		sint32 mSiblingTail;
	};

	vdfastvector<ElementPointer> elementStack;
	elementStack.emplace_back(ElementPointer{-1, -1});

	auto& nodes = mpDoc->mNodes;
	while(mpSrc != mpSrcEnd) {
		if (*mpSrc == '<') {
			++mpSrc;

			// check for end tag
			ATUpdateFeedName elementName;
			if (ParseExpected('/')) {
				// end tag
				if (!ParseName(elementName))
					return false;

				ParseWhitespace();
				if (!ParseExpected('>'))
					return false;

				if (elementStack.size() <= 1 || nodes[elementStack.back().mIndex].mName != elementName)
					return false;

				elementStack.pop_back();
			} else {
				// start or empty tag -- parse element name
				if (!ParseName(elementName))
					return false;

				// create new element node
				uint32 elementIndex = (uint32)nodes.size();
				nodes.emplace_back();

				ATUpdateFeedDocNode& element = nodes.back();
				element.mName = elementName;
				element.mNextOffset = 0;
				element.mFirstChild = 0;
				element.mAttributeCount = 0;

				if (!elementStack.empty()) {
					auto& tos = elementStack.back();
					if (tos.mSiblingTail >= 0)
						nodes[tos.mSiblingTail].mNextOffset = elementIndex - tos.mSiblingTail;
					else if (tos.mIndex >= 0)
						nodes[tos.mIndex].mFirstChild = elementIndex - tos.mIndex;
					tos.mSiblingTail = elementIndex;
				}

				// parse attributes
				uint32 attributeCount = 0;

				for(;;) {
					bool space = ParseWhitespace();

					if (ParseExpected("/>", 2))
						break;
					else if (ParseExpected('>')) {
						elementStack.emplace_back(ElementPointer{(sint32)elementIndex, -1});
						break;
					}

					if (!space)
						return false;

					ATUpdateFeedName attributeName;
					if (!ParseName(attributeName))
						return false;

					if (!ParseExpected('='))
						return false;

					const char delim = '"';
					if (!ParseExpected(delim))
						return false;

					ATUpdateFeedDocNode attributeNode = ParseCDATA(delim);
					attributeNode.mName = attributeName;
					nodes.emplace_back(attributeNode);

					if (!ParseExpected(delim))
						return false;

					++attributeCount;
				}

				nodes[elementIndex].mAttributeCount = attributeCount;
			}
		} else {
			if (elementStack.size() <= 1) {
				if (!ParseWhitespace())
					return false;
			} else {
				uint32 nodeIndex = (uint32)nodes.size();

				auto& tos = elementStack.back();
				if (tos.mSiblingTail >= 0)
					nodes[tos.mSiblingTail].mNextOffset = nodeIndex - tos.mSiblingTail;
				else if (tos.mIndex >= 0)
					nodes[tos.mIndex].mFirstChild = nodeIndex - tos.mIndex;
				tos.mSiblingTail = nodeIndex;

				nodes.emplace_back(ParseCDATA('<'));
			}
		}
	}

	return true;
}

ATUpdateFeedDocNode ATUpdateFeedParser::ParseCDATA(char term) {
	ATUpdateFeedDocNode node;
	node.mName = ATUpdateFeedName::CDATA;
	node.mNextOffset = 0;
	node.mTextOffset = (uint32)(mpSrc - mpSrcStart);

	char *dst0 = mpSrc;
	char *dst = mpSrc;

	while(mpSrc != mpSrcEnd) {
		char c = *mpSrc;

		if (c == term)
			break;

		++mpSrc;

		if (c == '&') {
			if (ParseExpected("lt;", 3))
				c = '<';
			else if (ParseExpected("gt;", 3))
				c = '>';
			else if (ParseExpected("amp;", 4))
				c = '&';
			else if (ParseExpected("quot;", 5))
				c = '"';
		}

		*dst++ = c;
	}

	node.mTextLength = (uint32)(dst - dst0);
	return node;
}

bool ATUpdateFeedParser::ParseName(ATUpdateFeedName& name) {
	const char *nameStart = mpSrc;

	char c;
	if (!ParseChar(c))
		return false;

	if ((unsigned char)((c & 0xDF) - 'A') >= 26 && c != '_')
		return false;

	while(mpSrc != mpSrcEnd) {
		c = *mpSrc;

		if ((unsigned char)(((unsigned char)c & 0xDF) - 'A') >= 26 && (unsigned char)((unsigned char)c - '0') >= 10 && c != '_' && c != '-' && c != '.')
			break;

		++mpSrc;
	}

	name = mpDoc->CreateNameToken(nameStart, (size_t)(mpSrc - nameStart));
	return true;
}

bool ATUpdateFeedParser::ParseExpected(char c) {
	if (mpSrc == mpSrcEnd || *mpSrc != c)
		return false;

	++mpSrc;
	return true;
}

bool ATUpdateFeedParser::ParseExpected(const char *s, size_t n) {
	if ((size_t)(mpSrcEnd - mpSrc) < n || memcmp(mpSrc, s, n))
		return false;

	mpSrc += n;
	return true;
}

bool ATUpdateFeedParser::ParseChar(char& c) {
	if (mpSrc == mpSrcEnd)
		return false;

	c = *mpSrc++;
	return true;
}


bool ATUpdateFeedParser::ParseWhitespace() {
	bool space = false;

	while(mpSrc != mpSrcEnd) {
		char c = *mpSrc;

		if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
			break;

		++mpSrc;
		space = true;
	}

	return space;
}

bool ATUpdateFeedParser::VerifySignature(const char *sigStart, size_t sigLen, const char *hashStart, size_t hashLen) {
	uint8 signature[256] {};

	if (!ATDecodeBase64(signature, sizeof signature, sigStart, sigLen))
		return false;

	return ATUpdateVerifyFeedSignature(signature, hashStart, hashLen);
}

////////////////////////////////////////////////////////////////////////////////

bool ATUpdateFeedInfo::Parse(const void *data, size_t len) {
	if (!len)
		return false;

	ATUpdateFeedDoc doc;
	ATUpdateFeedParser parser;
	if (!parser.Parse(data, len, true, doc))
		return false;

	mCurrentVersion = AT_VERSION_COMPARE_VALUE;

	auto elRss = doc.GetNameToken(ATXMLSubsetHashedStr("rss"));
	auto elChannel = doc.GetNameToken(ATXMLSubsetHashedStr("channel"));
	auto elItem = doc.GetNameToken(ATXMLSubsetHashedStr("item"));
	auto elTitle = doc.GetNameToken(ATXMLSubsetHashedStr("title"));
	auto elDescription = doc.GetNameToken(ATXMLSubsetHashedStr("description"));
	auto elCategory = doc.GetNameToken(ATXMLSubsetHashedStr("category"));
	auto elLink = doc.GetNameToken(ATXMLSubsetHashedStr("link"));

	auto root = doc.GetRoot();
	if (!root.IsElement(elRss))
		return false;

	auto channel = root/elChannel;

	for(auto&& itemNode : channel) {
		if (itemNode.IsElement(elItem)) {
			auto title = *(itemNode/elTitle);
			auto desc = *(itemNode/elDescription);
			auto category = *(itemNode/elCategory);

			if (!title.IsCDATA() || !desc.IsCDATA() || !category.IsCDATA())
				return false;

			const auto titleText = doc.GetText(title);
			const auto categoryText = doc.GetText(category);
			const auto descText = doc.GetText(desc);

			if (categoryText == "eol") {
				ATUpdateFeedParser parser2;
				if (!parser2.Parse(descText.data(), descText.size(), false, mEndOfLifeItem.mDoc))
					return false;

				mEndOfLifeItem.mTitle = VDTextU8ToW(titleText);
			} else if (categoryText == "release") {
				if (!mLatestReleaseItem.mDoc.IsEmpty())
					continue;

				ATUpdateFeedParser parser2;
				if (!parser2.Parse(descText.data(), descText.size(), false, mLatestReleaseItem.mDoc))
					return false;

				mLatestReleaseItem.mTitle = VDTextU8ToW(titleText);
				mLatestReleaseItem.mLink = VDTextU8ToW(doc.GetText(*(itemNode/elLink)));

				// parse the title for version info
				if (mLatestReleaseItem.mTitle.empty() || mLatestReleaseItem.mTitle.back() != L')')
					return false;

				auto it = mLatestReleaseItem.mTitle.end() - 1, itBegin = mLatestReleaseItem.mTitle.begin();

				while(it != itBegin && it[-1] != L'(')
					--it;

				if (it == itBegin)
					return false;

				auto itTitleEnd = it;
				if (itTitleEnd != itBegin && itTitleEnd[-1] == L'(') {
					--itTitleEnd;

					while(itTitleEnd != itBegin && itTitleEnd[-1] == L' ')
						--itTitleEnd;
				}

				// parse out semicolon delimited tokens
				VDStringRefW tokenList(it, mLatestReleaseItem.mTitle.end() - 1);
				bool haveVersion = false;

				while(!tokenList.empty()) {
					VDStringRefW token;
					if (!tokenList.split(L';', token)) {
						token = tokenList;
						tokenList.clear();
					}

					while(!token.empty() && token.front() == L' ')
						token = token.subspan(1);

					while(!token.empty() && token.back() == L' ')
						token = token.subspan(0, token.size() - 1);
					
					if (!haveVersion) {
						haveVersion = true;

						// parse x.y.z.w
						VDStringW s(token);
						wchar_t dummy;

						unsigned v1, v2, v3, v4;
						if (4 != swscanf(s.c_str(), L" %u.%u.%u.%u %c", &v1, &v2, &v3, &v4, &dummy))
							return false;

						mLatestVersion = ((uint64)v1 << 48)
							+ ((uint64)v2 << 32)
							+ ((uint64)v3 << 16)
							+ ((uint64)v4 <<  0);
					}
				}

				mLatestReleaseItem.mTitle.erase(itTitleEnd, mLatestReleaseItem.mTitle.end());
			}
		}
	}

	return true;
}
