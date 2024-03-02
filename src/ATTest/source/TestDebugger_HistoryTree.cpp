#include <stdafx.h>
#include <tuple>
#include <vd2/system/vdalloc.h>
#include <vd2/system/vdstl.h>
#include <at/atdebugger/historytree.h>
#include <at/atdebugger/historytreebuilder.h>
#include <test.h>

#include <stdafx.h>
#include <vd2/system/function.h>
#include <test.h>

namespace {
	namespace MatchHistoryTree {
		template<typename... Children>
		class BaseMatchNode {
		public:
			BaseMatchNode(const Children& ...args)
				: mArgs(args...)
			{
			}

		protected:
			uint32 Match1(const ATHTNode& node, uint32 insnOffset) const {
				return Match2(node, insnOffset, std::make_index_sequence<sizeof...(Children)>());
			}

		private:
			template<size_t... I>
			uint32 Match2(const ATHTNode& node, uint32 insnOffset, std::index_sequence<I...>) const {
				const ATHTNode *chain = node.mpFirstChild;

				(void)(((insnOffset = std::get<I>(mArgs).Match(*chain, insnOffset)), (chain = chain->mpNextSibling)), ...);

				TEST_ASSERT(chain == nullptr);

				return insnOffset;
			}

			std::tuple<Children...> mArgs;
		};

		template<typename... Children>
		class label final : public BaseMatchNode<Children...> {
		public:
			label(const Children& ...args)
				: BaseMatchNode<Children...>(args...)
			{
			}

			uint32 Match(const ATHTNode& node, uint32 insnOffset = 0) const {
				TEST_ASSERT(node.mNodeType == kATHTNodeType_Label);

				return this->Match1(node, insnOffset);
			}
		};

		template<typename... Children>
		class repeat final : public BaseMatchNode<Children...> {
		public:
			repeat(uint32 count, uint32 size, const Children& ...args)
				: BaseMatchNode<Children...>(args...)
				, mCount(count)
				, mSize(size)
			{
			}

			uint32 Match(const ATHTNode& node, uint32 insnOffset = 0) const {
				TEST_ASSERT(node.mNodeType == kATHTNodeType_Repeat);
				TEST_ASSERT(node.mRepeat.mCount == mCount);
				TEST_ASSERT(node.mRepeat.mSize == mSize);

				return this->Match1(node, insnOffset);
			}

		private:
			uint32 mCount;
			uint32 mSize;
		};

		template<typename... Children>
		class insns final : public BaseMatchNode<Children...> {
		public:
			insns(uint32 count, const Children& ...args)
				: BaseMatchNode<Children...>(args...)
				, mCount(count)
			{
			}

			uint32 Match(const ATHTNode& node, uint32 insnOffset = 0) const {
				TEST_ASSERT(node.mNodeType == kATHTNodeType_Insn);
				TEST_ASSERT(node.mInsn.mOffset == insnOffset);
				TEST_ASSERT(node.mInsn.mCount == mCount);

				return this->Match1(node, insnOffset + mCount);
			}

		private:
			uint32 mCount;
		};
	}
}


