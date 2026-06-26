# Project #4 Report

## 1. 프로젝트 개요 및 목표

본 프로젝트의 목표는 Cosmos+ OpenSSD 플랫폼에서 표준 리눅스 파일시스템인 ext4를 사용할 수 있도록 host-side Linux kernel을 수정하고, 그 위에서 동작하는 파일 기반 Key-Value Store(KVS)의 성능을 Project #3의 KV-SSD 방식과 비교하는 것이다.

Task1에서는 OpenSSD 장치에 대해 `mkfs.ext4`가 실패하는 원인을 분석하고, Linux NVMe host driver를 수정하여 ext4 파일시스템 생성이 정상적으로 완료되도록 하였다. Task2에서는 ext4 위에서 regular file을 이용해 KV semantics를 구현한 `kv_fs_bench`를 실행하고, `KV_FSYNC_PERIOD` 값에 따른 성능 변화를 측정하였다. 마지막으로 동일한 workload를 Project #3의 KV-SSD benchmark와 비교하여 파일시스템 기반 I/O path와 KV-native I/O path의 차이를 분석하였다.

## 2. Task1 - 리눅스 커널 수정 및 OpenSSD상 ext4 구동 지원

### 2.1 커널 수정 전 `mkfs.ext4`가 실패하는 이유와 해결 방법

문제의 핵심 원인은 `mkfs.ext4`가 파일시스템 초기화 과정에서 discard/TRIM 요청을 NVMe DSM(Data Set Management) 명령으로 전송하지만, Cosmos+ OpenSSD firmware가 해당 discard 요청을 올바르게 처리하지 못한다는 점이다.

`mkfs.ext4`는 새 파일시스템을 만들 때 기존 블록을 초기화하거나 사용하지 않는 영역을 장치에 알려 주기 위해 discard 요청을 보낼 수 있다. Linux NVMe driver는 장치가 Identify Controller 정보에서 DSM 기능을 지원한다고 보고하면 block layer에 discard capability를 노출한다. 이후 `mkfs.ext4`는 `/dev/nvme0n1`에 대해 discard를 수행하고, 이 요청이 NVMe DSM command로 OpenSSD에 전달된다. 그러나 Cosmos+ OpenSSD는 해당 DSM discard path를 정상 구현하지 않았기 때문에 파일시스템 생성 중 오류가 발생한다.

가능한 해결 방법은 다음과 같다.

1. OpenSSD firmware에 DSM discard/TRIM 명령 처리를 구현한다. 장치가 advertise한 기능을 실제로 지원하게 만드는 가장 근본적인 해결책이지만, Project4의 요구사항은 host-side kernel modification이므로 이번 과제 범위와는 맞지 않는다.
2. `mkfs.ext4 -K` 또는 `mkfs.ext4 -E nodiscard` 옵션을 사용하여 discard를 우회한다. 빠르게 문제를 피할 수는 있지만, 사용자가 항상 특수 옵션을 기억해야 하며 일반적인 `mkfs.ext4 -F /dev/nvme0n1` 명령이 여전히 실패한다.
3. Linux NVMe driver에서 Cosmos+ OpenSSD에 대한 device quirk를 추가하여 discard 기능을 비활성화한다. 이 방법은 특정 장치의 잘못된 기능 광고 또는 미구현 기능을 host driver에서 보정하는 방식이며, 표준 `mkfs.ext4` 명령을 그대로 사용할 수 있으므로 이번 과제에 가장 적절한 해결책이다.

따라서 본 프로젝트에서는 세 번째 방법을 사용하였다. Cosmos+ OpenSSD를 식별하면 `NVME_QUIRK_NO_DISCARD` quirk bit를 켜고, 해당 quirk가 설정된 컨트롤러에 대해서는 discard 관련 queue limit을 설정하지 않도록 수정하였다.

### 2.2 수정한 소스 파일 Before vs. After

수정 파일은 `drivers/nvme/host/nvme.h`와 `drivers/nvme/host/core.c` 두 개이다.

#### 2.2.1 `drivers/nvme/host/nvme.h`

Before:

```c
NVME_QUIRK_FORCE_NO_SIMPLE_SUSPEND = (1 << 20),
```

기존 NVMe quirk enum에는 discard 기능만 비활성화하기 위한 별도 flag가 없었다. 따라서 특정 장치가 DSM discard를 제대로 처리하지 못하더라도 이를 장치별 예외로 표현할 방법이 없었다.

After:

```c
/*
 * The controller advertises DSM discard, but does not handle it
 * correctly. - embe prj4
 */
NVME_QUIRK_NO_DISCARD = (1 << 21),
```

