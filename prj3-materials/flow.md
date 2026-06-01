# Project 3 KV-SSD 정적 흐름 정리

이 문서는 현재 구현을 기준으로 KV-PUT, KV-GET 요청이 처리되는 흐름을 정적으로 정리한다. 실제 빌드와 보드 실행 결과는 포함하지 않는다.

## 1. Host Command 형식

Host benchmark는 `prj3-host-evaluation/nvme_passthru.cc`에서 NVMe passthrough command를 생성한다.

| 필드 | 의미 |
| --- | --- |
| `OPC = 0xA0` | KV-PUT |
| `OPC = 0xA1` | KV-GET |
| `CDW10` | 4-byte key |
| `CDW12` | zero-based NLB |
| `CDW13` | value byte length |
| PRP buffer | PUT value 또는 GET 결과 buffer |

현재 benchmark의 PUT value는 4 KiB이다. GET buffer는 최대 16 KiB로 할당된다.

GET 성공 시 firmware는 NVMe completion의 `DW0` (`specific`)에 실제 value 길이를 반환해야 한다. Host는 해당 길이만큼 buffer를 문자열로 구성하여 최신 PUT value와 비교한다.

## 2. Firmware 초기화

초기화 진입점은 `GreedyFTL/cosmos_app/src/ftl_config.c`의 `InitFTL()`이다.

```text
InitFTL()
  -> 기존 FTL metadata 및 NAND 초기화
  -> storageCapacity_L 계산
  -> InitKvStore()
```

`InitKvStore()`는 `GreedyFTL/cosmos_app/src/kv_store.c`에 있다.

```text
InitKvStore()
  -> kvIndexPtr를 KV_INDEX_ADDR에 연결
  -> append LSA counter를 0으로 초기화
  -> 모든 hash entry를 empty 상태로 초기화
```

KV index는 `memory_map.h`의 reserved DRAM 영역에 정적으로 배치된다.

## 3. KV Index

KV index는 in-memory open-addressing hash table이다.

```text
key -> latest logical slice address (LSA), value length
```

Entry 구성:

```c
typedef struct _KV_INDEX_ENTRY {
	unsigned int key;
	unsigned int logicalSliceAddr;
	unsigned int valueLength;
} KV_INDEX_ENTRY;
```

충돌은 linear probing으로 처리한다. `logicalSliceAddr == KV_LSA_NONE`이면 empty entry이다.

KV 계층은 VSA나 physical NAND 주소를 직접 저장하지 않는다. 기존 GreedyFTL이 아래 변환을 담당한다.

```text
KV key -> latest LSA -> VSA -> physical NAND
```

## 4. NVMe Dispatch

NVMe I/O command는 `GreedyFTL/cosmos_app/src/nvme/nvme_io_cmd.c`의 `handle_nvme_io_cmd()`에서 opcode별로 분기된다.

```text
OPC 0xA0 -> handle_nvme_kv_put()
OPC 0xA1 -> handle_nvme_kv_get()
```

각 KV case 뒤에는 `break`가 있어야 한다. 그렇지 않으면 다음 case와 `default`까지 실행되어 assertion failure가 발생한다.

## 5. KV-PUT 흐름

```text
Host
  -> ioctl(NVME_IOCTL_IO_CMD, KV-PUT)
  -> firmware handle_nvme_io_cmd()
  -> handle_nvme_kv_put()
```

`handle_nvme_kv_put()`의 처리:

```text
1. CDW10에서 key 추출
2. CDW12에서 NLB, CDW13에서 value length 추출
3. 현재 구현이 지원하는 4 KiB 이하 단일-block value인지 검사
4. AllocateKvLogicalSlice()로 append LSA 할당
5. PutKvIndexEntry()로 key -> latest LSA, value length 갱신
6. ReqTransNvmeToSlice()에 write 요청 전달
```

기존 FTL write pipeline:

```text
ReqTransNvmeToSlice()
  -> slice request 생성
  -> ReqTransSliceToLowLevel()
  -> data buffer 할당
  -> RX DMA request 생성
  -> host PRP buffer의 value를 device data buffer로 전송
  -> dirty data buffer eviction 시 NAND write
  -> 기존 FTL이 LSA -> VSA mapping 갱신
```

