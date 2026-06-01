#include "kv_store.h"
#include "memory_map.h"
#include "ftl_config.h"

P_KV_INDEX kvIndexPtr;

static unsigned int nextKvLogicalSlice;

static unsigned int HashKvKey(unsigned int key)
{
	return (key * 2654435761U) & KV_INDEX_ENTRY_MASK;
}

void InitKvStore()
{
	unsigned int entry;

	kvIndexPtr = (P_KV_INDEX)KV_INDEX_ADDR;
	nextKvLogicalSlice = 0;

	for(entry = 0; entry < KV_INDEX_ENTRY_COUNT; entry++)
		kvIndexPtr->entry[entry].logicalSliceAddr = KV_LSA_NONE;
}

unsigned int AllocateKvLogicalSlice()
{
	unsigned int logicalSliceCapacity;

	logicalSliceCapacity = storageCapacity_L / NVME_BLOCKS_PER_SLICE;
	if(nextKvLogicalSlice >= logicalSliceCapacity)
		return KV_LSA_NONE;

	return nextKvLogicalSlice++;
}

unsigned int FindKvIndexEntry(unsigned int key, unsigned int *logicalSliceAddr, unsigned int *valueLength)
{
	unsigned int entry, probe;

	entry = HashKvKey(key);
	for(probe = 0; probe < KV_INDEX_ENTRY_COUNT; probe++)
	{
		if(kvIndexPtr->entry[entry].logicalSliceAddr == KV_LSA_NONE)
			return 0;

		if(kvIndexPtr->entry[entry].key == key)
		{
			*logicalSliceAddr = kvIndexPtr->entry[entry].logicalSliceAddr;
			*valueLength = kvIndexPtr->entry[entry].valueLength;
			return 1;
		}

		entry = (entry + 1) & KV_INDEX_ENTRY_MASK;
	}

	return 0;
}

unsigned int PutKvIndexEntry(unsigned int key, unsigned int logicalSliceAddr, unsigned int valueLength)
{
	unsigned int entry, probe;

	entry = HashKvKey(key);
	for(probe = 0; probe < KV_INDEX_ENTRY_COUNT; probe++)
	{
		if((kvIndexPtr->entry[entry].logicalSliceAddr == KV_LSA_NONE) ||
		   (kvIndexPtr->entry[entry].key == key))
		{
			kvIndexPtr->entry[entry].key = key;
			kvIndexPtr->entry[entry].logicalSliceAddr = logicalSliceAddr;
			kvIndexPtr->entry[entry].valueLength = valueLength;
			return 1;
		}

		entry = (entry + 1) & KV_INDEX_ENTRY_MASK;
	}

	return 0;
}