새로운 quirk bit `NVME_QUIRK_NO_DISCARD`를 추가하였다. 이 flag는 컨트롤러가 DSM discard 기능을 advertise하더라도 host driver가 해당 장치에 discard 기능을 노출하지 않도록 하기 위한 장치별 예외 처리 플래그이다.

#### 2.2.2 `drivers/nvme/host/core.c` - Cosmos+ OpenSSD 식별 helper 추가

Before:

```c
static bool quirk_matches(const struct nvme_id_ctrl *id,
			  const struct nvme_core_quirk_entry *q)
{
	return q->vid == le16_to_cpu(id->vid) &&
		string_matches(id->mn, q->mn, sizeof(id->mn)) &&
		string_matches(id->fr, q->fr, sizeof(id->fr));
}
```

기존 코드는 일반적인 `core_quirks` table matching만 수행하였다. Cosmos+ OpenSSD를 별도로 식별하는 helper는 없었다.

After:

```c
static bool nvme_is_cosmos_openssd(const struct nvme_id_ctrl *id)
{
	return !strncasecmp(id->mn, "COSMOS+", 7);
}
```

Identify Controller의 model number(`id->mn`)가 `"COSMOS+"`로 시작하는지 검사하는 helper를 추가하였다. 이를 통해 Cosmos+ OpenSSD에 대해서만 discard 비활성화 quirk를 적용할 수 있다.

#### 2.2.3 `drivers/nvme/host/core.c` - Cosmos+ OpenSSD에 quirk bit 설정

Before:

```c
for (i = 0; i < ARRAY_SIZE(core_quirks); i++) {
	if (quirk_matches(id, &core_quirks[i]))
		ctrl->quirks |= core_quirks[i].quirks;
}
```

기존에는 predefined quirk table에 matching되는 장치에 대해서만 quirk가 설정되었다.

After:

```c
if (nvme_is_cosmos_openssd(id))
	ctrl->quirks |= NVME_QUIRK_NO_DISCARD;

for (i = 0; i < ARRAY_SIZE(core_quirks); i++) {
	if (quirk_matches(id, &core_quirks[i]))
		ctrl->quirks |= core_quirks[i].quirks;
}
```

컨트롤러가 처음 identify될 때 Cosmos+ OpenSSD이면 `NVME_QUIRK_NO_DISCARD`를 설정하도록 하였다. 이후 discard limit 초기화 과정에서 이 quirk를 참조한다.

#### 2.2.4 `drivers/nvme/host/core.c` - discard capability 비활성화

Before:

```c
if (ctrl->oncs & NVME_CTRL_ONCS_DSM) {
	ctrl->max_discard_sectors = UINT_MAX;
	ctrl->max_discard_segments = NVME_DSM_MAX_RANGES;
} else {
	ctrl->max_discard_sectors = 0;
	ctrl->max_discard_segments = 0;
}

if (nvme_ctrl_limited_cns(ctrl))
	return 0;
```

기존 driver는 컨트롤러가 ONCS에서 DSM support bit를 보고하면 discard를 사용할 수 있다고 판단하고, block layer에 discard limit을 설정하였다. Cosmos+ OpenSSD가 DSM을 올바르게 처리하지 못하더라도 이 경로가 활성화되어 `mkfs.ext4`의 discard 요청이 장치로 전달되었다.

After:

```c
if ((ctrl->oncs & NVME_CTRL_ONCS_DSM) &&
    !(ctrl->quirks & NVME_QUIRK_NO_DISCARD)) {
	ctrl->max_discard_sectors = UINT_MAX;
	ctrl->max_discard_segments = NVME_DSM_MAX_RANGES;
} else {
	ctrl->max_discard_sectors = 0;
	ctrl->max_discard_segments = 0;
}

if (nvme_ctrl_limited_cns(ctrl) ||
    (ctrl->quirks & NVME_QUIRK_NO_DISCARD))
	return 0;
```

`NVME_QUIRK_NO_DISCARD`가 설정된 경우에는 `max_discard_sectors`와 `max_discard_segments`를 0으로 유지한다. 이후 namespace disk 정보 설정 과정에서 `nvme_config_discard()`가 discard queue flag를 clear하므로, block layer는 해당 NVMe namespace에 discard capability를 노출하지 않는다. 또한 quirk가 설정된 경우 discard 관련 추가 identify 작업으로 넘어가기 전에 조기 return하도록 하였다.

수정 후 `sudo mkfs.ext4 -F /dev/nvme0n1`을 실행한 결과 ext4 파일시스템 생성이 정상 완료되었다.

```text
Creating journal (16384 blocks): done
Writing superblocks and filesystem accounting information: done
```

## 3. Task2 - 파일 기반 Key-Value Store vs. KV-SSD 비교 평가

