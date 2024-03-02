#include "stdafx.h"
#include <ctype.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include "debuggerexp.h"
#include "cpu.h"
#include "cpumemory.h"
#include "debugger.h"
#include "antic.h"

namespace {
	void FreeNodes(vdfastvector<ATDebugExpNode *>& nodes) {
		while(!nodes.empty()) {
			delete nodes.back();
			nodes.pop_back();
		}
	}

	enum {
		kNodePrecAnd,
		kNodePrecOr,
		kNodePrecRel,
		kNodePrecAdd,
		kNodePrecMul,
		kNodePrecUnary
	};
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeConst : public ATDebugExpNode {
public:
	ATDebugExpNodeConst(sint32 v, bool hex)
		: ATDebugExpNode(kATDebugExpNodeType_Const)
		, mVal(v)
		, mbHex(hex)
	{
	}

	bool IsHex() const { return mbHex; }

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const {
		result = mVal;
		return true;
	}

	void ToString(VDStringA& s, int prec) {
		s.append_sprintf(mbHex ? mVal >= 0x100 ? mVal >= 0x10000 ? "$%08X" : "$%04X" : "$%02X" : "%d", mVal);
	}

protected:
	const sint32 mVal;
	const bool mbHex;
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
				*result = new ATDebugExpNodeConst(v, static_cast<ATDebugExpNodeConst *>(&*mpArg)->IsHex());
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
	ATDebugExpNodeBinary(ATDebugExpNodeType type, ATDebugExpNode *left, ATDebugExpNode *right)
		: ATDebugExpNode(type)
		, mpLeft(left)
		, mpRight(right)
	{
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
					|| static_cast<ATDebugExpNodeConst *>(&*mpRight)->IsHex()
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
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeAnd : public ATDebugExpNodeBinary {
public:
	ATDebugExpNodeAnd(ATDebugExpNode *x, ATDebugExpNode *y)
		: ATDebugExpNodeBinary(kATDebugExpNodeType_And, x, y)
	{
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
		: ATDebugExpNodeBinary(kATDebugExpNodeType_Or, x, y)
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

	int GetAssociativity() const { return 0; }
	int GetPrecedence() const { return kNodePrecOr; }
	void EmitBinaryOp(VDStringA& s) {
		s += " or ";
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeAdd : public ATDebugExpNodeBinary {
public:
	ATDebugExpNodeAdd(ATDebugExpNode *x, ATDebugExpNode *y)
		: ATDebugExpNodeBinary(kATDebugExpNodeType_Add, x, y)
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
		: ATDebugExpNodeBinary(kATDebugExpNodeType_Sub, x, y)
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
		: ATDebugExpNodeBinary(kATDebugExpNodeType_Mul, x, y)
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

class ATDebugExpNodeDiv : public ATDebugExpNodeBinary {
public:
	ATDebugExpNodeDiv(ATDebugExpNode *x, ATDebugExpNode *y)
		: ATDebugExpNodeBinary(kATDebugExpNodeType_Div, x, y)
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
		: ATDebugExpNodeBinary(kATDebugExpNodeType_LT, x, y)
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

	int GetPrecedence() const { return kNodePrecRel; }
	void EmitBinaryOp(VDStringA& s) {
		s += '<';
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeLE : public ATDebugExpNodeBinary {
public:
	ATDebugExpNodeLE(ATDebugExpNode *x, ATDebugExpNode *y)
		: ATDebugExpNodeBinary(kATDebugExpNodeType_LT, x, y)
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

	int GetPrecedence() const { return kNodePrecRel; }
	void EmitBinaryOp(VDStringA& s) {
		s += "<=";
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeGT : public ATDebugExpNodeBinary {
public:
	ATDebugExpNodeGT(ATDebugExpNode *x, ATDebugExpNode *y)
		: ATDebugExpNodeBinary(kATDebugExpNodeType_GT, x, y)
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

	int GetPrecedence() const { return kNodePrecRel; }
	void EmitBinaryOp(VDStringA& s) {
		s += '>';
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeGE : public ATDebugExpNodeBinary {
public:
	ATDebugExpNodeGE(ATDebugExpNode *x, ATDebugExpNode *y)
		: ATDebugExpNodeBinary(kATDebugExpNodeType_GE, x, y)
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

	int GetPrecedence() const { return kNodePrecRel; }
	void EmitBinaryOp(VDStringA& s) {
		s += ">=";
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeEQ : public ATDebugExpNodeBinary {
public:
	ATDebugExpNodeEQ(ATDebugExpNode *x, ATDebugExpNode *y)
		: ATDebugExpNodeBinary(kATDebugExpNodeType_EQ, x, y)
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

	int GetPrecedence() const { return kNodePrecRel; }
	void EmitBinaryOp(VDStringA& s) {
		s += '=';
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeNE : public ATDebugExpNodeBinary {
public:
	ATDebugExpNodeNE(ATDebugExpNode *x, ATDebugExpNode *y)
		: ATDebugExpNodeBinary(kATDebugExpNodeType_NE, x, y)
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

	int GetPrecedence() const { return kNodePrecRel; }
	void EmitBinaryOp(VDStringA& s) {
		s += "!=";
	}
};

///////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeInvert : public ATDebugExpNodeUnary {
public:
	ATDebugExpNodeInvert(ATDebugExpNode *x)
		: ATDebugExpNodeUnary(kATDebugExpNodeType_Invert, x)
	{
	}

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
		: ATDebugExpNodeUnary(kATDebugExpNodeType_Negate, x)
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

class ATDebugExpNodeDerefWord : public ATDebugExpNodeUnary {
public:
	ATDebugExpNodeDerefWord(ATDebugExpNode *x)
		: ATDebugExpNodeUnary(kATDebugExpNodeType_Negate, x)
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

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const { return false; }

	void ToString(VDStringA& s, int prec) {
		s += "read";
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////

class ATDebugExpNodeWrite : public ATDebugExpNode {
public:
	ATDebugExpNodeWrite() : ATDebugExpNode(kATDebugExpNodeType_Write) {}

	bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const { return false; }

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

//////////////////////////////////////////////////////

ATDebugExpNode *ATDebuggerParseExpression(const char *s, IATDebugger *dbg) {
	enum {
		kOpNone,
		kOpOpenParen,
		kOpCloseParen,
		kOpOr,
		kOpAnd,
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
		kOpInvert,
		kOpNegate,
		kOpDerefByte,
		kOpDerefWord,
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

		// comparisons
		4,4,4,4,4,4,

		// additive
		5,5,

		// multiplicative
		6,6,

		// unary
		7,7,7,7
	};

	enum {
		kTokEOL,
		kTokInt		= 256,
		kTokDB,
		kTokDW,
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
		kTokVpos
	};

	vdfastvector<ATDebugExpNode *> valstack;
	vdfastvector<uint8> opstack;

	bool needValue = true;
	sint32 intVal;
	bool hexVal;

	for(;;) {
		char c = *s++;
		int tok = c;

		if (c == ' ')
			continue;

		if (!strchr("+-*/()", c)) {
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
			} else if (c == '!' && *s == '=') {
				++s;
				tok = kTokNE;
			} else if (c == '$') {
				c = *s;

				if (!isxdigit((unsigned char)c)) {
					FreeNodes(valstack);
					throw ATDebuggerExprParseException("Expected hex number after $");
				}

				++s;

				uint32 v = 0;
				for(;;) {
					v = (v << 4) + (c & 0x0f);

					if (c & 0x40)
						v += 9;

					c = *s;

					if (!isxdigit((unsigned char)c))
						break;

					++s;
				}

				intVal = v;
				hexVal = true;
				tok = kTokInt;
			} else if (isdigit((unsigned char)c)) {
				uint32 v = (unsigned char)(c - '0');

				for(;;) {
					c = *s;

					if (!isdigit((unsigned char)c))
						break;

					++s;

					v = (v * 10) + ((unsigned char)c - '0');
				}

				intVal = v;
				tok = kTokInt;
				hexVal = false;
			} else if (isalpha((unsigned char)c)) {
				const char *idstart = s-1;

				while(*s && isalnum((unsigned char)*s))
					++s;

				VDStringSpanA ident(idstart, s);

				if (ident == "pc")
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
				else if (ident == "dw")
					tok = kTokDW;
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
				else {
					IATDebuggerSymbolLookup *symlookup = ATGetDebuggerSymbolLookup();

					VDString identstr(ident);
					if (dbg && (intVal = dbg->ResolveSymbol(identstr.c_str())) >= 0) {
						tok = kTokInt;
						hexVal = true;
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
			} else if (tok == kTokDW) {
				opstack.push_back(kOpDerefWord);
			} else if (tok == kTokInt) {
				vdautoptr<ATDebugExpNode> node(new ATDebugExpNodeConst(intVal, hexVal));

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

				case kTokEOL:
					op = kOpNone;
					break;

				case kTokDB:
					op = kOpDerefByte;
					break;

				case kTokDW:
					op = kOpDerefWord;
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
	return new ATDebugExpNodeNegate(node);
}
