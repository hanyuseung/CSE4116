## Host => Device DMA request

- slice = 16KB

- NVME command => ReqTransNvmeToSlice() => Logical slice address => 

- host request  를 logical request로 쪼개는데 그거 단위가 **slice** 임

- slice 범위를 넘으면 first/middle/last request로 분할
    - first: 일단 보내
    - second: 루프 계산해서 중간 꽉찬 slice보내
    - last: 애매하게 남은 꽁다리 보내 (block이 한 2개 남았다 이러면 2/4니까...)


## Device: DRAM => NAND DMA request
- data buffer 는 그냥 DRAM임

- `ReqTransSlcieToLowLevel()`는 SQ에서 request를 받아와서, NAND DMA용으로 바꿔주는 함수임.
- requset가 Dram Hit 시에는 바로 실행
- requset가 Dram Miss 시에는 databuffer 할당해주고 거기에 requset관련 쓰기
- Write 명령이면 RxDMA로 reqCode를 설정해줌 + databuffer dirty true설정
    - Rx : 호스트에서 받아서 쓰기
- Read 명령이면 TxDMA로 reqCode를 설정해줌 
    - Tx: 읽어서 호스트로 보내기

### `EvictDataBufferEntry`
- Dram에서 evict하고 write back to NAND.
- VSA = virtual Slice Address: NAND내에서의 추상화 주소. 완전한 물리주소는 아님
    - Host는 LSA 즉 logical주소. (host dram - device Dram)
    - Device는 VSA 즉 virtual 주소. (Dram - Nand)

## `address_translation.c`
- VSA 변환해줌...
## data buffer `databuffer.c`




## 프로잭트 내용

1. ReqTransNvmeToSlice 함수와 ReqTransSliceToLowLevel 함수의 기능
    - Slice = 16KB. Request는 slice단위로 저장된다. 따라서 Host Request를 slice단위로 바꿔주는게 `ReqTransNvmeToSlice`
    - requset slice는 총 3가지 과정을 거쳐 requset queue에 저장된다.
        - request가 slice단위로 몇개로 쪼개질지 loop변수에 넣는다.
        - 먼저 requset pool에 맞춰 첫 slice를 넣는다. 
        - loop가 0이 아니면 slice를 꽉채울 수 있는 request가 있다는 뜻. 꽉채워서 loop만큼 slice를 꽉채운 requset를 생성해서 slot에 할당해준다.
        - 마지막으로 host req block개수가 4의 배수가 아니여서 slice를 꽉채우지 못하고 남은 블록을 처리한다. 해당 numOfNvmeBlock과 nvmeBlockOffset을 기록하여 slice안에서 실제 전송할 블록 범위를 표현해준다.
        - 결론적으로는 requst from Host Dram => Device Dram 에 맞춰 slice단위 request 로 바꿔서 Device Dram의 request pool에 저장
    - `ReqTrnasSliceToLowLevel`함수는 `ReqTransNvmeToSlice`에서 할당해준 request pool에서 request를 꺼내와서 
        - request가 Dram hit 시에는 바로 수행
        - request가 Dram miss 시에는 data buffer할당해주고 거기에 해당 내용 작성
        - Write 명령이면 reqCode = RxDMA & databuffer dirty - true.
            - Rx: 호스트에서 쓰기명령을 **받음**
        - Read 명령이면 reqCode = TxDMA 
            - Tx: 읽어서 호스트로 **보냄**
    - 최종 정리
        - `ReqTransNvmeToSlice()`: Host NVMe 요청을 FTL mapping 단위인 slice 요청들로 분할해 request pool과 slice queue에 등록하는 함수
        - `ReqTransSliceToLowLevel()`: slice 요청에 data buffer를 매핑한 뒤 필요하면 NAND read/write 요청을 추가로 만들고, 최종적으로 host-device DRAM 간 전송을 위한 NVMe DMA 요청인 RxDMA/TxDMA로 변환해 low-level queue에 넣는 함수


2. AllocateDataBuf함수와 EvictDataBufEntry함수의 기능
    - `EvictDataBufEntry` 함수는 재사용하려는 data buffer entry가 dirty 상태이면 NAND write request를 생성해서 low-level queue에 넣는 함수이다.
        - dirty가 아니면 NAND write-back 없이 종료한다.
        - dirty이면 `AddrTransWrite()`를 통해 해당 LSA에 대한 새 VSA를 할당받고, `REQ_TYPE_NAND`, `REQ_CODE_WRITE` request를 만든다.
        - 이 request는 data buffer entry의 내용을 NAND의 `virtualSliceAddr` 위치에 쓰기 위한 요청이다.
        - write request를 queue에 넣은 뒤 해당 data buffer entry의 dirty bit를 clean으로 바꾼다.
        - 따라서 host write가 들어올 때마다 NAND에 바로 쓰는 것이 아니라, data buffer에 dirty로 유지하다가 buffer entry가 재사용/evict될 때 lazy하게 NAND write-back이 발생한다.

    - `AllocateDataBuf` 함수는 data buffer에서 새 요청에 사용할 entry를 하나 할당하는 함수이다.
        - data buffer LRU list의 tail entry를 victim으로 선택한다. 즉 가장 오래 사용되지 않은 entry를 고른다.
        - 선택한 entry를 LRU list의 tail에서 제거하고 head로 옮긴다. 새 요청에 사용할 entry가 되었기 때문에 가장 최근 사용된 entry로 취급한다.
        - 해당 entry를 기존 data buffer hash list에서 제거한다. 이후 caller가 새 `logicalSliceAddr`로 metadata를 갱신하고 다시 hash list에 넣는다.
        - 함수 자체는 dirty write-back을 수행하지 않는다. dirty entry 처리와 NAND write request 생성은 이후 호출되는 `EvictDataBufEntry()`가 담당한다.
        - 최종적으로 재사용할 data buffer entry 번호를 반환한다.


3. `AddrTransWrite` 함수와 `AddrTransRead` 함수의 기능
    - `AddrTransRead` 함수는 LSA(Logical Slice Address)를 VSA(Virtual Slice Address)로 변환하는 read용 주소 변환 함수이다.
        - `logicalSliceMapPtr->logicalSlice[logicalSliceAddr].virtualSliceAddr`를 조회한다.
        - 해당 LSA에 매핑된 VSA가 있으면 그 VSA를 반환한다.
        - 아직 쓰인 적이 없거나 유효한 mapping이 없어서 `VSA_NONE`이면 `VSA_FAIL`을 반환한다.
        - 즉 read 요청에서 “이 logical slice가 NAND의 어느 virtual slice에 저장되어 있는가”를 찾는 역할을 한다.

    - `AddrTransWrite` 함수는 write 요청을 위해 LSA에 새 VSA를 할당하는 함수이다.
        - 먼저 `InvalidateOldVsa(logicalSliceAddr)`를 호출해서 기존에 같은 LSA에 매핑되어 있던 VSA를 invalid 처리한다.
        - 그 다음 `FindFreeVirtualSlice()`로 새로운 free VSA를 하나 할당받는다.
        - `logicalSliceMap`에는 `LSA -> 새 VSA` mapping을 기록한다.
        - `virtualSliceMap`에는 `새 VSA -> LSA` mapping을 기록한다.
        - 최종적으로 새로 할당된 VSA를 반환한다.
        - 즉 overwrite가 발생하면 기존 NAND 위치에 덮어쓰는 것이 아니라, 기존 VSA를 invalid 처리하고 새로운 VSA에 쓰는 out-of-place update 방식이다.




