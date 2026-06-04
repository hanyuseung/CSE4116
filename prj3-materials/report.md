# Project 3 Report: Key-Value Interface Enablement and KV-SSD Firmware Implementation

## 1. 프로젝트 개요 및 목표

본 프로젝트의 목표는 Cosmos+ OpenSSD의 기존 block 기반 GreedyFTL 펌웨어에 Key-Value SSD 인터페이스를 추가하는 것이다. 기존 NVMe block I/O는 Logical Block Address(LBA)를 기준으로 데이터를 읽고 쓰지만, KV-SSD에서는 host가 직접 정의한 key를 기준으로 value를 저장하고 검색한다.

본 구현은 제공된 `kv_bench` host benchmark가 발행하는 NVMe passthrough 기반 KV-PUT, KV-GET 명령을 처리하도록 펌웨어를 확장하였다. 최소 기능 목표는 다음과 같다.

- KV-PUT command를 해석하여 value를 SSD 내부 logical address space에 저장한다.
- KV-GET command를 해석하여 해당 key의 최신 value를 반환한다.
- 동일 key에 여러 번 PUT이 발생하면 latest-write-wins semantics를 보장한다.
- 존재하지 않는 key에 대한 GET은 host가 인식 가능한 `No such key` 상태로 완료한다.
- 기존 GreedyFTL의 LSA-to-VSA 변환, NAND 접근, data buffer, DMA request pipeline을 최대한 재사용한다.

## 2. KV-PUT, KV-GET Command 형식

Host benchmark는 `prj3-host-evaluation/nvme_passthru.cc`에서 Linux `NVME_IOCTL_IO_CMD` ioctl을 사용하여 NVMe passthrough command를 발행한다. 본 프로젝트에서 사용하는 command 형식은 다음과 같다.

| 필드 | 의미 |
| --- | --- |
| `OPC = 0xA0` | KV-PUT |
| `OPC = 0xA1` | KV-GET |
| `CDW10` | 4-byte key |
| `CDW12` | zero-based NLB |
| `CDW13` | value byte length |
| PRP buffer | PUT value 또는 GET result buffer |

Host의 PUT value는 benchmark 기준 4 KiB로 고정되어 있다. GET buffer는 최대 16 KiB로 할당되며, device는 completion `DW0`에 실제 value length를 반환한다. Host는 completion result 값을 기준으로 buffer에서 value를 추출한다.

## 3. 기존 Block I/O와 KV Command의 차이

기존 block I/O는 host가 LBA를 직접 지정한다.

```text
Host block write/read
  -> LBA
  -> GreedyFTL LSA/VSA translation
  -> NAND physical location
```

반면 KV command에서는 host가 LBA를 제공하지 않는다. Host는 key만 전달하고, firmware가 key에 대응하는 최신 logical location을 관리한다.

```text
Host KV-PUT/KV-GET
  -> key
  -> KV index lookup/update
  -> LSA
  -> GreedyFTL LSA/VSA translation
  -> NAND physical location
```

이 차이로 인해 host software stack은 파일 시스템 또는 DB 내부 index를 통해 block 위치를 직접 관리할 필요가 줄어든다. Key lookup과 value location 관리 일부가 SSD firmware 내부로 이동하므로, host는 key-value operation이라는 더 높은 수준의 storage abstraction을 사용할 수 있다.

## 4. KV Index 설계

### 4.1 Index 구조

본 구현은 DRAM에 정적으로 배치한 in-memory hash table을 KV index로 사용한다.

```text
key -> latest logical slice address (LSA), value length
```

Entry 구조는 다음과 같다.

```c
typedef struct _KV_INDEX_ENTRY {
	unsigned int key;
	unsigned int logicalSliceAddr;
	unsigned int valueLength;
} KV_INDEX_ENTRY;
```

Hash function은 32-bit key에 multiplicative hashing을 적용한 뒤 table mask를 사용한다. Collision은 linear probing으로 처리한다. `logicalSliceAddr == KV_LSA_NONE`이면 해당 entry는 비어 있는 것으로 간주한다.

### 4.2 DRAM 배치

KV index는 `memory_map.h`에 정의한 reserved DRAM 영역에 배치한다.

```text
KV_INDEX_ADDR = RESERVED0_START_ADDR
```

현재 table entry 수는 `8,388,608`개이고, entry당 크기는 12 bytes이다. 따라서 KV index의 전체 크기는 약 96 MiB이다.

```text
8,388,608 entries * 12 bytes = 100,663,296 bytes ~= 96 MiB
```

이 크기는 hidden evaluation에서 사용될 수 있는 큰 keyspace와 random workload를 고려한 선택이다. Open addressing table의 load factor를 낮게 유지하여 lookup과 insert의 probing 비용을 줄이는 것이 목적이다.

