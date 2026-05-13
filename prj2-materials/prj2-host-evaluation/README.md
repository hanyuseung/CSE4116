# Project#2 - Task 2. Cosmos+ OpenSSD FTL Benchmark
*Sogang University – Embedded Systems Software (CSE4116, Prof. Youngjae Kim)*

---

This benchmark verifies whether the **block-level FTL implementation** in Cosmos+ OpenSSD works well by performing thousands of direct NVMe read/write operations through the `ioctl` passthrough interface.

---

## Build

```bash
make
````

This compiles `ftl_bench` using `nvme_passthru.cc` and `nvme_passthru.h`.

---

## Run the Benchmark

### Arguments

| Position | Meaning                             | Example           |
| -------- | ----------------------------------- | ----------------- |
| 1        | NVMe device node                    | `/dev/nvme1n1`    |
| 2        | I/O mode                            | `seq` or `rand`   |
| 3        | Number of I/O operations (16KB each)| `10000`           |
| 4        | LBA span for random access          | `1048576` (≈4 GB)|
| 5        | NVMe device NSID                    | `1`               |

Example:

```bash
sudo ./ftl_bench /dev/nvme1n1 seq 10000 1048576 1
sudo ./ftl_bench /dev/nvme1n1 rand 1000 1048576 1
```

---

## Output Example

```
-----------------------------------------------
 Cosmos+ OpenSSD FTL Benchmark (seq)
-----------------------------------------------
 mode=seq ops=10000 span=1048576 (16KB units)
 result: OK=10000 FAIL=0
 elapsed: 11571.6 ms  (1728.37 IOPS est. for R/W)
-----------------------------------------------
```

If the FTL mapping or I/O path is faulty, a string of "[ERROR] Write/Read failed @ LBA" will appear.

---

## What It Does

1. **16KB(4-LBA slice-aligned) writes** to thousands of LBAs in either 'seq' or 'rand' order.
2. **Cache thrashing** by writing additional data outside the test range to evict controller-side data buffer.
3. **Reads all written LBAs** and checks whether patterns match exactly.

---

## Precautions

* **Do NOT run on mounted or active filesystems.**
* The program directly overwrites LBAs on the target NVMe device.
* It uses only user-level I/O via `ioctl`.
* Stop the benchmark anytime with `Ctrl+C` (progress will stop gracefully).
* Reload firmware before each benchmark run to ensure clean FTL state.

---

## Clean Up

```bash
make clean
```

Removes the compiled binary and object files.

---

