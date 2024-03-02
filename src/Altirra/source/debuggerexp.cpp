#include "stdafx.h"
#include <ctype.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include "debuggerexp.h"
#include "cpu.h"
#include "cpumemory.h"
#include "debugger.h"
#include "antic.h"
#include "mmu.h"
#include "address.h"

namespace {
	void FreeNodes(vdfastvector<ATDebugExpNode *>& nodes) {
		while(!nodes.empty()) {
			delete nodes.back();
			nodes.pop_back();
		}
	}

	enum {
		kNodePrecOr,
		kNodePrecAnd,
		kNodePrecBitOr,
		kNodePrecBitXor,
		kNodePrecBitAnd,
		kNodePrecRel,
		kNodePrecAdd,
		kNodePrecMul,
		kNodePrecUnary
	};
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeConst : public ATDebugExpNode {
public:
	ATDebugExpNodeConst(sint32 v, bool hex, bool addr)
		: ATDebugExpNode(kATDebugExpNodeType_Const)
		, mVal(v)
		, mbHex(hex)
		, mbAddress(addr)
	{
	}

	bool IsHex() const { return mbHex; }
	bool IsAddress() const { return mbAddress; }

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		result = mVal;
		return true;
	}

	void ToString(VDStringA& s, int prec) {
		sint32 v = mVal;

		if (mbAddress) {
			switch((uint32)v & kATAddressSpaceMask) {
				case kATAddressSpace_ANTIC:
					s += "a:";
					break;

				case kATAddressSpace_PORTB:
					s += "x:";
					break;

				case kATAddressSpace_RAM:
					s += "r:";
					break;

				case kATAddressSpace_VBXE:
					s += "v:";
					break;
			}

			v &= kATAddressOffsetMask;
		}

		s.append_sprintf(mbHex ? mVal >= 0x100 ? mVal >= 0x10000 ? "$%08X" : "$%04X" : "$%02X" : "%d", mVal);
	}

protected:
	const sint32 mVal;
	const bool mbHex;
	const bool mbAddress;
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeUnary : public ATDebugExpNode {
public:
	ATDebugExpNodeUnary(ATDebugExpNodeType type, ATDebugExpNode *arg)
		: ATDebugExpNode(type)
		, mpArg(arg)
	{
	}

	bool Optimize(ATDebugExpNode **result) {
		ATDebugExpNode *newArg;

		if (mpArg->Optimize(&newArg))
			mpArg = newArg;

		if (mpArg->mType == kATDebugExpNodeType_Const) {
			sint32 v;

			if (Evaluate(v, ATDebugExpEvalContext())) {
				*result = new ATDebugExpNodeConst(v, static_cast<ATDebugExpNodeConst *>(&*mpArg)->IsHex(), false);
				return true;
			}
		}

		return false;	
	}

protected:
	void ToString(VDStringA& s, int prec) {
		EmitUnaryOp(s);
		mpArg->ToString(s, kNodePrecUnary);
	}

	virtual void EmitUnaryOp(VDStringA& s) = 0;

	vdautoptr<ATDebugExpNode> mpArg;
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeBinary : public ATDebugExpNode {
public:
	ATDebugExpNodeBinary(ATDebugExpNodeType type, ATDebugExpNode *left, ATDebugExpNode *right, bool inheritAddress)
		: ATDebugExpNode(type)
		, mpLeft(left)
		, mpRight(right)
		, mbAddress(inheritAddress && (left->IsAddress() || right->IsAddress()))
	{
	}

	bool IsAddress() const {
		return mbAddress;
	}

	bool Optimize(ATDebugExpNode **result) {
		ATDebugExpNode *newArg;
		if (mpLeft->Optimize(&newArg))
			mpLeft = newArg;

		if (mpRight->Optimize(&newArg))
			mpRight = newArg;

		if (mpLeft->mType == kATDebugExpNodeType_Const && mpRight->mType == kATDebugExpNodeType_Const) {
			sint32 v;

			if (Evaluate(v, ATDebugExpEvalContext())) {
				*result = new ATDebugExpNodeConst(v,
					static_cast<ATDebugExpNodeConst *>(&*mpLeft)->IsHex()
					|| static_cast<ATDebugExpNodeConst *>(&*mpRight)->IsHex(),
					mbAddress
					);
				return true;
			}
		}

		return false;	
	}

protected:
	void ToString(VDStringA& s, int prec) {
		int thisprec = GetPrecedence();
		int assoc = GetAssociativity();

		if (prec > thisprec)
			s += '(';

		mpLeft->ToString(s, assoc < 0 ? thisprec+1 : thisprec);

		EmitBinaryOp(s);

		mpRight->ToString(s, assoc > 0 ? thisprec+1 : thisprec);

		if (prec > thisprec)
			s += ')';
	}

	virtual int GetAssociativity() const { return -1; }
	virtual int GetPrecedence() const = 0;
	virtual void EmitBinaryOp(VDStringA& s) = 0;

	vdautoptr<ATDebugExpNode> mpLeft;
	vdautoptr<ATDebugExpNode> mpRight;
	bool mbAddress;
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeAnd : public ATDebugExpNodeBinary {
public:
	ATDebugExpNodeAnd(ATDebugExpNode *x, ATDebugExpNode *y)
		: ATDebugExpNodeBinary(kATDebugExpNodeType_And, x, y, false)
	{
	}

	bool Optimize(ATDebugExpNode **result) {
		if (ATDebugExpNodeBinary::Optimize(result))
			return true;

		if (mpLeft->mType == kATDebugExpNodeType_Const) {
			sint32 v;

			if (mpLeft->Evaluate(v, ATDebugExpEvalContext()) && !v) {
				*result = new ATDebugExpNodeConst(0, false, false);
				return true;
			}
		}

		if (mpRight->mType == kATDebugExpNodeType_Const) {
			sint32 v;

			if (mpRight->Evaluate(v, ATDebugExpEvalContext()) && !v) {
				*result = new ATDebugExpNodeConst(0, false, false);
				return true;
			}
		}

		return false;
	}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		sint32 x;
		sint32 y;

		if (!mpLeft->Evaluate(x, context) ||
			!mpRight->Evaluate(y, context))
			return false;

		result = x && y;
		return true;
	}

	bool ExtractEqConst(ATDebugExpNodeType type, ATDebugExpNode **extracted, ATDebugExpNode **remainder) {
		vdautoptr<ATDebugExpNode> rem;

		if (mpLeft->ExtractEqConst(type, extracted, ~rem)) {
			if (rem) {
				*remainder = new ATDebugExpNodeAnd(rem, mpRight);
				rem.release();
			} else
				*remainder = mpRight;

			mpLeft.reset();
			mpRight.release();
			return true;
		}

		if (mpRight->ExtractEqConst(type, extracted, ~rem)) {
			if (rem) {
				*remainder = new ATDebugExpNodeAnd(mpLeft, rem);
				rem.release();
			} else
				*remainder = mpLeft;

			mpLeft.release();
			mpRight.reset();
			return true;
		}

		return false;
	}

	bool ExtractRelConst(ATDebugExpNodeType type, ATDebugExpNode **extracted, ATDebugExpNode **remainder, ATDebugExpNodeType *relop) {
		vdautoptr<ATDebugExpNode> rem;

		if (mpLeft->ExtractRelConst(type, extracted, ~rem, relop)) {
			if (rem) {
				*remainder = new ATDebugExpNodeAnd(rem, mpRight);
				rem.release();
			} else
				*remainder = mpRight;

			mpLeft.reset();
			mpRight.release();
			return true;
		}

		if (mpRight->ExtractRelConst(type, extracted, ~rem, relop)) {
			if (rem) {
				*remainder = new ATDebugExpNodeAnd(mpLeft, rem);
				rem.release();
			} else
				*remainder = mpLeft;

			mpLeft.release();
			mpRight.reset();
			return true;
		}

		return false;
	}

	bool OptimizeInvert(ATDebugExpNode **result);

	bool CanOptimizeInvert() const {
		return mpLeft->CanOptimizeInvert() && mpRight->CanOptimizeInvert();
	}

	int GetAssociativity() const { return 0; }
	int GetPrecedence() const { return kNodePrecAnd; }
	void EmitBinaryOp(VDStringA& s) {
		s += " and ";
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeOr : public ATDebugExpNodeBinary {
public:
	ATDebugExpNodeOr(ATDebugExpNode *x, ATDebugExpNode *y)
		: ATDebugExpNodeBinary(kATDebugExpNodeType_Or, x, y, false)
	{
	}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		sint32 x;
		sint32 y;

		if (!mpLeft->Evaluate(x, context) ||
			!mpRight->Evaluate(y, context))
			return false;

		result = x || y;
		return true;
	}

	bool Optimize(ATDebugExpNode **result) {
		if (ATDebugExpNodeBinary::Optimize(result))
			return true;

		if (mpLeft->mType == kATDebugExpNodeType_Const) {
			sint32 v;

			if (mpLeft->Evaluate(v, ATDebugExpEvalContext()) && v) {
				*result = new ATDebugExpNodeConst(1, false, false);
				return true;
			}
		}

		if (mpRight->mType == kATDebugExpNodeType_Const) {
			sint32 v;

			if (mpRight->Evaluate(v, ATDebugExpEvalContext()) && v) {
				*result = new ATDebugExpNodeConst(1, false, false);
				return true;
			}
		}

		return false;
	}

	bool OptimizeInvert(ATDebugExpNode **result);

	bool CanOptimizeInvert() const {
		return mpLeft->CanOptimizeInvert() && mpRight->CanOptimizeInvert();
	}

	int GetAssociativity() const { return 0; }
	int GetPrecedence() const { return kNodePrecOr; }
	void EmitBinaryOp(VDStringA& s) {
		s += " or ";
	}
};

///////////////////////////////////////////////////////////////////////////

bool ATDebugExpNodeAnd::OptimizeInvert(ATDebugExpNode **result) {
	if (!CanOptimizeInvert())
		return false;


	ATDebugExpNode *newNode;
	VDVERIFY(mpLeft->OptimizeInvert(&newNode));
	mpLeft = newNode;

	VDVERIFY(mpRight->OptimizeInvert(&newNode));
	mpRight = newNode;

	*result = new ATDebugExpNodeOr(mpLeft, mpRight);
	mpLeft.release();
	mpRight.release();

	return true;
}

bool ATDebugExpNodeOr::OptimizeInvert(ATDebugExpNode **result) {
	if (!CanOptimizeInvert())
		return false;

	ATDebugExpNode *newNode;
	VDVERIFY(mpLeft->OptimizeInvert(&newNode));
	mpLeft = newNode;

	VDVERIFY(mpRight->OptimizeInvert(&newNode));
	mpRight = newNode;

	*result = new ATDebugExpNodeAnd(mpLeft, mpRight);
	mpLeft.release();
	mpRight.release();

	return true;
}

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeBitwiseAnd : public ATDebugExpNodeBinary {
public:
	ATDebugExpNodeBitwiseAnd(ATDebugExpNode *x, ATDebugExpNode *y)
		: ATDebugExpNodeBinary(kATDebugExpNodeType_BitwiseAnd, x, y, true)
	{
	}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		sint32 x;
		sint32 y;

		if (!mpLeft->Evaluate(x, context) ||
			!mpRight->Evaluate(y, context))
			return false;

		result = x & y;
		return true;
	}

	int GetAssociativity() const { return 0; }
	int GetPrecedence() const { return kNodePrecBitAnd; }
	void EmitBinaryOp(VDStringA& s) {
		s += " & ";
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeBitwiseOr : public ATDebugExpNodeBinary {
public:
	ATDebugExpNodeBitwiseOr(ATDebugExpNode *x, ATDebugExpNode *y)
		: ATDebugExpNodeBinary(kATDebugExpNodeType_BitwiseOr, x, y, true)
	{
	}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		sint32 x;
		sint32 y;

		if (!mpLeft->Evaluate(x, context) ||
			!mpRight->Evaluate(y, context))
			return false;

		result = x | y;
		return true;
	}

	int GetAssociativity() const { return 0; }
	int GetPrecedence() const { return kNodePrecBitOr; }
	void EmitBinaryOp(VDStringA& s) {
		s += " | ";
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeBitwiseXor : public ATDebugExpNodeBinary {
public:
	ATDebugExpNodeBitwiseXor(ATDebugExpNode *x, ATDebugExpNode *y)
		: ATDebugExpNodeBinary(kATDebugExpNodeType_BitwiseXor, x, y, true)
	{
	}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		sint32 x;
		sint32 y;

		if (!mpLeft->Evaluate(x, context) ||
			!mpRight->Evaluate(y, context))
			return false;

		result = x ^ y;
		return true;
	}

	int GetAssociativity() const { return 0; }
	int GetPrecedence() const { return kNodePrecBitXor; }
	void EmitBinaryOp(VDStringA& s) {
		s += " ^ ";
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeAdd : public ATDebugExpNodeBinary {
public:
	ATDebugExpNodeAdd(ATDebugExpNode *x, ATDebugExpNode *y)
		: ATDebugExpNodeBinary(kATDebugExpNodeType_Add, x, y, true)
	{
	}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		sint32 x;
		sint32 y;

		if (!mpLeft->Evaluate(x, context) ||
			!mpRight->Evaluate(y, context))
			return false;

		result = x + y;
		return true;
	}

	int GetAssociativity() const { return 0; }
	int GetPrecedence() const { return kNodePrecAdd; }
	void EmitBinaryOp(VDStringA& s) {
		s += '+';
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeSub : public ATDebugExpNodeBinary {
public:
	ATDebugExpNodeSub(ATDebugExpNode *x, ATDebugExpNode *y)
		: ATDebugExpNodeBinary(kATDebugExpNodeType_Sub, x, y, true)
	{
	}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		sint32 x;
		sint32 y;

		if (!mpLeft->Evaluate(x, context) ||
			!mpRight->Evaluate(y, context))
			return false;

		result = x - y;
		return true;
	}

	int GetPrecedence() const { return kNodePrecAdd; }
	void EmitBinaryOp(VDStringA& s) {
		s += '-';
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeMul : public ATDebugExpNodeBinary {
public:
	ATDebugExpNodeMul(ATDebugExpNode *x, ATDebugExpNode *y)
		: ATDebugExpNodeBinary(kATDebugExpNodeType_Mul, x, y, true)
	{
	}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		sint32 x;
		sint32 y;

		if (!mpLeft->Evaluate(x, context) ||
			!mpRight->Evaluate(y, context))
			return false;

		result = x * y;
		return true;
	}

	int GetAssociativity() const { return 0; }
	int GetPrecedence() const { return kNodePrecMul; }
	void EmitBinaryOp(VDStringA& s) {
		s += '*';
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeMod : public ATDebugExpNodeBinary {
public:
	ATDebugExpNodeMod(ATDebugExpNode *x, ATDebugExpNode *y)
		: ATDebugExpNodeBinary(kATDebugExpNodeType_Div, x, y, false)
	{
	}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		sint32 x;
		sint32 y;

		if (!mpLeft->Evaluate(x, context) ||
			!mpRight->Evaluate(y, context))
			return false;

		if (!y)
			return false;

		result = x % y;
		return true;
	}

	int GetPrecedence() const { return kNodePrecMul; }
	void EmitBinaryOp(VDStringA& s) {
		s += '%';
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeDiv : public ATDebugExpNodeBinary {
public:
	ATDebugExpNodeDiv(ATDebugExpNode *x, ATDebugExpNode *y)
		: ATDebugExpNodeBinary(kATDebugExpNodeType_Div, x, y, false)
	{
	}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		sint32 x;
		sint32 y;

		if (!mpLeft->Evaluate(x, context) ||
			!mpRight->Evaluate(y, context))
			return false;

		if (!y)
			return false;

		result = x / y;
		return true;
	}

	int GetPrecedence() const { return kNodePrecMul; }
	void EmitBinaryOp(VDStringA& s) {
		s += '/';
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeLT : public ATDebugExpNodeBinary {
public:
	ATDebugExpNodeLT(ATDebugExpNode *x, ATDebugExpNode *y)
		: ATDebugExpNodeBinary(kATDebugExpNodeType_LT, x, y, false)
	{
	}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		sint32 x;
		sint32 y;

		if (!mpLeft->Evaluate(x, context) ||
			!mpRight->Evaluate(y, context))
			return false;

		result = x < y;
		return true;
	}

	bool ExtractRelConst(ATDebugExpNodeType type, ATDebugExpNode **extracted, ATDebugExpNode **remainder, ATDebugExpNodeType *relop) {
		if (mpLeft->mType == type && mpRight->mType == kATDebugExpNodeType_Const) {
			*remainder = NULL;
			*extracted = mpRight;
			*relop = kATDebugExpNodeType_LT;
			mpLeft.reset();
			mpRight.release();
			return true;
		}

		if (mpRight->mType == type && mpLeft->mType == kATDebugExpNodeType_Const) {
			*remainder = NULL;
			*extracted = mpLeft;
			*relop = kATDebugExpNodeType_GT;
			mpLeft.release();
			mpRight.reset();
			return true;
		}

		return false;
	}

	bool OptimizeInvert(ATDebugExpNode **result);
	bool CanOptimizeInvert() const { return true; }

	int GetPrecedence() const { return kNodePrecRel; }
	void EmitBinaryOp(VDStringA& s) {
		s += '<';
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeLE : public ATDebugExpNodeBinary {
public:
	ATDebugExpNodeLE(ATDebugExpNode *x, ATDebugExpNode *y)
		: ATDebugExpNodeBinary(kATDebugExpNodeType_LE, x, y, false)
	{
	}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		sint32 x;
		sint32 y;

		if (!mpLeft->Evaluate(x, context) ||
			!mpRight->Evaluate(y, context))
			return false;

		result = x <= y;
		return true;
	}

	bool ExtractRelConst(ATDebugExpNodeType type, ATDebugExpNode **extracted, ATDebugExpNode **remainder, ATDebugExpNodeType *relop) {
		if (mpLeft->mType == type && mpRight->mType == kATDebugExpNodeType_Const) {
			*remainder = NULL;
			*extracted = mpRight;
			*relop = kATDebugExpNodeType_LE;
			mpLeft.reset();
			mpRight.release();
			return true;
		}

		if (mpRight->mType == type && mpLeft->mType == kATDebugExpNodeType_Const) {
			*remainder = NULL;
			*extracted = mpLeft;
			*relop = kATDebugExpNodeType_GE;
			mpLeft.release();
			mpRight.reset();
			return true;
		}

		return false;
	}

	bool OptimizeInvert(ATDebugExpNode **result);
	bool CanOptimizeInvert() const { return true; }

	int GetPrecedence() const { return kNodePrecRel; }
	void EmitBinaryOp(VDStringA& s) {
		s += "<=";
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeGT : public ATDebugExpNodeBinary {
public:
	ATDebugExpNodeGT(ATDebugExpNode *x, ATDebugExpNode *y)
		: ATDebugExpNodeBinary(kATDebugExpNodeType_GT, x, y, false)
	{
	}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		sint32 x;
		sint32 y;

		if (!mpLeft->Evaluate(x, context) ||
			!mpRight->Evaluate(y, context))
			return false;

		result = x > y;
		return true;
	}

	bool ExtractRelConst(ATDebugExpNodeType type, ATDebugExpNode **extracted, ATDebugExpNode **remainder, ATDebugExpNodeType *relop) {
		if (mpLeft->mType == type && mpRight->mType == kATDebugExpNodeType_Const) {
			*remainder = NULL;
			*extracted = mpRight;
			*relop = kATDebugExpNodeType_GT;
			mpLeft.reset();
			mpRight.release();
			return true;
		}

		if (mpRight->mType == type && mpLeft->mType == kATDebugExpNodeType_Const) {
			*remainder = NULL;
			*extracted = mpLeft;
			*relop = kATDebugExpNodeType_LT;
			mpLeft.release();
			mpRight.reset();
			return true;
		}

		return false;
	}

	bool OptimizeInvert(ATDebugExpNode **result);
	bool CanOptimizeInvert() const { return true; }

	int GetPrecedence() const { return kNodePrecRel; }
	void EmitBinaryOp(VDStringA& s) {
		s += '>';
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeGE : public ATDebugExpNodeBinary {
public:
	ATDebugExpNodeGE(ATDebugExpNode *x, ATDebugExpNode *y)
		: ATDebugExpNodeBinary(kATDebugExpNodeType_GE, x, y, false)
	{
	}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		sint32 x;
		sint32 y;

		if (!mpLeft->Evaluate(x, context) ||
			!mpRight->Evaluate(y, context))
			return false;

		result = x >= y;
		return true;
	}

	bool ExtractRelConst(ATDebugExpNodeType type, ATDebugExpNode **extracted, ATDebugExpNode **remainder, ATDebugExpNodeType *relop) {
		if (mpLeft->mType == type && mpRight->mType == kATDebugExpNodeType_Const) {
			*remainder = NULL;
			*extracted = mpRight;
			*relop = kATDebugExpNodeType_GE;
			mpLeft.reset();
			mpRight.release();
			return true;
		}

		if (mpRight->mType == type && mpLeft->mType == kATDebugExpNodeType_Const) {
			*remainder = NULL;
			*extracted = mpLeft;
			*relop = kATDebugExpNodeType_LE;
			mpLeft.release();
			mpRight.reset();
			return true;
		}

		return false;
	}

	bool OptimizeInvert(ATDebugExpNode **result);
	bool CanOptimizeInvert() const { return true; }

	int GetPrecedence() const { return kNodePrecRel; }
	void EmitBinaryOp(VDStringA& s) {
		s += ">=";
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeEQ : public ATDebugExpNodeBinary {
public:
	ATDebugExpNodeEQ(ATDebugExpNode *x, ATDebugExpNode *y)
		: ATDebugExpNodeBinary(kATDebugExpNodeType_EQ, x, y, false)
	{
	}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		sint32 x;
		sint32 y;

		if (!mpLeft->Evaluate(x, context) ||
			!mpRight->Evaluate(y, context))
			return false;

		result = x == y;
		return true;
	}

	bool ExtractEqConst(ATDebugExpNodeType type, ATDebugExpNode **extracted, ATDebugExpNode **remainder) {
		if (mpLeft->mType == type && mpRight->mType == kATDebugExpNodeType_Const) {
			*remainder = NULL;
			*extracted = mpRight;
			mpLeft.reset();
			mpRight.release();
			return true;
		}

		if (mpRight->mType == type && mpLeft->mType == kATDebugExpNodeType_Const) {
			*remainder = NULL;
			*extracted = mpLeft;
			mpLeft.release();
			mpRight.reset();
			return true;
		}

		return false;
	}

	bool OptimizeInvert(ATDebugExpNode **result);
	bool CanOptimizeInvert() const { return true; }

	int GetPrecedence() const { return kNodePrecRel; }
	void EmitBinaryOp(VDStringA& s) {
		s += '=';
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeNE : public ATDebugExpNodeBinary {
public:
	ATDebugExpNodeNE(ATDebugExpNode *x, ATDebugExpNode *y)
		: ATDebugExpNodeBinary(kATDebugExpNodeType_NE, x, y, false)
	{
	}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		sint32 x;
		sint32 y;

		if (!mpLeft->Evaluate(x, context) ||
			!mpRight->Evaluate(y, context))
			return false;

		result = x != y;
		return true;
	}

	bool OptimizeInvert(ATDebugExpNode **result);
	bool CanOptimizeInvert() const { return true; }

	int GetPrecedence() const { return kNodePrecRel; }
	void EmitBinaryOp(VDStringA& s) {
		s += "!=";
	}
};

///////////////////////////////////////////////////////////////////////////

bool ATDebugExpNodeLT::OptimizeInvert(ATDebugExpNode **result) {
	*result = new ATDebugExpNodeGE(mpLeft, mpRight);
	mpLeft.release();
	mpRight.release();
	return true;
}

bool ATDebugExpNodeLE::OptimizeInvert(ATDebugExpNode **result) {
	*result = new ATDebugExpNodeGT(mpLeft, mpRight);
	mpLeft.release();
	mpRight.release();
	return true;
}

bool ATDebugExpNodeGT::OptimizeInvert(ATDebugExpNode **result) {
	*result = new ATDebugExpNodeLE(mpLeft, mpRight);
	mpLeft.release();
	mpRight.release();
	return true;
}

bool ATDebugExpNodeGE::OptimizeInvert(ATDebugExpNode **result) {
	*result = new ATDebugExpNodeLT(mpLeft, mpRight);
	mpLeft.release();
	mpRight.release();
	return true;
}

bool ATDebugExpNodeEQ::OptimizeInvert(ATDebugExpNode **result) {
	*result = new ATDebugExpNodeNE(mpLeft, mpRight);
	mpLeft.release();
	mpRight.release();
	return true;
}

bool ATDebugExpNodeNE::OptimizeInvert(ATDebugExpNode **result) {
	*result = new ATDebugExpNodeEQ(mpLeft, mpRight);
	mpLeft.release();
	mpRight.release();
	return true;
}

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeInvert : public ATDebugExpNodeUnary {
public:
	ATDebugExpNodeInvert(ATDebugExpNode *x)
		: ATDebugExpNodeUnary(kATDebugExpNodeType_Invert, x)
	{
	}

	bool Optimize(ATDebugExpNode **result) {
		if (mpArg->OptimizeInvert(result)) {
			return true;
		}

		return ATDebugExpNodeUnary::Optimize(result);
	}

	bool OptimizeInvert(ATDebugExpNode **result) {
		*result = mpArg;
		mpArg.release();
		return true;
	}

	bool CanOptimizeInvert() const { return true; }

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		sint32 x;

		if (!mpArg->Evaluate(x, context))
			return false;

		result = !x;
		return true;
	}

	void EmitUnaryOp(VDStringA& s) {
		s += '!';
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeNegate : public ATDebugExpNodeUnary {
public:
	ATDebugExpNodeNegate(ATDebugExpNode *x)
		: ATDebugExpNodeUnary(kATDebugExpNodeType_Negate, x)
	{
	}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		sint32 x;

		if (!mpArg->Evaluate(x, context))
			return false;

		result = -x;
		return true;
	}

	void EmitUnaryOp(VDStringA& s) {
		s += '-';
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeDerefByte : public ATDebugExpNodeUnary {
public:
	ATDebugExpNodeDerefByte(ATDebugExpNode *x)
		: ATDebugExpNodeUnary(kATDebugExpNodeType_DerefByte, x)
	{
	}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		sint32 x;

		if (!mpArg->Evaluate(x, context))
			return false;

		if (!context.mpMemory)
			return false;

		result = context.mpMemory->DebugReadByte(x & 0xffff);
		return true;
	}

	void EmitUnaryOp(VDStringA& s) {
		s += "db ";
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeDerefSignedByte : public ATDebugExpNodeUnary {
public:
	ATDebugExpNodeDerefSignedByte(ATDebugExpNode *x)
		: ATDebugExpNodeUnary(kATDebugExpNodeType_DerefSignedByte, x)
	{
	}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		sint32 x;

		if (!mpArg->Evaluate(x, context))
			return false;

		if (!context.mpMemory)
			return false;

		result = (sint8)context.mpMemory->DebugReadByte(x & 0xffff);
		return true;
	}

	void EmitUnaryOp(VDStringA& s) {
		s += "dsb ";
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeDerefSignedWord : public ATDebugExpNodeUnary {
public:
	ATDebugExpNodeDerefSignedWord(ATDebugExpNode *x)
		: ATDebugExpNodeUnary(kATDebugExpNodeType_DerefSignedWord, x)
	{
	}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		sint32 x;

		if (!mpArg->Evaluate(x, context))
			return false;

		if (!context.mpMemory)
			return false;

		uint8 c0 = context.mpMemory->DebugReadByte(x & 0xffff);
		uint8 c1 = context.mpMemory->DebugReadByte((x+1) & 0xffff);
		result = (sint16)(c0 + (c1 << 8));
		return true;
	}

	void EmitUnaryOp(VDStringA& s) {
		s += "dsw ";
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeDerefWord : public ATDebugExpNodeUnary {
public:
	ATDebugExpNodeDerefWord(ATDebugExpNode *x)
		: ATDebugExpNodeUnary(kATDebugExpNodeType_DerefWord, x)
	{
	}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		sint32 x;

		if (!mpArg->Evaluate(x, context))
			return false;

		if (!context.mpMemory)
			return false;

		result = context.mpMemory->DebugReadByte(x & 0xffff)
			+ ((sint32)context.mpMemory->DebugReadByte((x + 1) & 0xffff) << 8);
		return true;
	}

	void EmitUnaryOp(VDStringA& s) {
		s += "dw ";
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeLoByte : public ATDebugExpNodeUnary {
public:
	ATDebugExpNodeLoByte(ATDebugExpNode *x)
		: ATDebugExpNodeUnary(kATDebugExpNodeType_LoByte, x)
	{
	}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		sint32 x;

		if (!mpArg->Evaluate(x, context))
			return false;

		result = x & 0xff;
		return true;
	}

	void EmitUnaryOp(VDStringA& s) {
		s += '<';
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeHiByte : public ATDebugExpNodeUnary {
public:
	ATDebugExpNodeHiByte(ATDebugExpNode *x)
		: ATDebugExpNodeUnary(kATDebugExpNodeType_HiByte, x)
	{
	}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		sint32 x;

		if (!mpArg->Evaluate(x, context))
			return false;

		result = (x & 0xff00) >> 8;
		return true;
	}

	void EmitUnaryOp(VDStringA& s) {
		s += '>';
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodePC : public ATDebugExpNode {
public:
	ATDebugExpNodePC() : ATDebugExpNode(kATDebugExpNodeType_PC) {}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		if (!context.mpCPU)
			return false;
		result = context.mpCPU->GetInsnPC();
		return true;
	}

	void ToString(VDStringA& s, int prec) {
		s += "pc";
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeA : public ATDebugExpNode {
public:
	ATDebugExpNodeA() : ATDebugExpNode(kATDebugExpNodeType_A) {}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		if (!context.mpCPU)
			return false;
		result = context.mpCPU->GetA();
		return true;
	}

	void ToString(VDStringA& s, int prec) {
		s += 'a';
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeX : public ATDebugExpNode {
public:
	ATDebugExpNodeX() : ATDebugExpNode(kATDebugExpNodeType_X) {}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		if (!context.mpCPU)
			return false;
		result = context.mpCPU->GetX();
		return true;
	}

	void ToString(VDStringA& s, int prec) {
		s += 'x';
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeY : public ATDebugExpNode {
public:
	ATDebugExpNodeY() : ATDebugExpNode(kATDebugExpNodeType_Y) {}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		if (!context.mpCPU)
			return false;
		result = context.mpCPU->GetY();
		return true;
	}

	void ToString(VDStringA& s, int prec) {
		s += 'y';
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeS : public ATDebugExpNode {
public:
	ATDebugExpNodeS() : ATDebugExpNode(kATDebugExpNodeType_S) {}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		if (!context.mpCPU)
			return false;
		result = context.mpCPU->GetS();
		return true;
	}

	void ToString(VDStringA& s, int prec) {
		s += 's';
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeP : public ATDebugExpNode {
public:
	ATDebugExpNodeP() : ATDebugExpNode(kATDebugExpNodeType_P) {}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		if (!context.mpCPU)
			return false;
		result = context.mpCPU->GetP();
		return true;
	}

	void ToString(VDStringA& s, int prec) {
		s += 'p';
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeRead : public ATDebugExpNode {
public:
	ATDebugExpNodeRead() : ATDebugExpNode(kATDebugExpNodeType_Read) {}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		if (!context.mbAccessReadValid)
			return false;

		result = context.mAccessAddress;
		return true;
	}

	void ToString(VDStringA& s, int prec) {
		s += "read";
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeWrite : public ATDebugExpNode {
public:
	ATDebugExpNodeWrite() : ATDebugExpNode(kATDebugExpNodeType_Write) {}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		if (!context.mbAccessWriteValid)
			return false;

		result = context.mAccessAddress;
		return true;
	}

	void ToString(VDStringA& s, int prec) {
		s += "write";
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeHPOS : public ATDebugExpNode {
public:
	ATDebugExpNodeHPOS() : ATDebugExpNode(kATDebugExpNodeType_HPOS) {}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		if (!context.mpAntic)
			return false;

		result = context.mpAntic->GetBeamX();
		return true;
	}

	void ToString(VDStringA& s, int prec) {
		s += "hpos";
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeVPOS : public ATDebugExpNode {
public:
	ATDebugExpNodeVPOS() : ATDebugExpNode(kATDebugExpNodeType_VPOS) {}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		if (!context.mpAntic)
			return false;

		result = context.mpAntic->GetBeamY();
		return true;
	}

	void ToString(VDStringA& s, int prec) {
		s += "vpos";
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeAddress : public ATDebugExpNode {
public:
	ATDebugExpNodeAddress() : ATDebugExpNode(kATDebugExpNodeType_Address) {}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		if (!context.mbAccessValid)
			return false;

		result = context.mAccessAddress;
		return true;
	}

	void ToString(VDStringA& s, int prec) {
		s += "address";
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeValue : public ATDebugExpNode {
public:
	ATDebugExpNodeValue() : ATDebugExpNode(kATDebugExpNodeType_Value) {}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		if (!context.mbAccessValid)
			return false;

		result = context.mAccessValue;
		return true;
	}

	void ToString(VDStringA& s, int prec) {
		s += "value";
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeXBankReg : public ATDebugExpNode {
public:
	ATDebugExpNodeXBankReg() : ATDebugExpNode(kATDebugExpNodeType_XBankReg) {}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		if (!context.mpMMU)
			return false;

		result = context.mpMMU->GetBankRegister();
		return true;
	}

	void ToString(VDStringA& s, int prec) {
		s += "xbankreg";
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeXBankCPU : public ATDebugExpNode {
public:
	ATDebugExpNodeXBankCPU() : ATDebugExpNode(kATDebugExpNodeType_XBankCPU) {}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		if (!context.mpMMU)
			return false;

		result = (sint32)context.mpMMU->GetCPUBankBase() - 0x10000;
		return true;
	}

	void ToString(VDStringA& s, int prec) {
		s += "xbankcpu";
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeXBankANTIC : public ATDebugExpNode {
public:
	ATDebugExpNodeXBankANTIC() : ATDebugExpNode(kATDebugExpNodeType_XBankANTIC) {}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		if (!context.mpMMU)
			return false;

		result = context.mpMMU->GetAnticBankBase() - 0x10000;
		return true;
	}

	void ToString(VDStringA& s, int prec) {
		s += "xbankantic";
	}
};

//////////////////////////////////////////////////////

class ATDebugExpNodeAddrSpace : public ATDebugExpNodeUnary {
public:
	ATDebugExpNodeAddrSpace(uint32 space, ATDebugExpNode *x)
		: ATDebugExpNodeUnary(kATDebugExpNodeType_AddrSpace, x)
		, mSpace(space)
	{
	}

	bool IsAddress() const { return true; }

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		sint32 x;

		if (!mpArg->Evaluate(x, context))
			return false;

		result = (sint32)(((uint32)x & kATAddressOffsetMask) + mSpace);
		return true;
	}

	void EmitUnaryOp(VDStringA& s) {
		switch(mSpace) {
			case kATAddressSpace_ANTIC:
				s += "n:";
				break;

			case kATAddressSpace_CPU:
				break;

			case kATAddressSpace_PORTB:
				s += "x:";
				break;

			case kATAddressSpace_RAM:
				s += "r:";
				break;

			case kATAddressSpace_VBXE:
				s += "v:";
				break;
		}
	}

private:
	const uint32 mSpace;
};

ATDebugExpNode *ATDebuggerParseExpression(const char *s, IATDebugger *dbg, const ATDebuggerExprParseOpts& opts) {
	enum {
		kOpNone,
		kOpOpenParen,
		kOpCloseParen,
		kOpOr,
		kOpAnd,
		kOpBitwiseOr,
		kOpBitwiseXor,
		kOpBitwiseAnd,
		kOpLT,
		kOpLE,
		kOpGT,
		kOpGE,
		kOpEQ,
		kOpNE,
		kOpAdd,
		kOpSub,
		kOpMul,
		kOpDiv,
		kOpMod,
		kOpInvert,
		kOpNegate,
		kOpDerefByte,
		kOpDerefSignedByte,
		kOpDerefSignedWord,
		kOpDerefWord,
		kOpLoByte,
		kOpHiByte,
		kOpAddrSpace
	};

	static const uint8 kOpPrecedence[]={
		0,

		// open paren
		1,

		// close paren
		0,

		// or
		2,

		// and
		3,

		// bitwise or
		4,

		// bitwise xor
		5,

		// bitwise and
		6,

		// comparisons
		7,7,7,7,7,7,

		// additive
		8,8,

		// multiplicative
		9,9,9,

		// unary
		10,10,10,10,10,10,10,10,

		// addr space
		11
	};

	enum {
		kTokEOL,
		kTokInt		= 256,
		kTokDB,
		kTokDSB,
		kTokDW,
		kTokDSW,
		kTokAnd,
		kTokOr,
		kTokLT,
		kTokLE,
		kTokGT,
		kTokGE,
		kTokEQ,
		kTokNE,
		kTokPC,
		kTokA,
		kTokX,
		kTokY,
		kTokS,
		kTokP,
		kTokRead,
		kTokWrite,
		kTokHpos,
		kTokVpos,
		kTokAddress,
		kTokValue,
		kTokAddrSpace,
		kTokXBankReg,
		kTokXBankCPU,
		kTokXBankANTIC
	};

	vdfastvector<ATDebugExpNode *> valstack;
	vdfastvector<uint8> opstack;

	bool needValue = true;
	sint32 intVal;
	bool hexVal;

	if (dbg && opts.mbAllowUntaggedHex) {
		intVal = dbg->ResolveSymbol(s, true, true);

		if (intVal >= 0) {
			s += strlen(s);
			needValue = false;

			valstack.push_back(new ATDebugExpNodeConst(intVal, opts.mbAllowUntaggedHex, true));
		}
	}

	try {
		for(;;) {
			char c = *s++;
			int tok = c;

			if (c == ' ')
				continue;

			if (!strchr("+-*/%()&|^", c)) {
				if (!c) {
					tok = kTokEOL;
				} else if (c == '<') {
					if (*s == '=') {
						++s;
						tok = kTokLE;
					} else
						tok = kTokLT;
				} else if (c == '>') {
					if (*s == '=') {
						++s;
						tok = kTokGE;
					} else
						tok = kTokGT;
				} else if (c == '=') {
					tok = kTokEQ;
				} else if (c == '!') {
					if (*s == '=') {
						++s;
						tok = kTokNE;
					}
					// fall through if just !
				} else if (c == '$' || (isxdigit((unsigned char)c) && opts.mbDefaultHex)) {
					if (c == '$') {
						c = *s;

						if (!isxdigit((unsigned char)c)) {
							FreeNodes(valstack);
							throw ATDebuggerExprParseException("Expected hex number after $");
						}

						++s;
					}

					bool has_bank = false;
					uint32 bank = 0;
					uint32 v = 0;
					for(;;) {
						v = (v << 4) + (c & 0x0f);

						if (c & 0x40)
							v += 9;

						c = *s;

						if (!has_bank && c == ':') {
							has_bank = true;

							if (v > 0xff)
								throw ATDebuggerExprParseException("Bank too large");

							bank = v << 16;
							v = 0;
							c = 0;
						} else if (!isxdigit((unsigned char)c))
							break;

						++s;
					}

					if (has_bank)
						v = (v & 0xffff) + bank;

					intVal = v;
					hexVal = true;
					tok = kTokInt;
				} else if (isdigit((unsigned char)c)) {
					uint32 v = (unsigned char)(c - '0');
					bool has_bank = false;
					uint32 bank = 0;

					for(;;) {
						c = *s;

						if (!has_bank && c == ':') {
							has_bank = true;

							if (v > 0xff)
								throw ATDebuggerExprParseException("Bank too large");

							bank = v << 16;
							v = 0;
						} else if (isdigit((unsigned char)c))
							v = (v * 10) + ((unsigned char)c - '0');
						else
							break;

						++s;

					}

					if (has_bank)
						v += bank;

					intVal = v;
					tok = kTokInt;
					hexVal = false;
				} else if (isalpha((unsigned char)c) || c == '_' || c == '#') {
					const char *idstart = s-1;

					if (c == '#')
						++idstart;

					while(*s && (isalnum((unsigned char)*s) || *s == '_' || *s == '.'))
						++s;

					VDStringSpanA ident(idstart, s);

					if (c == '#')
						goto force_ident;

					if (*s == ':') {
						++s;

						if (ident == "v")
							intVal = (sint32)kATAddressSpace_VBXE;
						else if (ident == "n")
							intVal = (sint32)kATAddressSpace_ANTIC;
						else if (ident == "x")
							intVal = (sint32)kATAddressSpace_PORTB;
						else if (ident == "r")
							intVal = (sint32)kATAddressSpace_RAM;
						else
							throw ATDebuggerExprParseException("Unknown address space: '%.*s'", ident.size(), ident.data());

						tok = kTokAddrSpace;
					} else if (ident == "pc")
						tok = kTokPC;
					else if (ident == "a")
						tok = kTokA;
					else if (ident == "x")
						tok = kTokX;
					else if (ident == "y")
						tok = kTokY;
					else if (ident == "s")
						tok = kTokS;
					else if (ident == "p")
						tok = kTokP;
					else if (ident == "db")
						tok = kTokDB;
					else if (ident == "dsb")
						tok = kTokDSB;
					else if (ident == "dw")
						tok = kTokDW;
					else if (ident == "dsw")
						tok = kTokDSW;
					else if (ident == "and")
						tok = kTokAnd;
					else if (ident == "or")
						tok = kTokOr;
					else if (ident == "read")
						tok = kTokRead;
					else if (ident == "write")
						tok = kTokWrite;
					else if (ident == "hpos")
						tok = kTokHpos;
					else if (ident == "vpos")
						tok = kTokVpos;
					else if (ident == "address")
						tok = kTokAddress;
					else if (ident == "value")
						tok = kTokValue;
					else if (ident == "xbankreg")
						tok = kTokXBankReg;
					else if (ident == "xbankcpu")
						tok = kTokXBankCPU;
					else if (ident == "xbankantic")
						tok = kTokXBankANTIC;
					else {
force_ident:
						IATDebuggerSymbolLookup *symlookup = ATGetDebuggerSymbolLookup();

						VDString identstr(ident);
						if (dbg && (intVal = dbg->ResolveSymbol(identstr.c_str(), false, false)) >= 0) {
							tok = kTokInt;
							hexVal = opts.mbDefaultHex;
						} else {
							FreeNodes(valstack);
							throw ATDebuggerExprParseException("Unable to resolve symbol \"%s\"", identstr.c_str());
						}
					}
				} else
					throw ATDebuggerExprParseException("Unexpected character '%c'", c);
			}

			if (needValue) {
				if (tok == '(') {
					opstack.push_back(kOpOpenParen);
				} else if (tok == '+') {
					// unary plus - nothing to do
				} else if (tok == '-') {
					// unary minus
					opstack.push_back(kOpNegate);
				} else if (tok == '!') {
					// unary minus
					opstack.push_back(kOpInvert);
				} else if (tok == '%') {
					// binary number
					if (*s != '0' && *s != '1')
						throw ATDebuggerExprParseException("Invalid binary number.");

					intVal = 0;
					do {
						intVal = (intVal << 1) + (*s == '1');
						++s;
					} while(*s == '0' || *s == '1');

					vdautoptr<ATDebugExpNode> node(new ATDebugExpNodeConst(intVal, true, false));

					valstack.push_back(node);
					node.release();

					needValue = false;

				} else if (tok == kTokPC) {
					vdautoptr<ATDebugExpNode> node(new ATDebugExpNodePC);

					valstack.push_back(node);
					node.release();

					needValue = false;
				} else if (tok == kTokA) {
					vdautoptr<ATDebugExpNode> node(new ATDebugExpNodeA);

					valstack.push_back(node);
					node.release();

					needValue = false;
				} else if (tok == kTokX) {
					vdautoptr<ATDebugExpNode> node(new ATDebugExpNodeX);

					valstack.push_back(node);
					node.release();

					needValue = false;
				} else if (tok == kTokY) {
					vdautoptr<ATDebugExpNode> node(new ATDebugExpNodeY);

					valstack.push_back(node);
					node.release();

					needValue = false;
				} else if (tok == kTokS) {
					vdautoptr<ATDebugExpNode> node(new ATDebugExpNodeS);

					valstack.push_back(node);
					node.release();

					needValue = false;
				} else if (tok == kTokP) {
					vdautoptr<ATDebugExpNode> node(new ATDebugExpNodeP);

					valstack.push_back(node);
					node.release();

					needValue = false;
				} else if (tok == kTokRead) {
					vdautoptr<ATDebugExpNode> node(new ATDebugExpNodeRead);

					valstack.push_back(node);
					node.release();

					needValue = false;
				} else if (tok == kTokWrite) {
					vdautoptr<ATDebugExpNode> node(new ATDebugExpNodeWrite);

					valstack.push_back(node);
					node.release();

					needValue = false;
				} else if (tok == kTokHpos) {
					vdautoptr<ATDebugExpNode> node(new ATDebugExpNodeHPOS);

					valstack.push_back(node);
					node.release();

					needValue = false;
				} else if (tok == kTokVpos) {
					vdautoptr<ATDebugExpNode> node(new ATDebugExpNodeVPOS);

					valstack.push_back(node);
					node.release();

					needValue = false;
				} else if (tok == kTokDB) {
					opstack.push_back(kOpDerefByte);
				} else if (tok == kTokDSB) {
					opstack.push_back(kOpDerefSignedByte);
				} else if (tok == kTokDW) {
					opstack.push_back(kOpDerefWord);
				} else if (tok == kTokDSW) {
					opstack.push_back(kOpDerefSignedWord);
				} else if (tok == kTokInt) {
					vdautoptr<ATDebugExpNode> node(new ATDebugExpNodeConst(intVal, hexVal, false));

					valstack.push_back(node);
					node.release();

					needValue = false;
				} else if (tok == kTokLT) {
					opstack.push_back(kOpLoByte);
				} else if (tok == kTokGT) {
					opstack.push_back(kOpHiByte);
				} else if (tok == kTokAddress) {
					vdautoptr<ATDebugExpNode> node(new ATDebugExpNodeAddress);

					valstack.push_back(node);
					node.release();

					needValue = false;
				} else if (tok == kTokValue) {
					vdautoptr<ATDebugExpNode> node(new ATDebugExpNodeValue);

					valstack.push_back(node);
					node.release();

					needValue = false;
				} else if (tok == kTokAddrSpace) {
					valstack.push_back(new ATDebugExpNodeConst(intVal, true, true));
					opstack.push_back(kOpAddrSpace);
				} else if (tok == kTokXBankReg) {
					vdautoptr<ATDebugExpNode> node(new ATDebugExpNodeXBankReg);

					valstack.push_back(node);
					node.release();

					needValue = false;
				} else if (tok == kTokXBankCPU) {
					vdautoptr<ATDebugExpNode> node(new ATDebugExpNodeXBankCPU);

					valstack.push_back(node);
					node.release();

					needValue = false;
				} else if (tok == kTokXBankANTIC) {
					vdautoptr<ATDebugExpNode> node(new ATDebugExpNodeXBankANTIC);

					valstack.push_back(node);
					node.release();

					needValue = false;
				} else {
					throw ATDebuggerExprParseException("Expected value");
				}
			} else {
				uint8 op;

				switch(tok) {
					case ')':
						op = kOpCloseParen;
						break;

					case '&':
						op = kOpBitwiseAnd;
						break;

					case '|':
						op = kOpBitwiseOr;
						break;

					case '^':
						op = kOpBitwiseXor;
						break;

					case kTokEOL:
						op = kOpNone;
						break;


					case kTokAnd:
						op = kOpAnd;
						break;

					case kTokOr:
						op = kOpOr;
						break;

					case kTokLT:
						op = kOpLT;
						break;

					case kTokLE:
						op = kOpLE;
						break;

					case kTokGT:
						op = kOpGT;
						break;

					case kTokGE:
						op = kOpGE;
						break;

					case kTokEQ:
						op = kOpEQ;
						break;

					case kTokNE:
						op = kOpNE;
						break;

					case '+':
						op = kOpAdd;
						break;

					case '-':
						op = kOpSub;
						break;

					case '*':
						op = kOpMul;
						break;

					case '/':
						op = kOpDiv;
						break;

					case '%':
						op = kOpMod;
						break;

					default:
						throw ATDebuggerExprParseException("Expected operator");
				}

				// begin reduction
				uint8 prec = kOpPrecedence[op];
				for(;;) {
					if (opstack.empty()) {
						if (op == kOpCloseParen)
							throw ATDebuggerExprParseException("Unmatched '('");
						break;
					}

					uint8 redop = opstack.back();

					if (kOpPrecedence[redop] < prec)
						break;

					opstack.pop_back();

					if (redop == kOpOpenParen) {
						if (op == kOpNone)
							throw ATDebuggerExprParseException("Unmatched ')'");
						break;
					}

					vdautoptr<ATDebugExpNode> node;
					ATDebugExpNode **sp = &valstack.back();

					int argcount = 2;

					switch(redop) {
						case kOpOr:
							node = new ATDebugExpNodeOr(sp[-1], sp[0]);
							break;

						case kOpAnd:
							node = new ATDebugExpNodeAnd(sp[-1], sp[0]);
							break;

						case kOpLT:
							node = new ATDebugExpNodeLT(sp[-1], sp[0]);
							break;

						case kOpLE:
							node = new ATDebugExpNodeLE(sp[-1], sp[0]);
							break;

						case kOpGT:
							node = new ATDebugExpNodeGT(sp[-1], sp[0]);
							break;

						case kOpGE:
							node = new ATDebugExpNodeGE(sp[-1], sp[0]);
							break;

						case kOpEQ:
							node = new ATDebugExpNodeEQ(sp[-1], sp[0]);
							break;

						case kOpNE:
							node = new ATDebugExpNodeNE(sp[-1], sp[0]);
							break;

						case kOpAdd:
							node = new ATDebugExpNodeAdd(sp[-1], sp[0]);
							break;

						case kOpSub:
							node = new ATDebugExpNodeSub(sp[-1], sp[0]);
							break;

						case kOpMul:
							node = new ATDebugExpNodeMul(sp[-1], sp[0]);
							break;

						case kOpDiv:
							node = new ATDebugExpNodeDiv(sp[-1], sp[0]);
							break;

						case kOpMod:
							node = new ATDebugExpNodeMod(sp[-1], sp[0]);
							break;

						case kOpInvert:
							node = new ATDebugExpNodeInvert(sp[0]);
							argcount = 1;
							break;


						case kOpNegate:
							node = new ATDebugExpNodeNegate(sp[0]);
							argcount = 1;
							break;

						case kOpDerefByte:
							node = new ATDebugExpNodeDerefByte(sp[0]);
							argcount = 1;
							break;

						case kOpDerefWord:
							node = new ATDebugExpNodeDerefWord(sp[0]);
							argcount = 1;
							break;

						case kOpDerefSignedByte:
							node = new ATDebugExpNodeDerefSignedByte(sp[0]);
							argcount = 1;
							break;

						case kOpDerefSignedWord:
							node = new ATDebugExpNodeDerefSignedWord(sp[0]);
							argcount = 1;
							break;

						case kOpLoByte:
							node = new ATDebugExpNodeLoByte(sp[0]);
							argcount = 1;
							break;

						case kOpHiByte:
							node = new ATDebugExpNodeHiByte(sp[0]);
							argcount = 1;
							break;

						case kOpBitwiseAnd:
							node = new ATDebugExpNodeBitwiseAnd(sp[-1], sp[0]);
							break;

						case kOpBitwiseOr:
							node = new ATDebugExpNodeBitwiseOr(sp[-1], sp[0]);
							break;

						case kOpBitwiseXor:
							node = new ATDebugExpNodeBitwiseXor(sp[-1], sp[0]);
							break;

						case kOpAddrSpace:
							sp[-1]->Evaluate(intVal, ATDebugExpEvalContext());
							delete sp[-1];
							sp[-1] = NULL;
							node = new ATDebugExpNodeAddrSpace(intVal, sp[0]);
							break;
					}

					while(argcount--)
						valstack.pop_back();

					valstack.push_back(node);
					node.release();
				}

				if (op == kOpNone)
					break;

				if (op != kOpCloseParen) {
					opstack.push_back(op);
					needValue = true;
				}
			}
		}
	} catch(const ATDebuggerExprParseException&) {
		while(!valstack.empty()) {
			delete valstack.back();
			valstack.pop_back();
		}

		throw;
	}

	VDASSERT(valstack.size() == 1);

	ATDebugExpNode *result = valstack.back();
	ATDebugExpNode *optResult;

	if (result->Optimize(&optResult)) {
		delete result;
		result = optResult;
	}

	return result;
}

ATDebugExpNode *ATDebuggerInvertExpression(ATDebugExpNode *node) {
	ATDebugExpNode *result = new ATDebugExpNodeInvert(node);
	ATDebugExpNode *optResult;

	if (result->Optimize(&optResult)) {
		delete result;
		result = optResult;
	}

	return result;
}
