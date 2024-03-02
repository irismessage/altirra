#ifndef f_AT_UIQUEUE_H
#define f_AT_UIQUEUE_H

#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>

void ATUIStepFunctionAdapter(void *data);

template<class T, void (T::*T_Method)()>
void ATUIStepMethodAdapter(void *data) {
	(((T *)data)->*T_Method)();
}

template<class T>
void ATUIStepReleaseAdapter(void *data) {
	((T *)data)->Release();
}

struct ATUIStep {
	void *mpData;
	void (*mpRunFn)(void *data);
	void (*mpReleaseFn)(void *data);

	ATUIStep(void (*fn)())
		: mpData(fn)
		, mpRunFn(ATUIStepFunctionAdapter)
		, mpReleaseFn(NULL)
	{
	}

	ATUIStep(void (*fn)(void *), void *data, void (*releasefn)(void *))
		: mpData(data)
		, mpRunFn(fn)
		, mpReleaseFn(releasefn)
	{
	}

	template<class T, void (T::*T_Method)()>
	static ATUIStep FromMethod(T *thisptr) {
		thisptr->AddRef();
		return ATUIStep(ATUIStepMethodAdapter<T, T_Method>, thisptr, ATUIStepReleaseAdapter<T>);
	}
};

class ATUIFuture : public vdrefcount {
public:
	ATUIFuture();
	virtual ~ATUIFuture();

	ATUIStep GetStep();

	bool Run();
	virtual void RunInner();

protected:
	void MarkCompleted() { mStage = -1; }
	void Wait(ATUIFuture *f);

	sint32 mStage;

private:
	virtual void RunStep();

	vdrefptr<ATUIFuture> mpWait;
};

template<class T>
class ATUIFutureWithResult : public ATUIFuture {
public:
	ATUIFutureWithResult() {}

	explicit ATUIFutureWithResult(const T& immediateResult) {
		mResult = immediateResult;
		MarkCompleted();
	}

	const T& GetResult() const { return mResult; }

protected:
	T mResult;
};

class ATUIQueue {
public:
	bool Run();

	void PushStep(const ATUIStep& step);

protected:
	typedef vdfastvector<ATUIStep> Steps;
	Steps mSteps;
};

ATUIQueue& ATUIGetQueue();
void ATUIPushStep(const ATUIStep& step);

#endif