### 3.1 `kv_fs_bench`를 통한 파일 기반 Key-Value Store 성능 평가

파일 기반 KVS는 ext4 위에 value log와 index log를 regular file로 저장한다. 각 PUT 이후 durability 보장을 위해 `fsync()`를 호출하며, `kv_fs_backend.h`의 `KV_FSYNC_PERIOD` 값을 변경하여 fsync 빈도를 조절하였다.

실험 parameter는 모든 경우 동일하게 `(number of operations, keyspace) = (100000, 524288)`로 설정하였다.

| KV_FSYNC_PERIOD | OK | FAIL | NO-SUCH-KEY | Elapsed (ms) | IOPS |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 91121 | 0 | 1 | 238693 | 837.897 |
| 10 | 91121 | 0 | 1 | 33527.5 | 5965.26 |
| 100 | 91121 | 0 | 1 | 9499.36 | 21054.1 |
| 1000 | 91121 | 0 | 1 | 4502.2 | 44422.7 |

모든 설정에서 `FAIL=0`으로 correctness failure는 발생하지 않았다. `OK=91121`은 random key workload에서 실제로 마지막 값 검증 대상이 된 unique key 수이며, `NO-SUCH-KEY=1`은 존재하지 않는 key에 대한 GET 실패를 정상적으로 처리했음을 의미한다.

실행 결과는 다음과 같다.

