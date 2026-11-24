# evo_sysmon

A full-screen **system monitor** tile ("System") — live CPU cores and clock,
per-core temperature, RAM used/free, process and thread counts, load and uptime,
and a top-processes list.

## How it works
Uses the same stack as the terminal/DOOM/launcher
([`evo_graphics_lib`](../../shared/evo_graphics_lib) + 8×8 font straight to the
framebuffer, HMI hidden via presentation mode) and iDrive **BACK** to exit. All
data is read with **native QNX syscalls** — it never spawns `pidin`/`hogs`
(those wedge in REPLY when spawned from inside this multithreaded graphics
process):

- `/dev/cputemp` — per-core temperature
- syspage `num_cpu` + `cpuinfo.speed` — core count + MHz
- syspage asinfo `ram`, `posix_typed_mem("/memory")` — total / free RAM
- `/proc/<pid>/as` `DCMD_PROC_INFO` — process+thread counts, CPU load, top list
- `CLOCK_MONOTONIC` — uptime

Data is gathered on a background thread, so the UI stays live and BACK always exits.

## Build
```sh
make -C projects/evo_sysmon       # -> bin/evo_sysmon (ARM QNX)
```

## Run
Launcher tile **System** (`order 30`, icon `cpu.rgba`).
