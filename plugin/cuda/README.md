CUDA checkpoint plugin
======================

This is plugin of DMTCP to checkpoint CUDA programs.
This plugin uses CUDA's checkpoint/restart API introduced
in CUDA 12.4.

## Compile
```
make
```

## Run
```
dmtcp_launch --with-plugin /path/to/crac/libdmtcp_crac.so [target]
```