### 4.3 선택 이유

Hash table을 선택한 이유는 다음과 같다.

- Random key workload에서 평균 O(1) lookup/update를 기대할 수 있다.
- 구현이 단순하여 firmware 환경에서 debugging이 쉽다.
- benchmark는 reset 이후 persistence를 요구하지 않으므로 in-memory index로 충분하다.
- Key 크기가 4 bytes로 고정되어 있어 entry 구조가 단순하다.

단점은 다음과 같다.

- Firmware reset 이후 index가 복구되지 않는다.
- DRAM 사용량이 크다.
- Entry 삭제와 log cleaning을 지원하지 않는다.
- Load factor가 높아지면 probing 비용이 증가한다.

## 5. Value 관리 방식

Value는 기존 GreedyFTL의 logical address space 위에 append-only 방식으로 저장한다. KV 계층은 physical NAND 주소 또는 VSA를 직접 관리하지 않는다. 대신 PUT마다 새로운 LSA를 할당하고, 기존 FTL이 `LSA -> VSA -> physical NAND` 변환을 처리하도록 한다.

```text
PUT key=K, value=V
  -> append LSA 할당
  -> key K의 index entry를 latest LSA로 갱신
  -> 기존 FTL write pipeline으로 value 저장
```

동일 key에 대한 overwrite는 기존 value를 직접 수정하지 않고 새 LSA에 append한다.

```text
PUT key=7, value=A -> key 7 maps to LSA 0
PUT key=7, value=B -> key 7 maps to LSA 1
GET key=7          -> LSA 1에서 value B 반환
```

이 방식은 구현이 단순하고 latest-write-wins semantics를 쉽게 보장할 수 있다. 반면 이전 value가 사용하던 LSA는 KV 계층에서 즉시 회수하지 않으므로, 장시간 실행 workload에서는 log cleaning 또는 compaction 정책이 필요하다.

## 6. KV-PUT 처리 흐름

KV-PUT은 `nvme_io_cmd.c`의 `handle_nvme_kv_put()`에서 처리한다.

```text
1. CDW10에서 key 추출
2. CDW12에서 NLB 추출
3. CDW13에서 value length 추출
4. value size가 현재 지원 범위인지 확인
5. AllocateKvLogicalSlice()로 append LSA 할당
6. PutKvIndexEntry()로 key -> latest LSA, value length 갱신
7. ReqTransNvmeToSlice()를 통해 기존 write pipeline으로 전달
```

기존 write pipeline은 slice request를 만들고, data buffer를 할당한 뒤 RX DMA를 통해 host PRP buffer의 value를 device memory로 가져온다. 이후 GreedyFTL의 data buffer와 NAND write 경로가 기존 block write와 동일하게 동작한다.

## 7. KV-GET 처리 흐름

KV-GET은 `nvme_io_cmd.c`의 `handle_nvme_kv_get()`에서 처리한다.

```text
1. CDW10에서 key 추출
2. FindKvIndexEntry()로 latest LSA와 value length 검색
3. key가 없으면 No-such-key completion 반환
4. key가 있으면 ReqTransKvGetToSlice()로 read request 생성
5. TX DMA 완료 후 completion DW0에 value length 반환
```

GET path는 기존 read pipeline을 재사용한다. 다만 일반 block read와 달리 completion에 value length를 실어야 하므로 DMA completion 방식이 다르다.

## 8. DMA 및 Completion 처리

기존 GreedyFTL의 block read/write는 DMA request를 만들 때 hardware auto-completion을 켠다.

```text
set_auto_tx_dma(..., autoCompletion = 1)
set_auto_rx_dma(..., autoCompletion = 1)
```

`autoCompletion` bit는 `HOST_DMA_CMD_FIFO_REG`의 `dword[3]` bit 13에 위치한다. 이 bit가 1이면 FPGA NVMe host controller hardware가 DMA 완료 후 completion queue entry를 자동 생성한다.

KV-GET에서는 host에게 실제 value length를 반환해야 한다. Hardware auto-completion만 사용하면 completion `DW0`에 원하는 값을 넣을 수 없으므로, KV-GET request는 auto-completion을 끈다.

```text
ReqTransKvGetToSlice()
  -> autoCompletion = 0
  -> completionSpecific = valueLength
```

`completionSpecific`은 DMA command FIFO로 전달되지 않는다. 대신 firmware의 request metadata에 저장된다.

