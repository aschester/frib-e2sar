# FRIB-E2SAR dev

This is a repository for various E2SAR-related development codes for FRIB data processing. The subdirectories contain examples of simple initialization and segmenter-reassembly workflows as well as configuration files and run scripts. The scripts are used to run FRIBDAQ services to build and record event data on the reassembly side of the E2SAR pipeline.

This document is not intended as a comprehensive user's manual for this project, rather it is a loose and evolving set of developer notes so I (ASC) don't forget how to do this stuff.

## Requirements

- E2SAR software and prereqs (https://github.com/JeffersonLab/E2SAR/wiki/Code-and-Binaries)
- NSCLDAQ 12.1 or later
- Unified Format Library 2.2-007 or later
- CMake 3.18
- Compiler support for C++17 standard

A Docker image based on Debian 11 (Bullseye) with preinstalled E2SAR binaries and prereqs is available here: https://hub.docker.com/r/aschester/e2sar-bullseye. The Docker image can be used to build images with Apptainer, Shifter, etc. The project will incorporate its own <a href="https://github.com/FRIBDAQ/UnifiedFormat">Unified Format Library</a> as a git submodule.

## Building the codes

- Clone the repository from https://github.com/aschester/frib-e2sar.git.
- Ensure the Unified Format Library submodule is initialized properly by running the commands `git submodule init` and `git submodule update`. One can ensure the submodules are initialized properly by cloning the project via `git clone --recurse-submodules [URL of git repository]` as well.
- Build the project using CMake:

```
mkdir build && cd build
cmake .. -DNSCLDAQ_ROOT=/path/to/nscldaq/dir -DCMAKE_INSTALL_PREFIX=/path/to/install/dir
cmake --build .
cmake --install .
```

You can override the default unified format path by setting an alternative during the first stage of the build with `-DUFMT_ROOT=/path/to/ufmt`. Parallel builds are supported by the `-j` flag, where `-jN` will use `N` cores to build. Note that due to some internal dependencies the build _may_ fail if `N` is "large."

The install directory contians six (6) folders:
- bin/     : contains project binaries
- include/ : contains project headers
- lib/     : contains project libraries, including those for the Unified Format subproject
- ini/     : example initialization files for the Segmenter and Reassembler
- scripts/ : scripts to setup and run the FRIB event-building pipeline (DEPRECATED 5/6/25)
- share/   : project documentation and examples for frib-e2sar (share/html/) and Unified Format (share/htmldocs/ and share/examples/)

## Running the examples

Running programs with the `-h` option will show all command-line options and their defaults. Sensible defaults for parameters are set in most cases. All programs which use the E2SAR streaming libraries expect an EJFAT URI either passed on the command line or stored in an environment variable. Anything passed on the command line when running the program will override preset values.

The EJFAT URI has the form:

`ejfat[s]://[<token>@]<cp_host>:<cp_port>/[lb/<lb_id>][?[data=<data_host:[<data_port>]>][&sync=<sync_host>:<sync_port>][&sessionid=<session_id>]].`

For testing I usually export the URI or define it in an environment file which sets the environment in the container at runtime. An example URI is shown below:

`export EJFAT_URI="ejfat://mytoken@127.0.0.1:23456/lb/123?data=127.0.0.1:23457&sync=127.0.0.1:23458"`.

The quotes on the string may be needed to prevent your shell from interpreting `&` as a shell command. The above URI is used for one-to-one Segmenter-to-Reassembler streaming without an EJFAT load balancer. Note that in this case, the `cp_host`, `data_host` and `sync_host` are all `127.0.0.1` i.e., `localhost`. For one-to-one reassembly, the receiver should listen on the data port, which in this example is 23457.

### Running the FRIB-E2SAR event building pipeline (DEPRECATED 5/6/25)

The E2SAR event-building pipeline is controlled via two scripts and an event-builder configuration file:
- `reassemble.py` : runs the Reassembler and, if sending raw NSCLDAQ 12 DDAS data, ddasSort. The Reassembler outputs reassembled raw data into a raw ringbuffer where it is made available to the rest of the processing pipeline. If building events on the reassembly side, ddasSort reads from this ringbuffer and puts its output into a sorted ringbuffer which is input to glom. If not building events, this data can be directly input into an event-building pipeline which is not glomming data.
- `run_evb.py` : run the combined event-building pipeline with an eventlogger to build sorted data into events and write event data to a file.

The `reassemble.py` script should be run first, as it will create the sort ring needed by the event-building stage. `run_evb.py` will call your configuration script to configure the event-building pipeline; the configuration script initializes the EVB pipeline and starts the data sources feeding the event builder. The data sources in this file must be input by hand, similar to a `ReadoutCallouts.tcl`; the destination ring and event-building window can be set on the command line. By default the program assumes the setup script is called `setup_evb.sh` and exists in the directory where you launch `run_evb.py` unless another path is provided at runtime. An example `setup_evb.sh` is installed in the scripts/ directory. Both the Reassembler/sorter and the EVB/logger applications will tell you when they are ready to receive data. A ready state looks like:

#### reas_and_sort.py
```
<daq-ejfat-01:e2sar-analysis >sw/scripts/reas_and_sort.py 
Using DAQBIN: /usr/opt/daq/12.1-pre6.e2sar/bin
Running reas command: /user/0400x/e2sar-analysis/sw/bin/evtbuild/recv -S tcp://localhost/reas0_raw -t 4
STARTED OK /user/0400x/e2sar-analysis/sw/bin/evtbuild/recv -S tcp://localhost/reas0_raw -t 4
Running sort command: /usr/opt/daq/12.1-pre6.e2sar/bin/ddasSort -s tcp://localhost/reas0_raw -S reas0_sort -W 10.0
STARTED OK /usr/opt/daq/12.1-pre6.e2sar/bin/ddasSort -s tcp://localhost/reas0_raw -S reas0_sort -W 10.0
```

#### run_evb.py
```
<daq-ejfat-01:e2sar-analysis >sw/scripts/run_evb.py 
Using DAQBIN: /usr/opt/daq/12.1-pre6.e2sar/bin
Running evtbuild command: /user/0400x/e2sar-analysis/sw/scripts/setup_evb.sh
Running eventlog command: /usr/opt/daq/12.1-pre6.e2sar/bin/eventlog -s tcp://localhost/frib_e2sar_evb --number-of-sources=1 --oneshot
Recording run...
```

In the latter case an event builder GUI will be running showing the registered data source(s). Configuration of ring sources and initialization of the evb is performed in the `setup_evb.sh` script. To send data through the pipeline, run the `send` program installed at `bin/evtbuild/send` under your top-level installation directory. At minimum the Reassembly/sorting code requires an NSLCDAQ 12 data soruce URI (file:// or tcp://) and a configuration file describing how to run the Reassembler.

Once the proper number of end runs is seen, the event-building pipeline and eventlogger will restart and wait to receive more data. This process takes approximately 5 seconds.

## Notes

- Ensure that useCP is set to the same value in both the segmenter and reassembler configuration files.
- Pipeline configuration is entirely hardcoded, so be cautious when changing e.g., ringbuffer names.
- In "most" cases the pipeline can safely shut itself down when it encounters and error. Ctrl-C (SIGINT) will be propagated to all child processes and is generally the safest way to exit the python scripts. In some cases hanging processes must be killed on the command line. Most likely this is going to be a stray ringFragmentSource.
- As of 5/6/25 the contents of the `scripts` folder are largely outdated, though it does provide a framework for recussitating the event-building features of the workflow if needed.