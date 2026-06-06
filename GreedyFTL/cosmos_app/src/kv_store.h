#ifndef KV_STORE_H_
#define KV_STORE_H_

#define KV_INDEX_ENTRY_COUNT			(1 << 23)
#define KV_INDEX_ENTRY_MASK			(KV_INDEX_ENTRY_COUNT - 1)

#define KV_LBA_NONE				0xffffffff
#define KV_LSA_NONE				KV_LBA_NONE
#define KV_VALUE_LENGTH_NONE			0xffffffff

#define KV_FTL_SUCCESS				0
#define KV_FTL_INVALID_VALUE			1
#define KV_FTL_CAPACITY_FULL			2

typedef struct _KV_INDEX_ENTRY {
	unsigned int key;
	unsigned int startLba;
	unsigned int valueLength;
} KV_INDEX_ENTRY, *P_KV_INDEX_ENTRY;

typedef struct _KV_INDEX {
	KV_INDEX_ENTRY entry[KV_INDEX_ENTRY_COUNT];
} KV_INDEX, *P_KV_INDEX;

void InitKvStore();
unsigned int KvFtlBlockCountFromLength(unsigned int valueLength);
unsigned int KvFtlPut(unsigned int key, unsigned int nlb, unsigned int valueLength, unsigned int *startLba);
unsigned int AllocateKvBlockRange(unsigned int blockCount);
unsigned int FindKvIndexEntry(unsigned int key, unsigned int *startLba, unsigned int *valueLength);
unsigned int PutKvIndexEntry(unsigned int key, unsigned int startLba, unsigned int valueLength);

extern P_KV_INDEX kvIndexPtr;

#endif /* KV_STORE_H_ */
