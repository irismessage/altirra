#ifndef f_AT_SCSIDISK_H
#define f_AT_SCSIDISK_H

#include "scsi.h"

class IATUIRenderer;
class IATIDEDisk;

class IATSCSIDiskDevice : public IATSCSIDevice {
public:
	virtual void SetUIRenderer(IATUIRenderer *r) = 0;
};

void ATCreateSCSIDiskDevice(IATIDEDisk *disk, IATSCSIDiskDevice **dev);

#endif
