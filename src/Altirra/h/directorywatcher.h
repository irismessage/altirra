#ifndef f_AT_DIRECTORYWATCHER_H
#define f_AT_DIRECTORYWATCHER_H

#include <vd2/system/thread.h>
#include <vd2/system/VDString.h>
#include <vd2/system/vdstl_hashset.h>

class ATDirectoryWatcher : public VDThread {
	ATDirectoryWatcher(const ATDirectoryWatcher&);
	ATDirectoryWatcher& operator=(const ATDirectoryWatcher&);
public:
	static void SetShouldUsePolling(bool enabled);

	ATDirectoryWatcher();
	~ATDirectoryWatcher();

	void Init(const wchar_t *basePath, bool recursive = true);
	void Shutdown();

	bool CheckForChanges();
	bool CheckForChanges(vdfastvector<wchar_t>& strheap);

protected:
	void ThreadRun();
	void RunPollThread();
	void PollDirectory(uint32 *orderIndependentChecksum, const VDStringSpanW& path, uint32 nestingLevel);
	void RunNotifyThread();
	void NotifyAllChanged();

	VDStringW mBasePath;
	void *mhDir;
	void *mhExitEvent;
	void *mhDirChangeEvent;
	void *mpChangeBuffer;
	uint32 mChangeBufferSize;
	bool mbRecursive;

	VDCriticalSection mMutex;
	typedef vdhashset<VDStringW, vdstringhashi, vdstringpredi> ChangedDirs;
	ChangedDirs mChangedDirs;
	bool mbAllChanged;

	static bool sbShouldUsePolling;
};

#endif
