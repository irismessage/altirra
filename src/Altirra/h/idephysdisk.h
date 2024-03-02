#ifndef f_AT_IDEPHYSDISK_H
#define f_AT_IDEPHYSDISK_H

bool ATIDEIsPhysicalDiskPath(const wchar_t *path);

class ATIDEPhysicalDisk {
	ATIDEPhysicalDisk(const ATIDEPhysicalDisk&);
	ATIDEPhysicalDisk& operator=(const ATIDEPhysicalDisk&);
public:
	ATIDEPhysicalDisk();
	~ATIDEPhysicalDisk();

	uint32 GetSectorCount() const { return mSectorCount; }

	void Init(const wchar_t *path);
	void Shutdown();

	void RequestUpdate();

	void ReadSectors(void *data, uint32 lba, uint32 n);
	void WriteSectors(void *data, uint32 lba, uint32 n);

protected:
	void *mhDisk;
	void *mpBuffer;
	uint32 mSectorCount;
};

#endif
