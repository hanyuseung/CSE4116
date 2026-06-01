# Project 3: KV-SSD 구현 계획

## 1. 목표

Cosmos+ OpenSSD 펌웨어에 최소 기능의 Key-Value SSD 인터페이스를 추가한다.

- Host benchmark가 전달하는 NVMe KV-PUT, KV-GET 명령을 처리한다.
- 동일한 key에 여러 번 PUT한 경우 가장 최근 value를 반환한다.
- 존재하지 않는 key를 GET한 경우 host가 기대하는 `No such key` 상태를 반환한다.
- 기존 GreedyFTL의 주소 변환, NAND 접근, GC 흐름을 최대한 재사용한다.

## 2. Host Command 규약

`prj3-materials/prj3-host-evaluation/nvme_passthru.{h,cc}`를 기준으로 아래 규약을 사용한다.

| 항목 | 의미 |
| --- | --- |
| `OPC = 0xA0` | KV-PUT |
| `OPC = 0xA1` | KV-GET |
| `CDW10` | 4-byte key |
| `CDW12` | zero-based NLB |
| `CDW13` | value byte length |
| PUT data buffer | 현재 benchmark 기준 4 KiB |
| GET data buffer | 최대 16 KiB |
| GET completion `DW0` (`specific`) | 실제 value byte length |
| absent-key status | host의 `ENOSUCHKEY == 0x7C1` 처리 조건과 일치해야 함 |

Host benchmark는 random PUT 이후 key별 최신 value를 기억하고, GET 결과를 비교한다. 따라서 latest-write-wins semantics가 필수이다.

## 3. KV 저장 구조

### 3.1 전체 흐름

기존 FTL이 이미 `LSA -> VSA -> physical NAND` 변환과 GC를 담당한다. KV 계층에서는 VSA를 직접 관리하지 않고 아래 구조를 사용한다.

```text
KV index: key -> latest LSA

PUT:
host KV-PUT -> key 추출 -> append LSA 할당 -> 기존 FTL write 경로 호출
            -> DMA 및 요청 완료 확인 -> index를 최신 LSA로 갱신

GET:
host KV-GET -> key 추출 -> index lookup
            -> key가 없으면 No-such-key completion 반환
            -> key가 있으면 최신 LSA로 기존 FTL read 경로 호출
            -> value 길이를 completion DW0에 반환
```

### 3.2 Index

정적으로 할당한 in-memory hash table을 사용한다.

- Entry는 최소한 `key`, `latest LSA`, `value length`, `valid` 정보를 가진다.
- Collision 처리 방식을 명시하고 구현한다. 초기 구현은 open addressing의 linear probing을 고려한다.
- 초기화 함수에서 모든 entry를 empty 상태로 설정한다.
- 최대 hidden evaluation 조건인 keyspace `4,194,304`를 고려하여 entry 수와 DRAM 사용량을 계산한다.
- DRAM 영역은 `cosmos_app/src/memory_map.h`에 명시적으로 예약하고, 기존 영역과 겹치지 않는지 검사한다.

### 3.3 Value 관리

value는 기존 FTL logical address space 위에 append-only log 형태로 저장한다.

- PUT마다 새로운 logical 위치를 할당한다.
- index는 해당 key의 가장 최근 LSA만 가리킨다.
- overwrite된 이전 value의 NAND 위치 정리와 GC는 기존 FTL 흐름을 활용한다.
- 현재 benchmark의 value는 4 KiB이지만, command 규약상 `CDW12`, `CDW13`을 읽어 길이를 검증한다.
- FTL mapping unit은 16 KiB slice이므로 4 KiB value 배치 방식과 LSA 증가 단위를 구현 전에 확정한다.

## 4. 구현 단계

### 4.1 Opcode 등록 및 dispatch 수정

대상 파일:

- `GreedyFTL/cosmos_app/src/nvme/nvme.h`
- `GreedyFTL/cosmos_app/src/nvme/nvme_io_cmd.c`
- `GreedyFTL/cosmos_app/src/nvme/nvme_io_cmd.h`

opcode 정의:

```c
#define IO_NVM_KV_PUT 0xA0
#define IO_NVM_KV_GET 0xA1
```

KV handler 호출 뒤 반드시 `break`를 추가한다. 현재 코드처럼 `break`가 없으면 GET이 PUT과 `default`까지 연쇄 실행되어 `ASSERT(0)`으로 종료된다.

```c
case IO_NVM_KV_GET:
	handle_nvme_kv_get(nvmeCmd->cmdSlotTag, nvmeIOCmd);
	break;
case IO_NVM_KV_PUT:
	handle_nvme_kv_put(nvmeCmd->cmdSlotTag, nvmeIOCmd);
	break;
```

### 4.2 KV 모듈 추가

새 모듈을 추가한다.

