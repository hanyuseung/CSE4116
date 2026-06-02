# 코드 해석
- 내가 직접 정리한 내용

## 왜 request Transform을 바꿔야 하며 어떻게 바꿔야 하는가?
### 추상화
- **KV GET을 할 때, `nvmeReg`에 valueLength를 request에 기록해야 합니다.**
    - kv_banch.cc에서 host의 `ioctl cmd.result`로 CQ에 기록된 valueLength 를 직접 사용하고 있기 때문에, `DW0 - specific` 필드에 직접 value length를 넣어야 합니다. 
- 기존의 CQ push방식은, request가 끝날때, 기존의 CQ는 dword[3]의 autoCompletion bit를 활성화 하여 자동으로 CQ에 request를 올려버립니다.
    - 이는 `IssueNvmeDmaReq()`의 `set_auto_rx_dma()` 호출 인자에 `NVME_COMMAND_AUTO_COMPLETION_ON`이 들어있는 것으로 확인할 수 있습니다.  
    - 이렇게 올려버리면 specific 필드를 바꿀 수가 없습니다. Request를 처리하는 도중에 specific을 바꿀수가 없기 때문입니다.
- 이를 해결하기 위해
    1. `request_format.h`의 `struct _NVME_DMA_INFO`에 `valueLength(completionSpecific)` 필드를 추가해야 합니다. 
    2. `autoCompletion`을 꺼야합니다. 이것도 필드에 추가합니다.
    3. request Issue시에 `autoCompletion`에 관한 정보를 보냅니다.
    4. 이후 request처리 시에 `specific`에 해당 value length를 입력하며 CQ에 집어넣으면 됩니다. 
    - `host_ldd.c`에서 `specific, autoCompletion`필드를 확인할 수 있습니다.

### 실제 코드 분석 1
- `request_format.h`
    ``` c++
    typedef struct _NVME_DMA_INFO{
        unsigned int startIndex : 16;
        unsigned int nvmeBlockOffset : 16;
        unsigned int numOfNvmeBlock : 16;
        unsigned int reqTail	: 8;
        unsigned int autoCompletion : 1;
        unsigned int reserved0 : 7;
        unsigned int overFlowCnt;
        unsigned int completionSpecific;
    } NVME_DMA_INFO, *P_NVME_DMA_INFO;
    ```
- `host_ldd.c`
    ```c++
    //offset: 0x0000030C, size: 16
    typedef struct _HOST_DMA_CMD_FIFO_REG
    {
        union {
            unsigned int dword[5];//slot_modified
            struct 
            {
                unsigned int devAddr;
                unsigned int pcieAddrH;
                unsigned int pcieAddrL;			
                struct 
                {
                    unsigned int dmaLen				:13;
                    unsigned int autoCompletion		:1;
                    unsigned int cmd4KBOffset		:9;
                    unsigned int reserved0			:7;//slot_modified
                    unsigned int dmaDirection		:1;
                    unsigned int dmaType			:1;
                };
                unsigned int cmdSlotTag;//slot_modified
            };
        };
    } HOST_DMA_CMD_FIFO_REG;
    ```
- 위의 두 함수에 있는 `autoCompletion`은 동일한 의미를 갖고 있습니다. 해당 필드 가 1이면 하드웨어가 자동으로 처리된 request를 CQ에 올려버리겠다는 의미입니다.
- 기존 `void IssueNvmeDmaReq(unsigned int reqSlotTag)`에서는 이 필드를 1, 즉 auto로 `set_auto_tx_dma()`에 올리고 있습니다. 이 부분을 수정했습니다.
    ```c++
    // OLD
    void IssueNvmeDmaReq(unsigned int reqSlotTag){
    ...
    while(numOfNvmeBlock < reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.numOfNvmeBlock)
		{
			set_auto_tx_dma(reqPoolPtr->reqPool[reqSlotTag].nvmeCmdSlotTag, dmaIndex, devAddr, NVME_COMMAND_AUTO_COMPLETION_ON);
            ...
        }
    }
    ...
    ```
    ```c++
    // NEW
    void IssueNvmeDmaReq(unsigned int reqSlotTag){
    ...
    while(numOfNvmeBlock < reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.numOfNvmeBlock)
		{
			// now check auto CQ or not
			set_auto_rx_dma(reqPoolPtr->reqPool[reqSlotTag].nvmeCmdSlotTag, dmaIndex, devAddr, reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.autoCompletion);
            ...
        }
    }
    ...
    ```
### 실제 코드 분석 2
- `ReqTransNvmeToSliceWithCompletion()`: 
    - 기존 `ReqTransNvmeToSlice()`를 변형한 함수입니다. 로직은 동일합니다.
    - 해당 함수는 `request_transformation.c`에서만 사용하는 helper함수입니다. 따라서 static으로 선언합니다.
    - KV FTL을 위해 추가된 필드 `autoCompletion`, `completionSpecific(벨류 길이)`을 dma info에 추가합니다.
- `ReqTransNvmeToSlice()`:
    - `ReqTransNvmeToSliceWithCompletion()`의 wrapper함수입니다. 기존 `ReqTransNvmeToSlice()` 의 역할, 즉 page level FTL의 request를 처리합니다.
- `ReqTransKvGetToSlice()`
    - `ReqTransNvmeToSliceWithCompletion()`의 wrapper함수입니다. KV FTL의 request를 처리합니다.


### 실제 코드 분석 3
- `CheckDoneNvmeDmaReq()`
    - 기존
        - `Rx,TxDone == true`면, 즉 요청 처리가 끝나면 request queue에서 해당 request slot을 할당 해제해 줍니다 (`SelectiveGetFromNvmeDmaReqQ(reqSlotTag)` 사용). 
        - `autoCompletion`이 request issue시 부터 ON되어 있었기 때문에, 자동으로 CQ에 request를 올립니다. 따라서 `SelectiveGetFromNvmeDmaReqQ(reqSlotTag)`만으로 처리가 가능합니다.
    - 변경
        - `SelectiveGetFromNvmeDmaReqQ(reqSlotTag)`이전에 request의 내용을 복사해놓습니다. (날라가기 때문)
        - `completionSpecific` 필드에는 valueLength가 들어있습니다. 이를 `set_auto_nvme_cpl(cmdSlotTag, completionSpecific, 0)`을 호출하여 `nvmeReg.specific`에 저장하며 CQ에 entry를 올립니다.
        - 이제 `ioctl cmd.result`즉 CQentry의 specific 필드에 value length가 저장됩니다.

### 나머지
- 나머지는 그냥 hash 구조 만들어서 KEY - LSA구조 만드는게 전부입니다. `kv_store.c,h`확인하시면 됩니다.
- 물론 해당 KEY - LSA 매핑 테이블은 DRAM에 정적으로 할당해야 하기 때문에, `memory_map.h`에서 할당해줘야 합니다.
- proj1과 같이 `nvme_io_cmd.c, nvme.h` 에 opcode 추가하고 handler 추가한 뒤 `handle_nvme_io_cmd()` 에 case분기 해야합니다.
