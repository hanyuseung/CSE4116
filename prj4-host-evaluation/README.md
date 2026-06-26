# Project#4 - Task 2. File-System-Based Key-Value Store Benchmark
*Sogang University – Embedded Systems Software (CSE4116, Prof. Youngjae Kim)*

---

This benchmark evaluates a **file-system-based Key-Value Store (KVS)** implemented on top of a conventional **ext4 file system** mounted over the Cosmos+ OpenSSD platform, and serves as a software baseline for comparing against the **previous KV-SSD implementation**.

Unlike direct NVMe key-value commands, this benchmark realizes KV semantics using _regular files_, explicitly enforcing persistence guarantees via `fsync()`, and measures the resulting performance and correctness under randomized workloads.

---

## Build

```bash
make
````

This compiles `kv_fs_bench` using the file-based KVS backend implementation (`kv_fs_backend.h|c`).

---

## Run the Benchmark

### One-Time Setup

Before running the benchmark for the first time, you must create an ext4 file system on the Cosmos+ OpenSSD platform (e.g., use /dev/nvme1n1 as the benchmark device):

```bash
sudo umount /dev/nvme1n1 2>/dev/null || true
sudo mkfs.ext4 -F /dev/nvme0n1
````

* **Make sure /dev/nvme1n1 is NOT your OS/root disk**.
* Use lsblk to double-check before formatting.

> **Important**  
> On the current lab PCs, running `mkfs.ext4` on the Cosmos+ OpenSSD device is **expected to fail initially**. This is not a mistake in the setup or command usage.
>
> **Understand the cause of this failure and make the necessary changes (Task 1)**, so that a standard ext4 file system can be created successfully on the OpenSSD.

### Arguments

| Position | Meaning                                        | Example        |
| -------- | ---------------------------------------------- | -------------- |
| 1        | NVMe device node                               | `/dev/nvme0n1` |
| 2        | Number of PUT operations                       | `10000`        |
| 3        | Keyspace size (range of random key generation) | `4096`         |

Example:

```bash
sudo ./kv_fs_bench /dev/nvme1n1 10000 4096
```

The program automatically mounts the device under `/mnt/cosmos_test` before running and unmounts it after completion.

---

## Output Example

```
[INFO] Mounted /dev/nvme1n1 on /mnt/cosmos_test
[INFO] GET failed @ key `{` (No such key)
-----------------------------------------------
 File-System-Based KVS Benchmark
-----------------------------------------------
 dev=/dev/nvme1n1 mountpoint=/mnt/cosmos_test
 ops=10000 keyspace=4096
 result: OK=3746 FAIL=0 NO-SUCH-KEY=1
 elapsed: 21177.4 ms  (944.401 IOPS est. for PUT+GET )
-----------------------------------------------
[INFO] Unmounted /mnt/cosmos_test
```

- OK: Count of keys whose GET value exactly matched the last PUT value.
- FAIL: Mismatched values, missing writes, corrupted returns, or unexpected device behavior.
- NO-SUCH-KEY: Correct handling of a known non-existent key (expected behavior for KVS).

---

## What It Does

1. **Random KV-PUT operations**
  - Generates random 4-byte keys within a user-defined keyspace.
  - Some keys intentionally overlap to test “latest-write-wins” semantics.
  - Stores the most recent value for each key on the host side.
2. **Single GET on a non-existing key**
  - Confirms the KVS returns a “No such key” status correctly.
3. **KV-GET verification**
  - Reads each previously written key.
  - Compares the returned value with the host-side “latest value” map.
  - Reports any mismatch as a KVS correctness failure.

---

## Persistence and Durability

- Both the value log and index log are backed by regular files on ext4.
- Durability is enforced using fsync():
  - By default, every PUT operation is made persistent.
  - **The fsync frequency can be adjusted by modifying a macro variable `KV_FSYNC_PERIOD` defined in `kv_fs_backend.h`**.

---

## Precautions

* **Do NOT run on mounted or active system disks.**
* The benchmark formats and mounts a raw NVMe block device.
* All data on the target device may be overwritten.
* Root privileges are required for mounting and unmounting.
* The benchmark is intended only for experimental and educational use.

---

## Clean Up

```bash
make clean
```

Removes the compiled binary and object files.

---

