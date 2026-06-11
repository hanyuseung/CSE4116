# Task1 – Key-Value Interface 개통 및 KV-SSD 펌웨어 개발

## 1. NVMe KV I/O Command와 기존 Block I/O Command의 차이, 그리고 새 Storage Abstraction이 호스트 소프트웨어 스택에 미치는 영향

### 1.1 Command 구조의 차이

기존 NVMe Block I/O Command(Read `0x02`, Write `0x01`)는 host가 데이터의 위치(Logical Block Address)를 직접 지정한다. 반면 본 프로젝트에서 새로 정의한 KV I/O Command는 위치 대신 key를 전달하며, 데이터가 저장될 위치의 결정은 전적으로 SSD 펌웨어 내부로 위임된다. 두 command의 SQE(Submission Queue Entry) 필드 사용을 비교하면 다음과 같다.

| 필드 | Block Write/Read (`0x01`/`0x02`) | KV-PUT (`0xA0`) / KV-GET (`0xA1`) |
| --- | --- | --- |
| OPC | `0x01` (Write), `0x02` (Read) | `0xA0` (PUT), `0xA1` (GET) |
| CDW10–11 | Starting LBA (64-bit) | **4-byte key** (CDW10) |
| CDW12 | NLB (zero-based block 수) | NLB (zero-based block 수) |
| CDW13 | (DSM 등) | **value 길이 (bytes)** |
| PRP | data buffer | PUT value / GET 결과 buffer |
| Completion DW0 | 사용 안 함 | **GET 시 실제 value 길이 반환** |

구조적으로 가장 큰 차이는 두 가지다.

1. **주소 필드의 의미 변화**: Block command의 SLBA 자리에는 host가 계산한 "위치"가 들어가지만, KV command의 CDW10에는 host application이 정의한 "이름(key)"이 들어간다. 즉 *name-to-location translation*이 host에서 device로 이동한다.
2. **Completion의 의미 변화**: Block read는 host가 요청한 길이를 그대로 돌려받으므로 completion entry의 DW0가 불필요하다. 반면 KV-GET은 host가 value의 실제 길이를 모르는 상태로 요청하므로, device가 completion DW0(specific 필드)에 value 길이를 실어 반환해야 한다. 또한 존재하지 않는 key에 대해서는 vendor-specific status (`SCT=0x7`, `SC=0xC1`)로 "No such key"를 알린다. 이는 block 인터페이스에는 존재하지 않는 의미론이다.

> [시각자료: Block I/O와 KV I/O의 SQE dword 배치를 좌우로 나란히 그린 비교 그림. 왼쪽 Block command는 CDW10–11에 SLBA, CDW12에 NLB가 들어가고, 오른쪽 KV command는 CDW10에 key, CDW12에 NLB, CDW13에 value length가 들어가는 것을 같은 dword 위치에 색으로 대응시켜 표현. 아래쪽에는 completion entry(CQE)도 함께 그려 KV-GET에서만 DW0에 valueLength가 채워짐을 강조하면 좋음.]

### 1.2 주소 변환 경로의 차이

Block 인터페이스에서는 mapping 관리의 책임이 host에 있다.

```text
[Block I/O]
App → File System / DB index → Block Layer → NVMe Driver
    → (host가 결정한 LBA) → SSD: LSA → VSA → NAND physical
```

KV 인터페이스에서는 key→위치 변환 단계가 SSD 내부에 추가되고, host는 위치를 전혀 알지 못한다.

```text
[KV I/O]
App → KV API (NVMe passthrough ioctl)
    → (key) → SSD: KV index (key → LBA) → LSA → VSA → NAND physical
```

> [시각자료: 위 두 경로를 하나의 그림에 상하 또는 좌우로 배치한 host–device 계층도. Host 영역(Application, File System/DB index, Block layer/Driver)과 Device 영역(KV index, LSA→VSA 변환, NAND)을 박스로 구분하고, "key→LBA mapping 관리 주체"가 Block I/O에서는 host 쪽 박스에, KV I/O에서는 device 쪽 박스에 있음을 화살표와 색상으로 대비시키는 그림이 적합함.]

### 1.3 호스트 소프트웨어 스택에 미치는 영향

이러한 storage abstraction의 변화는 host 스택에 다음과 같은 영향을 준다.

