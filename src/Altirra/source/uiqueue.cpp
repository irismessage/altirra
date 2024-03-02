#include "stdafx.h"
#include <vd2/system/error.h>
#include "uiqueue.h"
#include "uicommondialogs.h"

void ATUIStepFunctionAdapter(void *data) {
	((void(*)())data)();
}

ATUIFuture::ATUIFuture()
	: mStage(0)
{
}

ATUIFuture::~ATUIFuture() {
}

ATUIStep ATUIFuture::GetStep() {
	return ATUIStep::FromMethod<ATUIFuture, &ATUIFuture::RunStep>(this);
}

bool ATUIFuture::Run() {
	if (mpWait) {
		if (!mpWait->Run())
			return false;

		mpWait.clear();
	}

	try {
		RunInner();
	} catch(...) {
		MarkCompleted();
		throw;
	}

	return mStage < 0;
}

void ATUIFuture::RunInner() {
}

void ATUIFuture::RunStep() {
	if (mStage >= 0) {
		ATUIPushStep(GetStep());

		if (Run())
			mStage = -1;
	}
}

void ATUIFuture::Wait(ATUIFuture *f) {
	mpWait = f;
}

///////////////////////////////////////////////////////////////////////////

bool ATUIQueue::Run() {
	if (mSteps.empty())
		return false;

	ATUIStep step = mSteps.back();
	mSteps.pop_back();

	if (step.mpRunFn) {
		try {
			step.mpRunFn(step.mpData);
		} catch(const MyError& e) {
			PushStep(ATUIShowAlert(VDTextAToW(e.gets()).c_str(), L"Altirra Error")->GetStep());
		}
	}

	if (step.mpReleaseFn)
		step.mpReleaseFn(step.mpData);

	return true;
}

void ATUIQueue::PushStep(const ATUIStep& step) {
	mSteps.push_back(step);
}

///////////////////////////////////////////////////////////////////////////

ATUIQueue g_ATUIQueue;

ATUIQueue& ATUIGetQueue() {
	return g_ATUIQueue;
}

void ATUIPushStep(const ATUIStep& step) {
	g_ATUIQueue.PushStep(step);
}
