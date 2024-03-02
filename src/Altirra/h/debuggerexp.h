#ifndef f_AT_DEBUGGEREXP_H
#define f_AT_DEBUGGEREXP_H

#include <stdarg.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/error.h>

class VDStringA;
class ATCPUEmulator;
class ATCPUEmulatorMemory;
class ATAnticEmulator;
class ATMMUEmulator;
class IATDebugger;

enum ATDebugExpNodeType {
	kATDebugExpNodeType_None,
	kATDebugExpNodeType_PC,
	kATDebugExpNodeType_A,
	kATDebugExpNodeType_X,
	kATDebugExpNodeType_Y,
	kATDebugExpNodeType_S,
	kATDebugExpNodeType_P,
	kATDebugExpNodeType_Or,
	kATDebugExpNodeType_And,
	kATDebugExpNodeType_BitwiseOr,
	kATDebugExpNodeType_BitwiseXor,
	kATDebugExpNodeType_BitwiseAnd,
	kATDebugExpNodeType_LT,
	kATDebugExpNodeType_LE,
	kATDebugExpNodeType_GT,
	kATDebugExpNodeType_GE,
	kATDebugExpNodeType_NE,
	kATDebugExpNodeType_EQ,
	kATDebugExpNodeType_Add,
	kATDebugExpNodeType_Sub,
	kATDebugExpNodeType_Mul,
	kATDebugExpNodeType_Div,
	kATDebugExpNodeType_DerefByte,
	kATDebugExpNodeType_DerefSignedByte,
	kATDebugExpNodeType_DerefWord,
	kATDebugExpNodeType_DerefSignedWord,
	kATDebugExpNodeType_DerefSignedDoubleWord,
	kATDebugExpNodeType_Invert,
	kATDebugExpNodeType_Negate,
	kATDebugExpNodeType_Const,
	kATDebugExpNodeType_Read,
	kATDebugExpNodeType_Write,
	kATDebugExpNodeType_HPOS,
	kATDebugExpNodeType_VPOS,
	kATDebugExpNodeType_LoByte,
	kATDebugExpNodeType_HiByte,
	kATDebugExpNodeType_Address,
	kATDebugExpNodeType_Value,
	kATDebugExpNodeType_XBankReg,
	kATDebugExpNodeType_XBankCPU,
	kATDebugExpNodeType_XBankANTIC,
	kATDebugExpNodeType_AddrSpace,
	kATDebugExpNodeType_Ternary
};

struct ATDebugExpEvalContext {
	ATCPUEmulator *mpCPU;
	ATCPUEmulatorMemory *mpMemory;
	ATAnticEmulator *mpAntic;
	ATMMUEmulator *mpMMU;

	bool mbAccessValid;
	bool mbAccessReadValid;
	bool mbAccessWriteValid;
	sint32 mAccessAddress;
	uint8 mAccessValue;
};

class ATDebugExpNode {
public:
	const ATDebugExpNodeType mType;

	ATDebugExpNode(ATDebugExpNodeType nodeType)
		: mType(nodeType)
	{
	}

	virtual ~ATDebugExpNode() {}

	virtual bool Evaluate(sint32& result, const ATDebugExpEvalContext& context) const = 0;

	/// Attempt to extract out a (node == const) clause from an AND sequence; returns
	/// the constant and the remainder expression.
	virtual bool ExtractEqConst(ATDebugExpNodeType type, ATDebugExpNode **extracted, ATDebugExpNode **remainder) { return false; }

	/// Attempt to extract out less than / less equal / greater than / greater equal
	/// expressions of the form (type relop const). These will capture flips as well,
	/// i.e. a request to capture GT will match both (type > const) and (const < type).
	virtual bool ExtractRelConst(ATDebugExpNodeType type, ATDebugExpNode **extracted, ATDebugExpNode **remainder, ATDebugExpNodeType *relop) { return false; }

	/// Returns a new expression node representing an equivalent subexpression that is
	/// better than the existing node.
	///
	/// Optimize() is expected to be auto-propagating -- a node should optimize its
	/// arguments first.
	///
	/// If false is returned, no optimization was possible and no new node is returned.
	/// If true is returned, a new optimized node is returned, and the 'this' node should
	/// now be destroyed.
	virtual bool Optimize(ATDebugExpNode **result) { return false; }

	/// Returns a new expression node representing the logical inversion of the existing
	/// node that is at least as good as the existing node. It is OK for this to return
	/// a subexpression of equal complexity. Invert operators return their argument;
	/// relational operators return their inverse.
	///
	/// If false is returned, no optimization was possible and no new node is returned.
	/// If true is returned, a new optimized node is returned, and the 'this' node should
	/// now be destroyed.
	virtual bool OptimizeInvert(ATDebugExpNode **result) { return false; }

	virtual bool IsAddress() const { return false; }

	/// Returns true if OptimizeInvert() can produce a result that is at least no
	/// worse, but may be the same. This is needed in binary or trinary ops which need
	/// to know that both subexpressions can be advantageously inverted before doing
	/// either of them.
	virtual bool CanOptimizeInvert() const { return false; }

	virtual void ToString(VDStringA& s) {
		ToString(s, 0);
	}

	virtual void ToString(VDStringA& s, int prec) = 0;
};

struct ATDebuggerExprParseOpts {
	bool mbDefaultHex;
	bool mbAllowUntaggedHex;
};

ATDebugExpNode *ATDebuggerParseExpression(const char *s, IATDebugger *dbg, const ATDebuggerExprParseOpts& opts);
ATDebugExpNode *ATDebuggerInvertExpression(ATDebugExpNode *node);

class ATDebuggerExprParseException : public MyError {
public:
	ATDebuggerExprParseException(const char *s, ...) {
		va_list val;
		va_start(val, s);
		vsetf(s, val);
		va_end(val);
	}
};

#endif