- **Indexing 계층의 오프로드**: 기존에는 file system의 extent/inode 관리나 KV DB(LSM-tree 등)의 인덱스가 "key → 파일 offset → LBA" 변환을 수행했다. KV-SSD에서는 이 변환의 마지막 단계가 device로 내려가므로, host의 software indexing 계층을 얇게 만들거나 생략할 수 있고, 그만큼 host CPU/DRAM 자원과 I/O amplification이 줄어든다.
- **Block layer 우회**: 표준 block 인터페이스를 따르지 않으므로 기존 file system, page cache, block scheduler를 그대로 사용할 수 없다. 본 프로젝트의 host benchmark도 `NVME_IOCTL_IO_CMD` 기반 NVMe passthrough로 command를 직접 발행한다. 즉, KV 추상화의 이점을 얻는 대신 host는 새로운 KV 전용 API/드라이버 경로를 필요로 한다.
- **오류 의미론의 확장**: host는 "No such key"라는 존재 여부 의미론과, completion DW0를 통한 가변 길이 반환을 처리해야 한다. 이는 단순한 sector 입출력보다 풍부한 계약(contract)이며, application 입장에서는 GET/PUT만으로 object storage처럼 사용할 수 있게 된다.
- **일관성 책임의 이동**: 동일 key에 대한 latest-write-wins 보장이 device의 책임이 되므로, host는 자체적인 버전 관리 없이 최신 value 조회를 신뢰할 수 있다.

## 2. KV-SSD 구현체의 Index 자료구조

### 2.1 채택한 구조: DRAM 상의 open-addressing hash table

KV index는 `kv_store.h`에 선언된 정적 hash table로, device DRAM의 예약 영역(`memory_map.h`의 `RESERVED0_START_ADDR = 0x00300000`)에 배치하였다.

```c
#define KV_INDEX_ENTRY_COUNT    (1 << 23)        // 8,388,608 entries
#define KV_INDEX_ENTRY_MASK     (KV_INDEX_ENTRY_COUNT - 1)

typedef struct _KV_INDEX_ENTRY {
    unsigned int key;            // host가 정의한 4-byte key
    unsigned int startLba;       // 최신 value가 저장된 시작 LBA
    unsigned int valueLength;    // 최신 value의 바이트 길이
} KV_INDEX_ENTRY;
```

- **Hash function**: Knuth의 multiplicative hashing(`key * 2654435761`)에 table mask를 적용한다. 곱셈 한 번과 AND 한 번으로 계산되어 펌웨어 환경에서 매우 저렴하고, 순차적/편향된 key 입력도 table 전체에 고르게 분산시킨다.
- **Collision 처리**: open addressing + linear probing. 충돌 시 다음 slot으로 한 칸씩 이동하며, 빈 entry는 `startLba == 0xFFFFFFFF`(`KV_LBA_NONE`)로 표시한다.
- **크기**: entry당 12 bytes × 8,388,608개 = 96 MiB. 평가에서 요구되는 keyspace 4,194,304(2^22)를 기준으로 load factor가 최대 50%가 되도록 entry 수를 2^23으로 정하여, linear probing의 평균 probe 길이를 짧게 유지하였다.

> [시각자료: hash table 동작을 보여주는 그림. 세로로 길게 그린 entry 배열(0 ~ 8,388,607)에 대해, ① key가 multiplicative hash를 거쳐 특정 index로 매핑되는 화살표, ② 해당 slot이 차 있을 때 linear probing으로 아래 칸으로 이동하는 화살표, ③ entry 내부 3개 필드(key, startLba, valueLength)의 확대도, ④ 빈 slot은 startLba=0xFFFFFFFF로 표시됨을 함께 그리면 좋음. DRAM memory map에서 KV index가 RESERVED0 영역(96 MiB)을 차지함을 보여주는 작은 배치도를 곁들이면 더 좋음.]

### 2.2 선정 이유

- **평균 O(1) 연산**: benchmark workload는 균등 random key에 대한 PUT/GET이므로, hash table이 평균 상수 시간 lookup/insert를 제공한다. 10,000,000회 연산 규모에서 tree 계열(O(log n))보다 유리하다.
- **고정 크기 key에 최적**: key가 4 bytes 고정이므로 가변 길이 key 비교나 별도 key 저장 공간이 필요 없고, entry가 12 bytes로 단순해진다.
- **펌웨어 환경 적합성**: 동적 메모리 할당 없이 부팅 시 정적 영역에 한 번 배치하면 되고, 포인터 추적이 없어 디버깅이 쉽다. Cosmos+ 보드의 1 GiB DRAM 중 96 MiB는 충분히 감당 가능한 크기다.
- **Persistence 불요**: 본 과제의 평가 시나리오는 reset 이후 데이터 유지가 요구되지 않으므로, NAND-backed 구조(B+-tree, LSM 등) 대신 in-memory 구조로 충분하다.

