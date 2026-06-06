#include "kv_store.h"
#include "memory_map.h"
#include "ftl_config.h"

P_KV_INDEX kvIndexPtr;

static unsigned int nextKvLba;

static unsigned int HashKvKey(unsigned int key)
{
	return (key * 2654435761U) & KV_INDEX_ENTRY_MASK;
}

void InitKvStore()
{
	unsigned int entry;

	kvIndexPtr = (P_KV_INDEX)KV_INDEX_ADDR;
	nextKvLba = 0;

	for(entry = 0; entry < KV_INDEX_ENTRY_COUNT; entry++)
		kvIndexPtr->entry[entry].startLba = KV_LBA_NONE;
}

unsigned int KvFtlBlockCountFromLength(unsigned int valueLength)
{
	if(valueLength == 0)
		return 0;

	return ((valueLength - 1) / BYTES_PER_NVME_BLOCK) + 1;
}

unsigned int AllocateKvBlockRange(unsigned int blockCount)
{
	unsigned int allocatedLba;

	if(blockCount == 0)
		return KV_LBA_NONE;

	if(nextKvLba > storageCapacity_L)
		return KV_LBA_NONE;

	if(blockCount > (storageCapacity_L - nextKvLba))
		return KV_LBA_NONE;

	allocatedLba = nextKvLba;
	nextKvLba += blockCount;

	return allocatedLba;
}

unsigned int FindKvIndexEntry(unsigned int key, unsigned int *startLba, unsigned int *valueLength)
{
	unsigned int entry, probe;

	entry = HashKvKey(key);
	for(probe = 0; probe < KV_INDEX_ENTRY_COUNT; probe++)
	{
		if(kvIndexPtr->entry[entry].startLba == KV_LBA_NONE)
			return 0;

		if(kvIndexPtr->entry[entry].key == key)
		{
			*startLba = kvIndexPtr->entry[entry].startLba;
			*valueLength = kvIndexPtr->entry[entry].valueLength;
			return 1;
		}

		entry = (entry + 1) & KV_INDEX_ENTRY_MASK;
	}

	return 0;
}

unsigned int PutKvIndexEntry(unsigned int key, unsigned int startLba, unsigned int valueLength)
{
	unsigned int entry, probe;

	entry = HashKvKey(key);
	for(probe = 0; probe < KV_INDEX_ENTRY_COUNT; probe++)
	{
		if((kvIndexPtr->entry[entry].startLba == KV_LBA_NONE) ||
		   (kvIndexPtr->entry[entry].key == key))
		{
			kvIndexPtr->entry[entry].key = key;
			kvIndexPtr->entry[entry].startLba = startLba;
			kvIndexPtr->entry[entry].valueLength = valueLength;
			return 1;
		}

		entry = (entry + 1) & KV_INDEX_ENTRY_MASK;
	}

	return 0;
}

unsigned int KvFtlPut(unsigned int key, unsigned int nlb, unsigned int valueLength, unsigned int *startLba)
{
	unsigned int allocatedLba;
	unsigned int blockCount;
	unsigned int oldBlockCount;
	unsigned int oldStartLba;
	unsigned int oldValueLength;
	unsigned int maxValueLength;

	blockCount = nlb + 1;
	if(blockCount == 0)
		return KV_FTL_INVALID_VALUE;

	maxValueLength = blockCount * BYTES_PER_NVME_BLOCK;
	if(valueLength == 0)
		valueLength = maxValueLength;
	else if(valueLength > maxValueLength)
		return KV_FTL_INVALID_VALUE;

	if(FindKvIndexEntry(key, &oldStartLba, &oldValueLength))
	{
		oldBlockCount = KvFtlBlockCountFromLength(oldValueLength);
		if(blockCount <= oldBlockCount)
		{
			if(!PutKvIndexEntry(key, oldStartLba, valueLength))
				return KV_FTL_CAPACITY_FULL;

			*startLba = oldStartLba;
			return KV_FTL_SUCCESS;
		}
	}

	allocatedLba = AllocateKvBlockRange(blockCount);
	if(allocatedLba == KV_LBA_NONE)
		return KV_FTL_CAPACITY_FULL;

	if(!PutKvIndexEntry(key, allocatedLba, valueLength))
	{
		if((allocatedLba + blockCount) == nextKvLba)
			nextKvLba = allocatedLba;

		return KV_FTL_CAPACITY_FULL;
	}

	*startLba = allocatedLba;

	return KV_FTL_SUCCESS;
}