PUT은 append-only 방식이다. 동일 key가 다시 들어오면 새 LSA를 할당하고 index가 최신 LSA를 가리키도록 갱신한다.

## 6. KV-GET 흐름

```text
Host
  -> ioctl(NVME_IOCTL_IO_CMD, KV-GET)
  -> firmware handle_nvme_io_cmd()
  -> handle_nvme_kv_get()
```

`handle_nvme_kv_get()`의 처리:

```text
1. CDW10에서 key 추출
2. FindKvIndexEntry()로 최신 LSA와 value length 검색
3. key가 없으면 No-such-key completion 반환
4. key가 있으면 ReqTransKvGetToSlice()에 read 요청 전달
```

기존 FTL read pipeline:

```text
ReqTransKvGetToSlice()
  -> custom completion metadata를 포함한 slice request 생성
  -> ReqTransSliceToLowLevel()
  -> data buffer hit 검사
  -> miss이면 LSA -> VSA 변환 후 NAND read
  -> TX DMA request 생성
  -> device data buffer의 value를 host PRP buffer로 전송
```

KV-GET은 일반 block read와 달리 TX DMA auto-completion을 끈다. DMA 완료를 확인한 뒤 firmware가 직접 completion을 생성한다.

```text
CheckDoneNvmeDmaReq()
  -> KV GET TX DMA 완료 확인
  -> set_auto_nvme_cpl(cmdSlotTag, valueLength, 0)
```

이때 completion `specific` 값이 host에 실제 value 길이로 전달된다.

## 7. No-Such-Key 흐름

KV-GET key가 index에 없으면 NAND read나 DMA를 수행하지 않는다.

```text
FindKvIndexEntry() == miss
  -> complete_nvme_kv_error()
  -> vendor-specific status 반환
```

현재 firmware는 host의 `ENOSUCHKEY == 0x7C1` 조건에 맞추기 위해 아래 status를 사용한다.

```text
SCT = 0x7 (vendor specific)
SC  = 0xC1
Linux ioctl return status = 0x7C1
```

## 8. 정적 확인 필요 사항

아래 항목은 실제 빌드와 보드 실행 전에 코드 관점에서 추가 확인이 필요하다.

1. PUT index 갱신 시점  
   현재 index는 RX DMA 완료 전에 갱신된다. Host benchmark는 PUT 완료 후 다음 command를 보내므로 일반 실행에서는 문제가 없을 가능성이 높지만, 엄밀하게는 RX DMA 완료 후 index를 노출하는 방식이 더 안전하다.

2. Value 크기  
   현재 구현은 benchmark의 4 KiB PUT에 맞춰 단일 NVMe block value만 지원한다. 4 KiB 초과 value 지원은 별도 확장이 필요하다.

3. Append log 수명  
   PUT마다 새 LSA를 소비한다. append 공간을 모두 사용하면 `SC_CAPACITY_EXCEEDED`를 반환한다. 장시간 반복 실행을 위한 log 재사용 정책은 아직 없다.

4. KV index persistence  
   index는 DRAM에만 존재한다. firmware reset 이후 복구되지 않는다.

5. 정적 DRAM 사용량  
   KV index는 `8,388,608` entries를 사용한다. entry당 12 bytes이므로 약 96 MiB이다. reserved DRAM 범위와 겹치지 않는지 `memory_map.h`의 제한 검사를 통해 확인하도록 구성했다.

## 9. 보드 검증 시 확인할 출력

실제 실행은 사용자가 직접 수행한다. 기본 benchmark의 성공 조건은 다음과 같다.

```text
result: OK=... FAIL=0 NO-SUCH-KEY=1
```

특히 아래 동작을 확인해야 한다.

- random PUT이 중단 없이 완료되는가
- 동일 key에 대한 GET이 가장 최근 value를 반환하는가
- absent key GET이 `No such key`로 처리되는가
- device abort, assertion failure, hang이 없는가