### 2.3 장단점

장점:

- 평균 O(1)의 빠른 lookup/insert, 구현과 검증이 단순함.
- 메모리 배치가 정적·연속적이라 cache 동작과 디버깅에 유리함.
- load factor 50% 이하로 설계하여 probing 비용이 안정적임.

단점:

- DRAM에만 존재하므로 reset 시 index가 소실됨 (persistence 없음).
- entry 수가 컴파일 시 고정되어 keyspace가 table 크기에 근접하면 probing이 급격히 길어지고, 초과하면 PUT이 실패함 (resize 불가).
- 삭제(DELETE) 연산을 고려하지 않은 설계이며, 삭제를 지원하려면 tombstone 처리가 추가로 필요함.
- range query, 순회 등 정렬 기반 연산을 지원할 수 없음 (hash 구조의 본질적 한계).

## 3. KV-SSD 구현체의 Value 관리 기법

### 3.1 저장 (KV-PUT)

KV-PUT은 `nvme_io_cmd.c`의 `handle_nvme_kv_put()`에서 처리하며, 핵심 정책은 **NVMe block(4 KiB) 단위 할당 + 동일 key overwrite 시 기존 LBA 재활용**이다.

```text
KV-PUT (OPC 0xA0)
 1. CDW10에서 key, CDW12에서 NLB, CDW13에서 value 길이 추출
 2. KvFtlPut() 호출:
    a. 기존 entry가 있고 새 value가 기존 block 수 이하로 들어가면
       → 기존 startLba를 그대로 재사용 (in-place overwrite)
    b. 그 외에는 bump allocator(nextKvLba)로 새 LBA 구간 할당
    c. hash index에 key → (startLba, valueLength) 갱신
 3. ReqTransNvmeToSlice()로 기존 GreedyFTL write pipeline에 전달
    → slice request 생성 → data buffer 할당 → RX DMA로 host value 수신
    → LSA→VSA 변환 → NAND program
```

초기 구현은 value 하나에 slice(16 KiB) 전체를 할당했기 때문에, benchmark의 4 KiB value 기준으로 PUT마다 12 KiB의 logical space 낭비가 있었다. 이를 NVMe block(4 KiB) 단위 할당으로 수정하여 낭비를 제거하였다. 또한 초기에는 동일 key 재-PUT 시에도 매번 새 LBA를 append하여 logical space가 단조 증가했는데, 새 value가 기존 자리에 들어가는 경우 기존 LBA 구간을 재사용하도록 개선하였다. 이 두 가지 개선이 없으면 10,000,000회 PUT 시 10M × 16 KiB ≈ 152.6 GiB의 logical space가 필요해 용량을 초과하지만, 개선 후에는 unique key 수만큼만(약 3.8M × 4 KiB ≈ 14.5 GiB) 소비된다.

동일 key에 대한 latest-write-wins는 index가 항상 "가장 최근 PUT의 위치"만 가리키는 것으로 보장된다. 기존 자리를 재사용하는 경우 데이터 자체가 덮어써지고, 새 자리를 할당하는 경우 index가 새 LBA로 갱신되므로 이전 value는 더 이상 도달할 수 없다.

### 3.2 검색 (KV-GET)

KV-GET은 `handle_nvme_kv_get()`에서 처리한다.

```text
KV-GET (OPC 0xA1)
 1. CDW10에서 key 추출
 2. FindKvIndexEntry()로 hash lookup
    - 실패 시: NAND 접근 없이 vendor-specific status
      (SCT=0x7, SC=0xC1)로 즉시 completion → host는 ENOSUCHKEY로 해석
 3. 성공 시: ReqTransKvGetToSlice(startLba, readNlb, valueLength) 호출
    → 기존 read pipeline 재사용 (LSA→VSA 변환, NAND read, TX DMA)
 4. TX DMA 완료 시 수동 completion으로 CQ DW0에 valueLength 기록
```

