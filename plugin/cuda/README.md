CUDA checkpoint plugin
======================

This is plugin of DMTCP to checkpoint CUDA programs.
This plugin uses CUDA's checkpoint/restart API introduced
in CUDA 12.4.

## Compile
```
make
```

## Test
```
make check
```

## Run
```
dmtcp_launch --with-plugin /path/to/libdmtcp_cuda.so [target]
```
