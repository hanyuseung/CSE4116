#ifndef KV_STORE_H_
#define KV_STORE_H_

#define KV_INDEX_ENTRY_COUNT			(1 << 23)
#define KV_INDEX_ENTRY_MASK			(KV_INDEX_ENTRY_COUNT - 1)

#define KV_LSA_NONE				0xffffffff
#define KV_VALUE_LENGTH_NONE			0xffffffff

typedef struct _KV_INDEX_ENTRY {
	unsigned int key;
	unsigned int logicalSliceAddr;
	unsigned int valueLength;
} KV_INDEX_ENTRY, *P_KV_INDEX_ENTRY;

typedef struct _KV_INDEX {
	KV_INDEX_ENTRY entry[KV_INDEX_ENTRY_COUNT];
} KV_INDEX, *P_KV_INDEX;

void InitKvStore();
unsigned int AllocateKvLogicalSlice();
unsigned int FindKvIndexEntry(unsigned int key, unsigned int *logicalSliceAddr, unsigned int *valueLength);
unsigned int PutKvIndexEntry(unsigned int key, unsigned int logicalSliceAddr, unsigned int valueLength);

extern P_KV_INDEX kvIndexPtr;

#endif /* KV_STORE_H_ */
