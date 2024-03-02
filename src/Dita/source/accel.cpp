#include <stdafx.h>
#include <vd2/system/error.h>
#include <vd2/system/registry.h>
#include <vd2/system/strutil.h>
#include <vd2/Dita/accel.h>

struct VDAccelTableDefinition::TaggedAccelPred {
	bool operator()(const TaggedAccel& a, const TaggedAccel& b) const {
		return a.mKey < b.mKey;
	}
};

VDAccelTableDefinition::VDAccelTableDefinition() {
}

VDAccelTableDefinition::VDAccelTableDefinition(const VDAccelTableDefinition& src) {
	try {
		AddRange(src.mAccelerators.begin(), src.mAccelerators.size());
	} catch(...) {
		Clear();
		throw;
	}
}

VDAccelTableDefinition::~VDAccelTableDefinition() {
	Clear();
}

VDAccelTableDefinition& VDAccelTableDefinition::operator=(const VDAccelTableDefinition& src) {
	if (&src != this) {
		VDAccelTableDefinition tmp(src);

		Swap(tmp);
	}

	return *this;
}

uint32 VDAccelTableDefinition::GetSize() const {
	return mAccelerators.size();
}

const VDAccelTableEntry& VDAccelTableDefinition::operator[](uint32 index) const {
	return mAccelerators[index];
}

const VDAccelTableEntry* VDAccelTableDefinition::operator()(const VDUIAccelerator& accel) const {
	for(Accelerators::const_iterator it(mAccelerators.begin()), itEnd(mAccelerators.end());
		it != itEnd;
		++it)
	{
		const VDAccelTableEntry& entry = *it;

		if (entry.mAccel.mVirtKey == accel.mVirtKey
			&& entry.mAccel.mModifiers == accel.mModifiers)
		{
			return &entry;
		}
	}

	return NULL;
}

void VDAccelTableDefinition::Clear() {
	while(!mAccelerators.empty()) {
		VDAccelTableEntry& ent = mAccelerators.back();
		free((void *)ent.mpCommand);
		mAccelerators.pop_back();
	}
}

void VDAccelTableDefinition::Add(const VDAccelTableEntry& src) {
	size_t len = strlen(src.mpCommand);
	char *s = (char *)malloc(len + 1);
	if (!s)
		throw MyMemoryError();

	memcpy(s, src.mpCommand, len + 1);

	try {
		VDAccelTableEntry& acc = mAccelerators.push_back();
		acc.mpCommand = s;
		acc.mCommandId = src.mCommandId;
		acc.mAccel = src.mAccel;
	} catch(...) {
		free(s);
		throw;
	}
}

void VDAccelTableDefinition::AddRange(const VDAccelTableEntry *ent, size_t n) {
	mAccelerators.reserve(mAccelerators.size() + n);

	while(n--)
		Add(*ent++);
}

void VDAccelTableDefinition::RemoveAt(uint32 index) {
	if (index < mAccelerators.size()) {
		VDAccelTableEntry& acc = mAccelerators[index];

		free((void *)acc.mpCommand);

		mAccelerators.erase(mAccelerators.begin() + index);
	}
}

void VDAccelTableDefinition::Swap(VDAccelTableDefinition& dst) {
	mAccelerators.swap(dst.mAccelerators);
}

