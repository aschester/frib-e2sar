# FRIB-E2SAR dev

This is a repository for various E2SAR-related development at FRIB. The subdirectories contain examples demonstrating how to perform simple initialization and segmenter-reassembly tasks up to and including an E2SAR workflow for FRIBDAQ data (`sendrecv`).

## Requirements

- E2SAR software and prereqs (https://github.com/JeffersonLab/E2SAR/wiki/Code-and-Binaries)
- NSCLDAQ 12.1 or later
- FRIB unified format library
- CMake 3.18

A Docker container based on Debian 11 (Bullseye) with preinstalled E2SAR binaries and prereqs is available here: https://hub.docker.com/r/aschester/e2sar-bullseye.

## Building the examples

- Clone the repository: `https://github.com/aschester/frib-e2sar.git`
- Assuming the environment is configured correctly, build using `CMake`:

```
mkdir build && cd build
cmake .. -DNSCLDAQ_ROOT=/path/to/nscldaq/dir
cmake --build .
cmake --install . --prefix "/path/to/installation/dir"
```

- You can override the default unified format path by setting an alternative during the first stage of the build with `-DUFMT_ROOT=/path/to/ufmt`.
- To build in parallel, use the `-j` flag: `cmake --build . -j N` where `N` is the number of cores you'd like to use.

## Running the examples

- Installation `/bin` directory contains executables, `/include` directory all project headers.
- Running programs with the `-h` option will describe how to use them.
- Sensible defaults are set in most cases.
- For codes using initialization files it is easy to switch between settings by pointing the code at a different config file at runtime.
- Most codes will expect an EJFAT URI either passed as a command line or stored in an environment variable. Generally anything passed on the command line will override any preset settings. For testing I usually take the second approach:

`export EJFAT_URI="ejfat://mytoken@127.0.0.1:23456/lb/123?data=127.0.0.1:23457&sync=127.0.0.1:23458"`.

The quotes on the string may be needed to prevent your shell from interpreting `&` as a shell command. For point-to-point reassembly, the receiver should listen on the data port, which is in this case 23457.

## Notes

- Ensure that useCP is set to the same value in both the segmenter and reassembler configuration files (todo: alternate config method which sets CP use for the  whole pipeline)