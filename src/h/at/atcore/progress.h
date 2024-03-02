//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2016 Avery Lee
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
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#ifndef f_AT_ATCORE_PROGRESS_H
#define f_AT_ATCORE_PROGRESS_H

#include <stdarg.h>
#include <vd2/system/function.h>

class IATTaskProgressContext;

void ATBeginProgress(uint32 total, const wchar_t *statusFormat, const wchar_t *desc);
void ATBeginProgressF(uint32 total, const wchar_t *statusFormat, const wchar_t *descFormat, va_list descArgs);
void ATUpdateProgress(uint32 count);
bool ATCheckProgressStatusUpdate();
void ATUpdateProgressStatus(const wchar_t *statusMessage);
void ATEndProgress();

class IATProgressHandler {
public:
	virtual void Begin(uint32 total, const wchar_t *status, const wchar_t *desc) = 0;
	virtual void BeginF(uint32 total, const wchar_t *status, const wchar_t *descFormat, va_list descArgs) = 0;
	virtual void Update(uint32 value) = 0;
	virtual bool CheckForCancellationOrStatus() = 0;
	virtual void UpdateStatus(const wchar_t *statusMessage) = 0;
	virtual void End() = 0;

	virtual bool RunTask(const wchar_t *desc, const vdfunction<void(IATTaskProgressContext&)>&) = 0;
};

void ATSetProgressHandler(IATProgressHandler *h);

class ATProgress {
	ATProgress(const ATProgress&) = delete;
	ATProgress& operator=(const ATProgress&) = delete;

public:
	ATProgress() = default;
	~ATProgress() { Shutdown(); }

	void Init(uint32 n, const wchar_t *statusFormat, const wchar_t *desc) {
		ATBeginProgress(n, statusFormat, desc);
		mbCreated = true;
	}

	void InitF(uint32 n, const wchar_t *statusFormat, const wchar_t *descFormat, ...) {
		va_list val;
		va_start(val, descFormat);
		ATBeginProgressF(n, statusFormat, descFormat, val);
		va_end(val);
		mbCreated = true;
	}

	void Shutdown() {
		if (mbCreated) {
			mbCreated = false;
			ATEndProgress();
		}
	}

	void Update(uint32 value) {
		ATUpdateProgress(value);
	}

	bool CheckForCancellationOrStatus() {
		return ATCheckProgressStatusUpdate();
	}

	void UpdateStatus(const wchar_t *msg) {
		ATUpdateProgressStatus(msg);
	}

protected:
	bool mbCreated = false;
};

/////////////////////////////////////////////////////////////////////////////////////////////

class IATTaskProgressContext {
public:
	virtual bool CheckForCancellationOrStatus() = 0;
	virtual void SetProgress(double progress) = 0;
	virtual void SetProgressF(double progress, const wchar_t *format, ...) = 0;
};

void ATRunTaskWithProgress(const wchar_t *desc, const vdfunction<void(IATTaskProgressContext&)>& fn);

#endif