```text
KV index valueLength
  -> reqPool.nvmeDmaInfo.completionSpecific
  -> DMA 완료 시 CheckDoneNvmeDmaReq()에서 읽음
  -> set_auto_nvme_cpl(cmdSlotTag, valueLength, 0)
  -> NVMe CQ DW0
  -> host cmd.result
```

즉 value 본문은 TX DMA를 통해 host buffer로 이동하고, value 길이는 completion queue의 `DW0`를 통해 별도로 전달된다.

## 9. No-Such-Key 처리

GET 요청의 key가 index에 존재하지 않으면 NAND read나 DMA를 수행하지 않는다. Firmware는 vendor-specific NVMe status를 completion으로 반환한다.

```text
SCT = 0x7
SC  = 0xC1
Linux ioctl return status = 0x7C1
```

Host benchmark는 이 값을 `ENOSUCHKEY`로 해석하여 `NO-SUCH-KEY` count를 증가시킨다. 이 동작은 absent key에 대한 정상 동작으로 간주된다.

## 10. 평가 결과

보드에서 firmware를 실행하고 제공된 host benchmark를 통해 기능 검증을 완료하였다. 사용자가 확인한 결과 모든 테스트를 통과하였다.

기본 benchmark 조건:

```text
operations = 10,000
keyspace   = 4,096
expected   = FAIL=0, NO-SUCH-KEY=1
```

최대 평가 조건:

```text
operations = 1,000,000
keyspace   = 4,194,304
expected   = FAIL=0, NO-SUCH-KEY=1
```

결과 캡처 삽입 위치:

```text
[Figure] 기본 benchmark 결과 캡처
[Figure] 최대 조건 benchmark 결과 캡처
```

분석:

- Random PUT workload가 device abort나 firmware crash 없이 완료되었다.
- 동일 key에 대한 overwrite 이후 GET이 최신 value를 반환하였다.
- 존재하지 않는 key에 대해 `No such key` semantics가 정상적으로 확인되었다.
- 최종 summary에서 `FAIL=0`을 확인하여 data mismatch가 발생하지 않았음을 검증하였다.

## 11. 한계 및 개선 방향

### 11.1 Value 크기 제한

현재 구현은 benchmark에 맞추어 4 KiB 이하 value를 대상으로 한다. 일반적인 KV-SSD라면 value 크기가 고정되지 않으므로, `key -> start LSA, value length`뿐 아니라 multi-slice extent 관리가 필요하다. 예를 들어 4 MiB value는 16 KiB slice 기준 256개의 연속 slice가 필요하다.

개선 방향:

- `AllocateKvLogicalSlices(sliceCount)` 구현
- `key -> start LSA, value length, slice count` 관리
- 큰 value에 대한 DMA split 및 multi-request completion 처리

### 11.2 Persistence

현재 KV index는 DRAM에만 존재한다. Firmware reset 이후 key-value mapping을 복구할 수 없다.

개선 방향:

- NAND-backed index checkpoint 추가
- append log record에 key와 value metadata 저장
- boot 시 log scan을 통한 index reconstruction

### 11.3 Space Reclamation

PUT마다 새 LSA를 소비하는 append-only 구조이므로 오래된 value가 차지한 logical space를 즉시 재사용하지 않는다.

개선 방향:

- invalidated KV entry 추적
- log segment 단위 compaction
- GreedyFTL GC와 KV metadata의 연동

### 11.4 PUT Index Update 시점

현재 PUT path는 write request를 pipeline에 넣기 전에 index를 갱신한다. Host benchmark는 synchronous ioctl 흐름으로 동작하므로 통과하지만, 더 엄밀한 설계에서는 RX DMA 완료 후 index를 갱신하는 것이 안전하다.

개선 방향:

- PUT request metadata에 key와 target LSA 저장
- RX DMA 완료 시점에 index commit
- 실패 시 index rollback 처리

## 12. 결론

본 프로젝트에서는 Cosmos+ OpenSSD GreedyFTL 펌웨어에 KV-PUT, KV-GET command path를 추가하고, in-memory hash index와 append-only value log를 통해 최소 기능의 KV-SSD를 구현하였다. 기존 FTL의 logical address translation과 DMA/NAND pipeline을 재사용함으로써 구현 범위를 줄이면서도 key 기반 storage abstraction을 제공할 수 있었다.

특히 KV-GET에서는 value 본문 전송과 value length 반환이 서로 다른 경로를 사용한다는 점이 핵심이다. Value는 TX DMA를 통해 host buffer로 전달하고, value length는 수동 NVMe completion을 통해 completion `DW0`에 기록하였다. 이를 통해 host benchmark가 최신 value를 정확히 비교할 수 있었다.

최종적으로 제공된 benchmark에서 crash, hang, data mismatch 없이 모든 테스트를 통과하였다.