void VDAccelTableDefinition::Save(VDRegistryKey& key, const VDAccelTableDefinition& base) const {
	TaggedAccelerators baseLookup;
	TaggedAccelerators curLookup;
	BuildTaggedList(baseLookup, base);
	BuildTaggedList(curLookup, *this);

	TaggedAccelerators updateSet(baseLookup.size() + curLookup.size());

	auto itBase = baseLookup.begin(), itBaseEnd = baseLookup.end();
	auto itCur = curLookup.begin(), itCurEnd = curLookup.end();
	auto itUpdate = updateSet.begin();

	TaggedAccelPred pred;
	for(;;) {
		if (itBase == itBaseEnd) {
			// current not in base -- copy remaining from current (add)
			itUpdate = std::copy(itCur, itCurEnd, itUpdate);
			break;
		}

		if (itCur == itCurEnd) {
			// base not in current -- copy remaining from base (delete)
			itUpdate = std::transform(itBase, itBaseEnd, itUpdate, [](const TaggedAccel& e) { return TaggedAccel { e.mKey, nullptr }; });
			break;
		}

		if (pred(*itBase, *itCur)) {
			// base not in current -- copy from base (delete)
			*itUpdate++ = TaggedAccel { itBase++->mKey, nullptr };
		} else if (pred(*itCur, *itBase)) {
			// current not in base -- copy from current (add)
			*itUpdate++ = *itCur++;
		} else {
			// base and current have same item -- copy if different (update)
			if (vdstricmp(itBase->mpEntry->mpCommand, itCur->mpEntry->mpCommand))
				*itUpdate++ = *itCur;

			++itBase;
			++itCur;
		}
	}

	updateSet.erase(itUpdate, updateSet.end());

	vdfastvector<uint32> entriesToDelete;

	VDRegistryValueIterator it(key);
	while(const char *name = it.Next()) {
		unsigned v;
		char term;
		if (sscanf(name, "%08x%c", &v, &term) != 1)
			continue;

		const TaggedAccel testEnt { v, nullptr };

		if (!std::binary_search(updateSet.begin(), updateSet.end(), testEnt, pred))
			entriesToDelete.push_back(v);
	}

	char buf[16];
	for(uint32 v : entriesToDelete) {
		sprintf(buf, "%08x", v);
		key.removeValue(buf);
	}

	for(const TaggedAccel& ent : updateSet) {
		sprintf(buf, "%08x", ent.mKey);

		key.setString(buf, ent.mpEntry ? ent.mpEntry->mpCommand : "");
	}
}

void VDAccelTableDefinition::Load(VDRegistryKey& key, const VDAccelTableDefinition& base, const VDAccelToCommandEntry *pCommands, uint32 nCommands) {
	Clear();

	VDRegistryValueIterator it(key);

	vdfastvector<uint32> deletedEntries;

	VDStringA cmd;
	while(const char *name = it.Next()) {
		unsigned v;
		char term;
		if (sscanf(name, "%08x%c", &v, &term) != 1 || !v)
			continue;

		if (!key.getString(name, cmd))
			continue;

		if (cmd.empty()) {
			deletedEntries.push_back(v);
		} else {
			VDAccelTableEntry ent;
			ent.mAccel.mVirtKey = (v & 0xffff);
			ent.mAccel.mModifiers = v >> 16;
			ent.mCommandId = 0;
			ent.mpCommand = cmd.c_str();

			for(uint32 i=0; i<nCommands; ++i) {
				const VDAccelToCommandEntry& cmdent = pCommands[i];

				if (!cmd.comparei(cmdent.mpName)) {
					ent.mCommandId = cmdent.mId;
					break;
				}
			}

			Add(ent);
		}
	}

	std::sort(deletedEntries.begin(), deletedEntries.end());

	for(const VDAccelTableEntry& ent : base.mAccelerators) {
		if (!std::binary_search(deletedEntries.begin(), deletedEntries.end(), GetKey(ent)))
			Add(ent);
	}
}

void VDAccelTableDefinition::BuildTaggedList(TaggedAccelerators& dst, const VDAccelTableDefinition& src) {
	for(const VDAccelTableEntry& ent : src.mAccelerators) {
		dst.push_back(TaggedAccel { GetKey(ent), &ent });
	}

	std::sort(dst.begin(), dst.end(), TaggedAccelPred());
}

inline uint32 VDAccelTableDefinition::GetKey(const VDAccelTableEntry& e) {
	return (e.mAccel.mModifiers << 16) + e.mAccel.mVirtKey;
}