- `GreedyFTL/cosmos_app/src/kv_store.h`
- `GreedyFTL/cosmos_app/src/kv_store.c`

담당 기능:

- KV index 자료구조 정의
- 정적 DRAM 주소 연결
- index 초기화
- hash lookup 및 insert/update
- append LSA 할당
- value length 관리

초기화 함수는 FTL 초기화 흐름에 연결한다.

### 4.3 Memory map 예약

대상 파일:

- `GreedyFTL/cosmos_app/src/memory_map.h`
- 필요 시 `GreedyFTL/cosmos_app/src/ftl_config.c`

확인 사항:

- KV index 영역이 기존 FTL metadata 영역과 겹치지 않는가
- `DRAM_END_ADDR`를 넘지 않는가
- hash entry 수를 hidden evaluation keyspace까지 수용할 수 있는가
- entry 크기와 전체 사용량이 현실적인가

### 4.4 PUT 경로 구현

1. `CDW10`에서 key를 추출한다.
2. `CDW12`, `CDW13`에서 value 크기를 확인한다.
3. append LSA를 할당한다.
4. 기존 write request pipeline을 재사용하여 host buffer를 NAND-backed logical space에 기록한다.
5. DMA 완료 및 NVMe completion 시점을 확인한다.
6. PUT이 정상 접수된 뒤 index의 `key -> latest LSA`와 value length를 갱신한다.

주의: host buffer 수신 전에 GET에서 새 위치를 읽도록 노출하면 안 된다.

### 4.5 GET 경로 구현

1. `CDW10`에서 key를 추출한다.
2. KV index에서 최신 LSA와 value length를 찾는다.
3. key가 없으면 host가 `-ENOSUCHKEY`로 해석할 수 있는 NVMe completion status를 반환한다.
4. key가 있으면 기존 read request pipeline을 재사용한다.
5. GET completion의 `specific` 필드에 실제 value 길이를 반환한다.

주의: 기존 block read의 auto-completion만 그대로 사용하면 GET value length를 반환하지 못할 수 있다. KV GET에 필요한 completion 생성 시점을 별도로 확인한다.

### 4.6 DMA 및 completion 검증

다음 코드를 읽고 KV 경로에 필요한 변경 범위를 확정한다.

- `GreedyFTL/cosmos_app/src/request_transform.c`
- `GreedyFTL/cosmos_app/src/nvme/host_lld.c`
- `GreedyFTL/cosmos_app/src/nvme/host_lld.h`

확인 사항:

- PUT의 RX DMA가 끝나는 시점
- GET의 TX DMA가 끝나는 시점
- auto-completion 사용 가능 여부
- GET 성공 시 `specific = value length` 설정 방법
- absent key 시 DMA 없이 오류 completion을 반환하는 방법

## 5. 검증 계획

### 5.1 정적 검토

- KV command dispatch에 `break`가 있는지 확인한다.
- 신규 DRAM 영역의 충돌 여부를 계산한다.
- PUT, GET, absent-key 경로에서 completion이 정확히 한 번 발생하는지 확인한다.
- index 초기화, collision 처리, 최대 entry 도달 시 동작을 확인한다.
- 전체 흐름을 `prj3-materials/flow.md`로 작성한다.

### 5.2 실행 제한

- Codex는 `make`, 컴파일러, 보드 접근, bitstream load, NVMe device 대상 benchmark 명령을 실행하지 않는다.
- Codex는 코드의 전체 흐름만 정적으로 확인하고 `prj3-materials/flow.md`를 작성한다.
- 실제 코드 빌드와 보드 실행 검증은 사용자가 직접 수행한다.

### 5.3 보드 실행 검증

기본 benchmark:

```bash
sudo ./kv_bench /dev/nvme1n1 10000 4096 1
```

기대 결과:

```text
result: OK=... FAIL=0 NO-SUCH-KEY=1
```

추가로 작은 입력부터 단계적으로 확인한 뒤 최대 평가 조건을 검증한다.

```text
operations = 1,000,000
keyspace   = 4,194,304
```

## 6. 보고서

### 6.1 작성 조건

사용자가 실제 보드에서 구현 완료를 확인했다고 말하기 전까지 보고서는 작성하지 않는다.

### 6.2 작성 가이드

보고서는 markdown으로 먼저 작성한 뒤 PDF로 export한다.

1. 프로젝트 개요 및 목표
2. KV-PUT, KV-GET command 형식
3. 기존 block I/O command와 KV command의 차이
4. 새로운 storage abstraction이 host software stack에 미치는 영향
5. KV index 구조, 선정 이유, 장단점, DRAM 사용량
6. append-only value 관리 방식과 latest-write-wins 처리
7. DMA 및 completion 처리 흐름
8. 기본 benchmark와 최대 조건 benchmark 결과 캡처 및 분석
9. 현재 구현의 한계와 개선 방향