```bash
KV_FSYNC_PERIOD 1000
[INFO] Mounted /dev/nvme0n1 on /mnt/cosmos_test
[INFO] GET failed @ key `{` (No such key)
-----------------------------------------------
File-System-Based KVS Benchmark
-----------------------------------------------
dev=/dev/nvme0n1 mountpoint=/mnt/cosmos_test
ops=100000 keyspace=524288
result: OK=91121 FAIL=0 NO-SUCH-KEY=1
elapsed: 4502.2 ms  (44422.7 IOPS est. for PUT+GET )
-----------------------------------------------
[INFO] Unmounted /mnt/cosmos_test

KV_FSYNC_PERIOD 100
[INFO] Mounted /dev/nvme0n1 on /mnt/cosmos_test
[INFO] GET failed @ key `{` (No such key)
-----------------------------------------------
File-System-Based KVS Benchmark
-----------------------------------------------
dev=/dev/nvme0n1 mountpoint=/mnt/cosmos_test
ops=100000 keyspace=524288
result: OK=91121 FAIL=0 NO-SUCH-KEY=1
elapsed: 9499.36 ms  (21054.1 IOPS est. for PUT+GET )
-----------------------------------------------
[INFO] Unmounted /mnt/cosmos_test

KV_FSYNC_PERIOD 10
[INFO] Mounted /dev/nvme0n1 on /mnt/cosmos_test
[INFO] GET failed @ key `{` (No such key)
-----------------------------------------------
File-System-Based KVS Benchmark
-----------------------------------------------
dev=/dev/nvme0n1 mountpoint=/mnt/cosmos_test
ops=100000 keyspace=524288
result: OK=91121 FAIL=0 NO-SUCH-KEY=1
elapsed: 33527.5 ms  (5965.26 IOPS est. for PUT+GET )
-----------------------------------------------
[INFO] Unmounted /mnt/cosmos_test

KV_FSYNC_PERIOD 1
[INFO] Mounted /dev/nvme0n1 on /mnt/cosmos_test
[INFO] GET failed @ key `{` (No such key)
-----------------------------------------------
File-System-Based KVS Benchmark
-----------------------------------------------
dev=/dev/nvme0n1 mountpoint=/mnt/cosmos_test
ops=100000 keyspace=524288
result: OK=91121 FAIL=0 NO-SUCH-KEY=1
elapsed: 238693 ms  (837.897 IOPS est. for PUT+GET )
-----------------------------------------------
[INFO] Unmounted /mnt/cosmos_test
```

캡처 첨부 위치: 각 `KV_FSYNC_PERIOD` 실행 터미널 캡처를 보고서 PDF에 함께 포함한다.

### 3.2 Project #3 `kv_bench`를 통한 Key-Value SSD 성능 평가

Project #3의 KV-SSD firmware를 기준으로 동일한 parameter `(100000, 524288)`에 대해 `kv_bench`를 실행하였다.

| Benchmark | OK | FAIL | NO-SUCH-KEY | Elapsed (ms) | IOPS |
| --- | ---: | ---: | ---: | ---: | ---: |
| KV-SSD `kv_bench` | 91121 | 0 | 1 | 46860.8 | 4267.96 |

실행 결과는 다음과 같다.

```bash
[INFO] GET failed @ key `{` (No such key)
-----------------------------------------------
Cosmos+ OpenSSD-Based KV-SSD Benchmark
-----------------------------------------------
ops=100000 keyspace=524288
unique_keys=91121
result: OK=91121 FAIL=0 NO-SUCH-KEY=1
elapsed: 46860.8 ms  (4267.96 IOPS est. for PUT+GET )
-----------------------------------------------
```

캡처 첨부 위치: `kv_bench` 실행 터미널 캡처를 보고서 PDF에 함께 포함한다.

### 3.3 파일 기반 Key-Value Store와 Key-Value SSD 성능 비교 분석

파일 기반 KVS에서는 `KV_FSYNC_PERIOD`가 커질수록 성능이 크게 증가하였다. `KV_FSYNC_PERIOD=1`에서는 매 PUT마다 `fsync()`가 호출되므로, 각 update가 ext4 journal, file metadata, block layer, NVMe driver, device flush path를 자주 통과한다. 이 경우 elapsed time은 238693 ms, 성능은 837.897 IOPS로 가장 낮았다.

`KV_FSYNC_PERIOD=10`에서는 여러 PUT을 묶은 뒤 fsync를 수행하므로 동기화 비용이 분산된다. 그 결과 성능은 5965.26 IOPS로 증가하였다. `KV_FSYNC_PERIOD=100`에서는 fsync 호출 수가 더 줄어 21054.1 IOPS를 보였고, `KV_FSYNC_PERIOD=1000`에서는 44422.7 IOPS로 가장 높은 성능을 보였다.

이 결과는 파일 기반 KVS 성능에서 `fsync()` 빈도가 매우 중요한 병목임을 보여 준다. fsync 주기가 작을수록 durability는 더 즉시 보장되지만, 매번 storage stack 전체에 동기화 비용이 발생한다. 반대로 fsync 주기가 커질수록 여러 write를 batching할 수 있어 성능은 좋아지지만, crash 발생 시 마지막 fsync 이후의 update를 잃을 가능성이 커진다.

KV-SSD benchmark는 4267.96 IOPS를 보였다. 이는 파일 기반 KVS의 `KV_FSYNC_PERIOD=1`보다는 높지만, `KV_FSYNC_PERIOD=10`, `100`, `1000`보다 낮다. KV-SSD 방식은 file system과 block I/O stack 일부를 우회하고 KV command를 직접 처리한다는 장점이 있다. 그러나 본 실험의 Project #3 firmware 구현에서는 OpenSSD firmware 내부의 KV command 처리, FTL 연동, metadata 관리, command handling overhead가 존재하며, 최적화 수준이 Linux ext4 page cache 및 batching 효과보다 낮을 수 있다.

따라서 이번 결과는 두 가지 trade-off를 보여 준다. 첫째, 파일 기반 KVS는 ext4, VFS, page cache, block layer를 거치므로 I/O path가 길지만, fsync 빈도를 낮추면 host-side buffering과 batching 효과로 높은 throughput을 얻을 수 있다. 둘째, KV-SSD는 KV-native access semantics를 제공하지만, 실제 성능은 firmware 구현의 효율성과 command processing path에 크게 의존한다. 특히 strict durability 조건에 가까운 `KV_FSYNC_PERIOD=1`과 비교하면 KV-SSD가 더 빠르지만, relaxed durability 조건에서는 파일 기반 KVS가 더 높은 성능을 보였다.

## 4. 결론

Task1에서는 Cosmos+ OpenSSD가 DSM discard/TRIM 요청을 제대로 처리하지 못해 `mkfs.ext4`가 실패한다는 원인을 확인하고, Linux NVMe host driver에 Cosmos+ OpenSSD 전용 `NVME_QUIRK_NO_DISCARD`를 추가하여 문제를 해결하였다. 수정 후 표준 명령인 `sudo mkfs.ext4 -F /dev/nvme0n1`이 정상 완료되었다.

Task2에서는 ext4 기반 파일 KVS와 Project #3 KV-SSD의 성능을 비교하였다. 파일 기반 KVS는 `KV_FSYNC_PERIOD`가 1에서 1000으로 증가할수록 IOPS가 837.897에서 44422.7까지 증가하였다. 이는 fsync 동기화 비용이 파일 기반 KVS 성능에 큰 영향을 미친다는 것을 의미한다. KV-SSD는 4267.96 IOPS를 보였으며, strict fsync 조건의 파일 KVS보다 빠르지만 batching이 적용된 파일 KVS보다는 느렸다. 결과적으로 storage abstraction의 성능은 I/O path 길이뿐 아니라 동기화 정책, host buffering, firmware 구현 효율성에 의해 함께 결정됨을 확인하였다.