DEFINE_TEST(Debugger_HistoryTree) {
	using namespace MatchHistoryTree;

	{
		ATHistoryTree tree;
		vdautoptr<ATHistoryTreeBuilder> builder(new ATHistoryTreeBuilder);

		builder->Init(&tree);
		builder->SetCollapseLoops(false);
		builder->SetCollapseCalls(false);
		builder->SetCollapseInterrupts(false);

		builder->BeginUpdate(false);

		static constexpr ATHistoryTraceInsn kInsns[] = {
			{ 0x1000, false, false, 0xFF, 0xEA, 0 },
			{ 0x1001, false, false, 0xFF, 0xAD, 0 },
			{ 0x1004, false, false, 0xFF, 0x8D, 0 },
		};

		builder->Update(kInsns, vdcountof(kInsns));

		ATHTNode *last;
		builder->EndUpdate(last);

		TEST_ASSERT(tree.Verify());

		auto *p = tree.GetRootNode()->mpFirstChild;

		const auto matchTree = MatchHistoryTree::insns(3);

		matchTree.Match(*p);

	}

	// test simple loop case
	{
		ATHistoryTree tree;
		vdautoptr<ATHistoryTreeBuilder> builder(new ATHistoryTreeBuilder);

		builder->Init(&tree);
		builder->SetCollapseLoops(true);
		builder->SetCollapseCalls(true);
		builder->SetCollapseInterrupts(true);

		builder->BeginUpdate(false);

		static constexpr ATHistoryTraceInsn kInsns[] = {
			// This pad instruction is necessary to allow the loop to be detected (this margin
			// is a small sacrifice to speed up the loop detector).
			{ 0x0FFF, false, false, 0xFF, 0xEA, 0 },	// NOP
			{ 0x1000, false, false, 0xFF, 0xA2, 0 },	// LDX
			{ 0x1001, false, false, 0xFF, 0xCA, 0 },	// DEX
			{ 0x1002, false, false, 0xFF, 0xD0, 0 },	// BNE
			{ 0x1001, false, false, 0xFF, 0xCA, 0 },	// DEX begin loop(3)
			{ 0x1002, false, false, 0xFF, 0xD0, 0 },	// BNE  |
			{ 0x1001, false, false, 0xFF, 0xCA, 0 },	// DEX  |
			{ 0x1002, false, false, 0xFF, 0xD0, 0 },	// BNE  |
			{ 0x1001, false, false, 0xFF, 0xCA, 0 },	// DEX  |
			{ 0x1002, false, false, 0xFF, 0xD0, 0 },	// BNE end loop
		};

		builder->Update(kInsns, vdcountof(kInsns));

		ATHTNode *last;
		builder->EndUpdate(last);

		TEST_ASSERT(tree.Verify());

		auto *p = tree.GetRootNode()->mpFirstChild;

		const auto matchTree =
			label(
				insns(4),
				repeat(3, 2,
					insns(6)
				)
			);

		matchTree.Match(*p);
	}

	// test compound loop case
	{
		ATHistoryTree tree;
		vdautoptr<ATHistoryTreeBuilder> builder(new ATHistoryTreeBuilder);

		builder->Init(&tree);
		builder->SetCollapseLoops(true);
		builder->SetCollapseCalls(true);
		builder->SetCollapseInterrupts(true);

		builder->BeginUpdate(false);

		static constexpr ATHistoryTraceInsn kInsns[] = {
			{ 0x1000, false, false, 0xFF, 0xA0, 0 },	// LDY
			{ 0x1002, false, false, 0xFF, 0xA2, 0 },	// LDX
			{ 0x1004, false, false, 0xFF, 0xCA, 0 },	// DEX
			{ 0x1005, false, false, 0xFF, 0xD0, 0 },	// BNE
			{ 0x1004, false, false, 0xFF, 0xCA, 0 },	// DEX begin loop(3)
			{ 0x1005, false, false, 0xFF, 0xD0, 0 },	// BNE  |
			{ 0x1004, false, false, 0xFF, 0xCA, 0 },	// DEX  |
			{ 0x1005, false, false, 0xFF, 0xD0, 0 },	// BNE  |
			{ 0x1004, false, false, 0xFF, 0xCA, 0 },	// DEX  |
			{ 0x1005, false, false, 0xFF, 0xD0, 0 },	// BNE end loop
			{ 0x1007, false, false, 0xFF, 0x88, 0 },	// DEY
			{ 0x1008, false, false, 0xFF, 0xD0, 0 },	// BNE
			{ 0x1002, false, false, 0xFF, 0xA2, 0 },	// LDX begin loop(3)
			{ 0x1004, false, false, 0xFF, 0xCA, 0 },	// DEX  |
			{ 0x1005, false, false, 0xFF, 0xD0, 0 },	// BNE  |
			{ 0x1004, false, false, 0xFF, 0xCA, 0 },	// DEX  |  begin loop(3)
			{ 0x1005, false, false, 0xFF, 0xD0, 0 },	// BNE  |   |
			{ 0x1004, false, false, 0xFF, 0xCA, 0 },	// DEX  |   |
			{ 0x1005, false, false, 0xFF, 0xD0, 0 },	// BNE  |   |
			{ 0x1004, false, false, 0xFF, 0xCA, 0 },	// DEX  |   |
			{ 0x1005, false, false, 0xFF, 0xD0, 0 },	// BNE  |  end loop
			{ 0x1007, false, false, 0xFF, 0x88, 0 },	// DEY  | 
			{ 0x1008, false, false, 0xFF, 0xD0, 0 },	// BNE  next
			{ 0x1002, false, false, 0xFF, 0xA2, 0 },	// LDX  |
			{ 0x1004, false, false, 0xFF, 0xCA, 0 },	// DEX  |
			{ 0x1005, false, false, 0xFF, 0xD0, 0 },	// BNE  |
			{ 0x1004, false, false, 0xFF, 0xCA, 0 },	// DEX  |  begin loop(3)
			{ 0x1005, false, false, 0xFF, 0xD0, 0 },	// BNE  |   |
			{ 0x1004, false, false, 0xFF, 0xCA, 0 },	// DEX  |   |
			{ 0x1005, false, false, 0xFF, 0xD0, 0 },	// BNE  |   |
			{ 0x1004, false, false, 0xFF, 0xCA, 0 },	// DEX  |   |
			{ 0x1005, false, false, 0xFF, 0xD0, 0 },	// BNE  |  end loop
			{ 0x1007, false, false, 0xFF, 0x88, 0 },	// DEY  |
			{ 0x1008, false, false, 0xFF, 0xD0, 0 },	// BNE  next
			{ 0x1002, false, false, 0xFF, 0xA2, 0 },	// LDX  |
			{ 0x1004, false, false, 0xFF, 0xCA, 0 },	// DEX  |
			{ 0x1005, false, false, 0xFF, 0xD0, 0 },	// BNE  |
			{ 0x1004, false, false, 0xFF, 0xCA, 0 },	// DEX  |  begin loop(3)
			{ 0x1005, false, false, 0xFF, 0xD0, 0 },	// BNE  |   |
			{ 0x1004, false, false, 0xFF, 0xCA, 0 },	// DEX  |   |
			{ 0x1005, false, false, 0xFF, 0xD0, 0 },	// BNE  |   |
			{ 0x1004, false, false, 0xFF, 0xCA, 0 },	// DEX  |   |
			{ 0x1005, false, false, 0xFF, 0xD0, 0 },	// BNE  |  end loop
			{ 0x1007, false, false, 0xFF, 0x88, 0 },	// DEY  |
			{ 0x1008, false, false, 0xFF, 0xD0, 0 },	// BNE end loop
		};

		builder->Update(kInsns, vdcountof(kInsns));

		ATHTNode *last;
		builder->EndUpdate(last);

		TEST_ASSERT(tree.Verify());

		auto *p = tree.GetRootNode()->mpFirstChild;

		const auto matchTree =
			label(
				insns(4),
				repeat(3, 2,
					insns(6)
				),
				insns(2),
				repeat(3, 11,
					insns(3),
					repeat(3, 2,
						insns(6)
					),
					insns(5),
					repeat(3, 2,
						insns(6)
					),
					insns(5),
					repeat(3, 2,
						insns(6)
					),
					insns(2)
				)
			);

		matchTree.Match(*p);
	}

	// test degenerate loop case
	{
		ATHistoryTree tree;
		vdautoptr<ATHistoryTreeBuilder> builder(new ATHistoryTreeBuilder);

		builder->Init(&tree);
		builder->SetCollapseLoops(true);
		builder->SetCollapseCalls(true);
		builder->SetCollapseInterrupts(true);

		builder->BeginUpdate(false);

		static constexpr ATHistoryTraceInsn kInsns[] = {
			{ 0xC0DF, false, false, 0xFF, 0x4C, 0 },	// JMP
			{ 0xC0DF, false, false, 0xFF, 0x4C, 0 },	// JMP
			{ 0xC0DF, false, false, 0xFF, 0x4C, 0 },	// JMP
			{ 0xC0DF, false, false, 0xFF, 0x4C, 0 },	// JMP
			{ 0xC0DF, false, false, 0xFF, 0x4C, 0 },	// JMP
			{ 0xC0DF, false, false, 0xFF, 0x4C, 0 },	// JMP
			{ 0xC0DF, false, false, 0xFF, 0x4C, 0 },	// JMP
			{ 0xC0DF, false, false, 0xFF, 0x4C, 0 },	// JMP
		};

		builder->Update(kInsns, vdcountof(kInsns));

		ATHTNode *last;
		builder->EndUpdate(last);

		TEST_ASSERT(tree.Verify());

		auto *p = tree.GetRootNode()->mpFirstChild;

		const auto matchTree =
			label(
				insns(1),
				repeat(7, 1,
					insns(7)
				)
			);

		matchTree.Match(*p);
	}

	// test subroutine collapsing
	{
		ATHistoryTree tree;
		vdautoptr<ATHistoryTreeBuilder> builder(new ATHistoryTreeBuilder);

		builder->Init(&tree);
		builder->SetCollapseLoops(true);
		builder->SetCollapseCalls(true);
		builder->SetCollapseInterrupts(true);

		builder->BeginUpdate(false);

		static constexpr ATHistoryTraceInsn kInsns[] = {
			{ 0x1000, false, false, 0xFF, 0xEA, 0 },	// NOP
			{ 0x1001, false, false, 0xFF, 0x48, 0 },	// PHA
			{ 0x1002, false, false, 0xFF, 0x68, 0 },	// PLA
			{ 0x1003, false, false, 0xFF, 0x20, 0 },	// JSR
			{ 0x2000, false, false, 0xFD, 0xAD, 0 },	// LDA
			{ 0x2003, false, false, 0xFD, 0x8D, 0 },	// STA
			{ 0x2006, false, false, 0xFD, 0x60, 0 },	// RTS
			{ 0x1006, false, false, 0xFF, 0x20, 0 },	// JSR
			{ 0x2000, false, false, 0xFD, 0xAD, 0 },	// LDA
			{ 0x2003, false, false, 0xFD, 0x8D, 0 },	// STA
			{ 0x2006, false, false, 0xFD, 0x60, 0 },	// RTS
			{ 0x1009, false, false, 0xFF, 0x20, 0 },	// JSR
			{ 0x3000, false, false, 0xFD, 0xA2, 0 },	// LDX
			{ 0x3002, false, false, 0xFD, 0xCA, 0 },	// DEX
			{ 0x3003, false, false, 0xFD, 0xD0, 0 },	// BNE
			{ 0x3002, false, false, 0xFD, 0xCA, 0 },	// DEX
			{ 0x3003, false, false, 0xFD, 0xD0, 0 },	// BNE
			{ 0x3002, false, false, 0xFD, 0xCA, 0 },	// DEX
			{ 0x3003, false, false, 0xFD, 0xD0, 0 },	// BNE
			{ 0x3002, false, false, 0xFD, 0xCA, 0 },	// DEX
			{ 0x3003, false, false, 0xFD, 0xD0, 0 },	// BNE
			{ 0x3005, false, false, 0xFD, 0x60, 0 },	// RTS
		};

		builder->Update(kInsns, vdcountof(kInsns));

		ATHTNode *last;
		builder->EndUpdate(last);

		TEST_ASSERT(tree.Verify());

		auto *p = tree.GetRootNode()->mpFirstChild;

		const auto matchTree =
			label(
				insns(3),			// NOP / PHA / PLA
				insns(1,			// JSR
					insns(3)
				),
				insns(1,			// JSR
					insns(3)
				),
				insns(1,			// JSR
					insns(3),
					repeat(3, 2,
						insns(6)
					),
					insns(1)
				)
			);

		matchTree.Match(*p);
	}

	return 0;
}
