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

- 