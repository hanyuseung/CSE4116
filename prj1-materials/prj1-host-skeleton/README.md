# Project#1 - Task 2 & Task 3. NVMe Passthru-Based Host Interface Tests
*Sogang University – Embedded Systems Software (CSE4116, Prof. Youngjae Kim)*

---

## File Descriptions
- **Makefile**  
  *Do not modify.*
- **test.cc**  
  Test program.  
  *Do not modify unless absolutely necessary (except for `DEV_PATH`); unjustifiable core logic changes will result in zero credit.*
  - Task 2: Image write/read test (default).  
  - Task 3: Custom command test (enabled when `#define TASK3_ON` is active).  
- **nvme_passthru.h / nvme_passthru.cc**  
  Implementation of passthru interface operations.  
  You are free to modify or extend these files as needed.

---

## Task 2 – Image Write/Read
Write an image file to the device, read it back, and verify that both files are identical.

### Build
```bash
make
```

### Usage
```bash
./test input.png output.png
```

- `input.png` will be written to the device.  
- The program immediately reads back from the device into `output.png`.  
- If the contents of `input.png` and `output.png` are identical, the test passes.

### Constraints
- Image size must be **less than 16 MB**.  
- The program compares the two files byte by byte.

---

## Task 3 – Custom NVMe Command: Self-Introduction
Define a new NVMe command that makes the device print out a short self-introduction via `xil_printf` (to be checked through `tio` (/setup.sh GreedyFTL show)).  

### Example Format
```
Name: Gildong Hong
Student ID: 20241234
Affiliation: Sogang University Computer Science and Engineering 
Interests: Embedded Systems and Operating Systems
Hobbies: Soccer, Movies
```

### How to Run
1. Define `#TASK3_ON` in `test.cc` to enable Task 3 mode.  
2. Rebuild the project:
   ```bash
   make clean && make
   ```
3. Run the test program (no image files needed for Task 3):  
   ```bash
   ./test
   ```
4. Open `tio` on the UART port connected to Cosmos+ OpenSSD.  
5. Check the output printed by the firmware (`xil_printf`).

---

## Notes
- **NSID** is defined in `nvme_passthru.cc`.  
- **DEV_PATH** is defined in `test.cc`.  
- Both values may differ depending on your machine.  
  Use the following command to check:
  ```bash
  lsblk
  ```
  Update the source accordingly before running the program.