GET 경로에서 핵심적인 변경은 completion 처리다. 기존 block I/O는 DMA 발행 시 hardware auto-completion(`HOST_DMA_CMD_FIFO_REG`의 `autoCompletion` bit)을 켜서 DMA 완료와 동시에 hardware가 CQ entry를 자동 생성한다. 그러나 이 방식으로는 completion DW0에 value 길이를 실을 수 없다. 따라서 KV-GET request는 `NVME_DMA_INFO`에 추가한 `autoCompletion`/`completionSpecific` 필드를 통해 auto-completion을 끄고 value 길이를 request metadata에 보관하며, `CheckDoneNvmeDmaReq()`에서 TX DMA 완료를 확인한 뒤 `set_auto_nvme_cpl(cmdSlotTag, valueLength, 0)`로 수동 completion을 발행한다.

```text
KV index의 valueLength
  → reqPool.nvmeDmaInfo.completionSpecific에 저장
  → TX DMA 완료 시 CheckDoneNvmeDmaReq()에서 읽음
  → set_auto_nvme_cpl()로 CQ entry DW0에 기록
  → host의 ioctl cmd.result로 전달
```

즉 value 본문은 TX DMA로 host buffer에 전달되고, value 길이는 completion DW0라는 별도 경로로 전달된다. host benchmark는 이 길이만큼 buffer에서 value를 잘라내어 기대 패턴과 비교한다.

## 4. 10,000,000번 PUT (Keyspace 4,194,304) 테스트 결과 및 분석

### 4.1 실행 조건 및 결과

보드에서 펌웨어를 실행하고 host에서 `kv_bench`를 다음 조건으로 수행하였다.

```text
sudo ./kv_bench /dev/nvme0n1 10000000 4194304 1
```

결과 (캡처: `10Mtest.png`, `10Mtest_detail.png`):

```text
ops=10000000  keyspace=4194304
unique_keys=3807651
result: OK=3807651  FAIL=0  NO-SUCH-KEY=1
elapsed: 7.70468e+06 ms  (2595.82 IOPS est. for PUT+GET)
```

### 4.2 분석

- **정확성**: 10,000,000회 random PUT 후 기록된 모든 unique key(3,807,651개)에 대한 GET이 전부 최신 패턴과 일치하여 `FAIL=0`을 달성했다. 평균적으로 key 하나당 약 2.6회의 overwrite가 발생하는 workload이므로, latest-write-wins semantics와 LBA 재활용 경로가 모두 올바르게 동작함을 보여준다. 존재하지 않는 key에 대한 GET도 정확히 1회 `NO-SUCH-KEY`로 처리되었다.
- **unique key 수의 타당성**: keyspace N=4,194,304에서 ops=10,000,000회 균등 random 추출 시 기대되는 unique key 수는 N·(1−e^(−ops/N)) ≈ 4,194,304 × (1−e^(−2.384)) ≈ 3,807,000개로, 측정값 3,807,651과 거의 일치한다. 이는 benchmark가 의도대로 수행되었고 index에 유실이 없음을 간접 검증한다.
- **공간 효율**: 4 KiB block 단위 할당과 overwrite 시 LBA 재활용 덕분에, 10M회 PUT에도 실제 소비된 logical space는 unique key 분량(약 3.8M × 4 KiB ≈ 14.5 GiB)에 그쳤다. 초기 구현(16 KiB slice 할당 + 무조건 append)이었다면 약 152.6 GiB가 필요하여 용량 초과로 실패했을 조건이다.
- **Index 부하**: unique key 3.8M개에 대해 table 크기 8.39M이므로 최종 load factor는 약 45%로, linear probing의 평균 probe 길이가 짧게 유지되는 설계 범위 안에서 동작하였다.
- **성능**: 전체 수행 시간은 약 7,705초(약 2시간 8분), PUT+GET 합산 약 2,596 IOPS로 측정되었다. 본 경로는 synchronous ioctl(QD=1) 기반이므로 device 한계가 아닌 단일 outstanding command 구조가 주된 병목이다.
- **Host 측 조정**: 평가에 사용한 host 머신의 메모리가 작아 기존 `kv_bench`를 그대로 실행하면 OOM이 발생하였다. 이에 benchmark의 host 측 검증용 자료구조의 데이터 크기를 줄여 실행하였으며, device로 전달되는 command 형식과 4 KiB value 크기 등 평가 조건 자체는 변경하지 않았다.
