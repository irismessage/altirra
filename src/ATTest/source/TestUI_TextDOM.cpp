#include <stdafx.h>
#include "test.h"
#include "textdom.h"

DEFINE_TEST(UI_TextDOM) {
	using namespace nsVDTextDOM;
	
	const auto IteratorAt = [](const Iterator& it, int para, int line, int offset) {
		return it.mPara == para && it.mLine == line && it.mOffset == offset;
	};

	struct HardWrapCallback final : public IDocumentCallback {
		void InvalidateRows(int ystart, int yend) override {
		}

		void VerticalShiftRows(int ysrc, int ydst) override {
		}

		void ReflowPara(int paraIdx, const Paragraph& para) override {
			size_t len = para.mText.size();

			if (len && para.mText.back() == '\n')
				--len;

			int lines = len ? (len + 3) / 4 : 1;
			mLines.resize(lines);

			for(int i=0; i<lines; ++i) {
				mLines[i] = Line { i*4, std::min<int>(4, len - 4*i), 1 };
			}

			mpDoc->ReflowPara(paraIdx, mLines.data(), lines);
		}

		void RecolorParagraph(int paraIdx, Paragraph& para) override {
		}

		void ChangeTotalHeight(int y) override {
		}

		Document *mpDoc;
		vdfastvector<Line> mLines;
	};

	HardWrapCallback cb;

	{
		Document doc;
		Iterator it(doc);
		Iterator after;

		// (it)[Hello, world!](after)
		doc.Insert(it, "Hello, world!", 13, &after);
		TEST_ASSERT(IteratorAt(it, 0, 0, 0));
		TEST_ASSERT(IteratorAt(after, 0, 0, 13));

		// (it)[foo
		// ](after2)Hello, world!(after)
		Iterator after2;
		doc.Insert(it, "foo\n", 4, &after2);
		TEST_ASSERT(IteratorAt(it, 0, 0, 0));
		TEST_ASSERT(IteratorAt(after, 1, 0, 13));
		TEST_ASSERT(IteratorAt(after2, 1, 0, 0));

		// (it)[
		// foo](after)f(it2)oo
		// (after2)Hello, world!
		Iterator it2(doc, 0, 0, 1);
		doc.Insert(it, "\nfoo", 4, &after);
		TEST_ASSERT(IteratorAt(it, 0, 0, 0));
		TEST_ASSERT(IteratorAt(it2, 1, 0, 4));
		TEST_ASSERT(IteratorAt(after2, 2, 0, 0));
		TEST_ASSERT(IteratorAt(after, 1, 0, 3));

		// (it)
		// foo(after)<f(it2)oo
		// >(after2)Hello, world!(end)
		Iterator end(doc, 2, 0, 13);
		doc.Delete(after, after2);
		// (it)
		// foo(after)(it2)(after2)Hello, world!(end)
		TEST_ASSERT(IteratorAt(it, 0, 0, 0));
		TEST_ASSERT(IteratorAt(after, 1, 0, 3));
		TEST_ASSERT(IteratorAt(it2, 1, 0, 3));
		TEST_ASSERT(IteratorAt(after2, 1, 0, 3));
		TEST_ASSERT(IteratorAt(end, 1, 0, 16));

		// (it)
		// foo(it2)(after2)[test1
		// test2
		// test3]Hello, world!(end)
		doc.Insert(it2, "test1\ntest2\ntest3", 17, &after);
		// (it)
		// foo(it2)(after2)test1
		// test2
		// test3(after)Hello, world!(end)
		TEST_ASSERT(IteratorAt(it, 0, 0, 0));
		TEST_ASSERT(IteratorAt(it2, 1, 0, 3));
		TEST_ASSERT(IteratorAt(after2, 1, 0, 3));
		TEST_ASSERT(IteratorAt(after, 3, 0, 5));
		TEST_ASSERT(IteratorAt(end, 3, 0, 18));
	}

	{
		Document doc;
		cb.mpDoc = &doc;
		doc.SetCallback(&cb);

		Iterator it(doc);
		Iterator after;

		// (it)[Hello, world!](after)
		doc.Insert(it, "Hello, world!", 13, &after);
		// (it)Hell
		//+o, w
		//+orld
		//!(after)
		TEST_ASSERT(IteratorAt(it, 0, 0, 0));
		TEST_ASSERT(IteratorAt(after, 0, 3, 1));

		// (it)[foo
		// ](after2)Hell
		//+o, w
		//+orld
		//+!(after)
		Iterator after2;
		doc.Insert(it, "foo\n", 4, &after2);
		// (it)foo
		// (after2)Hell
		//+o, w
		//+orld
		//+!(after)
		TEST_ASSERT(IteratorAt(it, 0, 0, 0));
		TEST_ASSERT(IteratorAt(after, 1, 3, 1));
		TEST_ASSERT(IteratorAt(after2, 1, 0, 0));

		// (it)[
		// foo](after)f
		//+(it2)oo
		// (after2)Hell
		//+o, w
		//+orld
		//+!
		Iterator it2(doc, 0, 0, 1);
		doc.Insert(it, "\nfoo", 4, &after);
		// (it)
		// foo(after)f
		//+(it2)oo
		// (after2)Hell
		//+o, w
		//+orld
		//+!
		TEST_ASSERT(IteratorAt(it, 0, 0, 0));
		TEST_ASSERT(IteratorAt(it2, 1, 1, 0));
		TEST_ASSERT(IteratorAt(after2, 2, 0, 0));
		TEST_ASSERT(IteratorAt(after, 1, 0, 3));

		// (it)
		// foo(after)<f(it2)oo
		// >(after2)Hell
		//+o, w
		//+orld
		//+!(end)
		Iterator end(doc, 2, 3, 1);
		doc.Delete(after, after2);
		// (it)
		// foo(after)(it2)(after2)H
		//+ello
		//+, wo
		//+rld!(end)
		TEST_ASSERT(IteratorAt(it, 0, 0, 0));
		TEST_ASSERT(IteratorAt(after, 1, 0, 3));
		TEST_ASSERT(IteratorAt(it2, 1, 0, 3));
		TEST_ASSERT(IteratorAt(after2, 1, 0, 3));
		TEST_ASSERT(IteratorAt(end, 1, 3, 4));

		// (it)
		// foo(it2)(after2)[test1
		// test2
		// test3]Hello, world!(end)
		doc.Insert(it2, "test1\ntest2\ntest3", 17, &after);
		// (it)
		// foo(it2)(after2)t
		//+est1
		// test
		//+2
		// test
		//+3(after)Hel
		//+lo, <>
		//+worl
		//+d!(end)
		TEST_ASSERT(IteratorAt(it, 0, 0, 0));
		TEST_ASSERT(IteratorAt(it2, 1, 0, 3));
		TEST_ASSERT(IteratorAt(after2, 1, 0, 3));
		TEST_ASSERT(IteratorAt(after, 3, 1, 1));
		TEST_ASSERT(IteratorAt(end, 3, 4, 2));
	}

	return 0;
}
