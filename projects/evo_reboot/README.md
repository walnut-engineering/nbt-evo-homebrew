# evo_reboot

A tiny **reboot** helper — the unit has no `shutdown`/`reboot` in `PATH`, so this
calls QNX `sysmgr_reboot()`. Not a launcher tile; a CLI helper for deploy/test
cycles.

## Build
```sh
make -C projects/evo_reboot       # -> bin/evo_reboot (ARM QNX; no graphics lib)
```

## Run
```sh
/fs/sda1/evo/evo_reboot           # run as root
```
