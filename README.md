# FRIB-E2SAR dev

This is a repository for various E2SAR-related development codes for FRIB data processing. The subdirectories contain example codes for inspecting E2SAR configuration info and running simple workflows on FRIB data. The Python and bash scripts in the scripts directory are used to run FRIBDAQ services to build and record event data on the reassembly side of the E2SAR pipeline.

This document is not intended as a comprehensive user's manual for this project, rather it is a loose and evolving set of developer notes so I (ASC) don't forget how to do this stuff.

## Requirements

- E2SAR software and prereqs (https://github.com/JeffersonLab/E2SAR/wiki/Code-and-Binaries)
- NSCLDAQ 12.1 or later
- CMake 3.18 or later
- Compiler support for C++17 standard

A Docker image based on Debian 11 (Bullseye) with preinstalled E2SAR binaries and prereqs is available here: https://hub.docker.com/r/aschester/e2sar-bullseye. The Docker image can be used to build images with Apptainer, Shifter, etc. The project will incorporate its own <a href="https://github.com/FRIBDAQ/UnifiedFormat">Unified Format Library</a> as a git submodule.

Since the E2SAR binaries are built against a newer Pyhton version and boost libraries than are available through the standard Debian repositories, it is advisable to build a version of NSCLDAQ 12.1 under the above container with the same Python and boost used by E2SAR. See [Appendix A](#appendix-a-nscldaq-build-configuration) for an example of how to do this.

## Building the example codes

- Clone the repository from https://github.com/aschester/frib-e2sar.git.
- Ensure the Unified Format Library submodule is initialized properly by running the commands `git submodule init` and `git submodule update`. One can ensure the submodules are initialized properly by cloning the project via `git clone --recurse-submodules [URL of git repository]` as well.
- Build and install the project using the install script:

```
./install [-p <install_prefix>] [-c <build_cores>]
```

If no installation prefix is provided, the install path defaults to ~/frib-e2sar. By default the build will use one (1) core. Note that due to some internal dependencies the build _may_ fail if the number of build cores is "large" (> 4 or so).

The install directory contians six (6) folders:
- bin/ : contains project binaries
- include/ : contains project headers
- ini/ : example initialization files for the Segmenter and Reassembler
- lib/ : contains project libraries, including those for the Unified Format subproject
- scripts/ : scripts to setup and run the FRIB event-building (EVB) pipeline
- share/ : project documentation and examples for frib-e2sar (share/html/) and Unified Format (share/htmldocs/ and share/examples/)

## Running the examples

Running programs and scripts with the `-h` flag will show command-line options and their defaults. Sensible defaults for parameters are set in most cases. All programs which use the E2SAR streaming libraries expect an EJFAT URI either passed on the command line or stored in the environment variable `EJFAT_URI`. I have opted, in general, for the latter. Arguments read from the command line when will override preset values.

The EJFAT URI has the form:

`ejfat[s]://[<token>@]<cp_host>:<cp_port>/[lb/<lb_id>][?[data=<data_host:[<data_port>]>][&sync=<sync_host>:<sync_port>][&sessionid=<session_id>]].`

An example URI is shown below:

`export EJFAT_URI="ejfat://mytoken@127.0.0.1:23456/lb/123?data=127.0.0.1:23457&sync=127.0.0.1:23458"`.

The quotes on the string may be needed to prevent the shell from interpreting `&` as a command. The above URI is used for one-to-one Segmenter-to-Reassembler streaming without an EJFAT load balancer. Note that in this case, the `cp_host`, `data_host` and `sync_host` are all `127.0.0.1` i.e., `localhost`. For one-to-one reassembly, the receiver should listen on the data port, which in this example is 23457.

### Running the FRIB-E2SAR event building pipeline

The E2SAR EVB pipeline is controlled via two Python scripts and an EVB configuration bash script:
- `reassemble.py` : runs the Reassembler and, if sending raw NSCLDAQ 12 DDAS data, ddasSort. The Reassembler outputs reassembled raw data into a raw ringbuffer where it is made available to the rest of the processing pipeline for sorting/fragment ordering, (optional) building events, and logging to a final data sink.
- `run_recv_pipe.py` : runs the full pipeline to (optionally) build events and output them to a data sink.
- `setup_evb.sh` : bash script to initialize the EVB and configure its clients. This script is called by `run_recv_pipe.sh` and is generally not intended to be run standalone. Users may need to edit this script to match their particular source configuration.

The `reassemble.py` script should be run first to create the ringbuffers needed by the EVB pipeline. Running `run_recv_pipe.py` will call the specified startup script to configure the EVB pipeline and run the main event loop to process data. The data sources in the startup script are set by the user, similar to a `ReadoutCallouts.tcl` file; other EVB parameters can be set on via the command line. Both the Reassembler/sorter and the EVB/logger applications will tell you when they are ready to receive data. A ready state for an infinite-duration reassembly process assuming NSCLDAQ 12 data from a DDAS system and the EJFAT load balancer looks like:

#### reas_and_sort.py
```
<daq-ejfat-01:e2sar >~/e2sar-analysis/reassemble.py \
-i ~/e2sar-analysis/reassembler_config.ini \
--ddas-raw

Using DAQBIN: /usr/opt/daq/12.1-010.e2sar/bin
Running reas command: /user/0400x/frib-e2sar/bin/e2sarwf --recv -i /user/0400x/e2sar-analysis/reassembler_config.ini -t 1 --deq 1 -d 0
STARTED OK /user/0400x/frib-e2sar/bin/e2sarwf --recv -i /user/0400x/e2sar-analysis/reassembler_config.ini -t 1 --deq 1 -d 0
Running sort command: /usr/opt/daq/12.1-010.e2sar/bin/ddasSort -s tcp://localhost/reas_t00 -S reas_t00_sort -W 10
STARTED OK /usr/opt/daq/12.1-010.e2sar/bin/ddasSort -s tcp://localhost/reas_t00 -S reas_t00_sort -W 10
Control plane will be ON
Expecting LB header NO
*** Make sure the LB has been reserved and the URI reflects the reserved instance information.
----- Receiver configuration -----
Reassembler flags:
        useCP           1
        useHostAddress  0
        period_ms       100
        validateCert    1
        Ki, Kp, Kd      0, 0, 2
        setPoint        0
        epoch_ms        1000
        portRange       -1
        withLBHeader    0
        eventTimeout_ms 500
        rcvSocketBufSize        3145728 (bytes)
        weight          1
        min_factor      0.5
        max_factor      2
Receiving on ports 20000:20000
Stats:
        Events Received: 0
        Events Lost in reassembly: 0
        Events Lost in enqueue: 0
        Data Errors: 0
        gRPC Errors: 0
        Events lost so far: 0
... more stats ...
```

#### run_recv_pipe.py
```
<daq-ejfat-01:e2sar >~/e2sar-analysis/run_recv_pipe.py --startup ~/e2sar-analysis/setup_evb.sh --ddas-raw
Using DAQBIN: /usr/opt/daq/12.1-010.e2sar/bin
Running evtbuild command: /user/0400x/e2sar-analysis/setup_evb.sh -sink frib_e2sar_evb -build 1 -glomdt 1000 -window 20 -ddasraw 1
Running eventlog command: /usr/opt/daq/12.1-010.e2sar/bin/eventlog -s tcp://localhost/frib_e2sar_evb -n 1 -S 1000g --oneshot
ringbuffer sink frib_e2sar_evb already exists on localhost
Recording run...
```

Running the `run_recv_pipe.py` script will also pop up an EVB GUI showing the registered data source(s) and statistics. Configuration of ring sources and initialization of the EVB pipe is performed in the `setup_evb.sh` script as mentioned previously. To send data through the E2SAR processing pipeline, run the `e2sarwf` program with the `--send` option:

```
<daq-ejfat-01:e2sar >~/frib-e2sar/bin/e2sarwf --send \
-i ~/e2sar-analysis/segmenter_config.ini \
-s file:///scratch/e2sar/data/22May2025-102523-run-0075-00.evt

Control plane                ON
Event rate reporting in Sync ON
Using usecs as event numbers ON
Number of send sockets:      4
*** Make sure the LB has been reserved and the URI reflects the reserved instance information.
Adding senders to LB:
35.11.82.130 
Getting LB status:
        Contacting: ejfats://ejfat-lb.es.net:18347/lb/16?sync=192.188.29.6:19022&data=192.188.29.21 using address: ejfat-lb.es.net:18347
        LB ID: 16
----- Sender configuration -----
Segmenter flags:
        dpV6            0
        connectedSocket 1
        useCP           1
        zeroRate        0
        usecAsEventNum  1
        syncPeriodMs    1000
        syncPeriods     2
        mtu             9000 (bytes)
        numSendSockets  4
        sndSockBufSize  3145728 (bytes)
Using URI:   ejfats://ejfat-lb.es.net:18347/lb/16?sync=192.188.29.6:19022&data=192.188.29.21
Sending:     all events
Data ID:     0
Source ID:   0
evtBufSize:  1048576 bytes
maxBufBytes: 943718 bytes 
sendRate:    1 Gbps
Queue size:  10000
E2SAR selected optimizations:  none
NSCLDAQ format version:        12
--------------------------------
Inter-event sleep time is 8388 microseconds
Segmenter started OK
```

Once the data has been sent, the sender will report some statistics:

```
Completed, 326998 frames sent, 0 errors
Send loop runtime: 26.0485 seconds
Stopping sender threads...
Removing senders: 35.11.82.130 
```

After the proper number of end runs is seen (generally speaking, as many as their are data sources), the EVB pipeline will restart and wait to receive more data. The restart process should take < 5 seconds.

The above examples assume raw NSCLDAQ data coming from a DDAS system. For more information about how pre-built data is processed, see [Appendix B](appendix-b-processing-pre-built-data).

## Notes

- Ensure that useCP is set to the same value in both the segmenter and reassembler configuration files.
- Pipeline configuration is entirely hardcoded, so be cautious when changing e.g., ringbuffer names.
- In "most" cases the pipeline can safely shut itself down when it encounters and error. Ctrl-C (SIGINT) will be propagated to all child processes and is generally the safest way to exit the python scripts. In some cases hanging processes must be killed on the command line. Most likely this is going to be a stray ringFragmentSource.
- For parallel event building from raw DDAS data, setting long accumulation windows for `reasseble.py` and `run_recv_pipe.py` may be necessary to ensure that the data initially buffered "long enough" to ensure no late fragments are output. The window may have to be quite large: for run 72 raw data and 4 parallel raw and sorted ringbuffers, windows of 1000s were needed.
- How do we get data out of the sorter and/or through the log stage faster? At relatively modest rates with single thread receive, rates can cap out well below the transmission speed at ~ 50 Mbps (!!!!)

## Appendix A: NSCLDAQ build configuration

Below is an example command to configure NSCLDAQ 12.1 build to use the same version of boost and Python as the E2SAR software. This example is run from the build directory under the top-level NSCLDAQ source directory after running `autoconf -if` to generate the configuration script:

```
../configure --prefix=/usr/opt/daq/12.1-010.e2sar \
--with-incorp-build-cores=4 \
--enable-docs \
--enable-usb --enable-epics-tools --with-epics-rootdir=/usr/opt/epics \
--enable-caen-digitizer-support \
--with-caen-digitizer-libroot=/usr/opt/caendigitizerlibs --enable-ddas \
--with-xiaapidir=/usr/opt/xiaapi/4.4.0 \
--with-firmwaredir=/usr/opt/ddas/firmware/2.2-001/firmware \
--with-dspdir=/usr/opt/ddas/firmware/2.2-001/dsp \
--with-rootsys=/usr/opt/root/6.26.04 \
--with-boost=/usr/local/include/boost \
--with-boost-libdir=/usr/local/lib \
CXX=/usr/opt/mpi/openmpi-4.1.4/bin/mpicxx \
PYTHON=/usr/bin/python3
```

## Appendix B: Processing pre-built data

How the pipeline is configured to process pre-built data requires some additional explanation. The event orderer/glom process wraps the data payload in a bunch of extra headers: The body size, fragment header, ring item header, and ring item body header for the built event. This process happens even if the `--nobuild` flag is specified for the `glom` program. These extra headers amount to 52 additional bytes of data per event, which we want to remove while allowing the event ordering process to sort the built data coming through the E2SAR pipeline by timestamp. An event filter is provided for this purpose. **[EVENT FILTER UNDER DEVELOPMENT]** 

### Building events from the command line

To inspect the output of adding a second EVB stage run the following:

- Terminal 1: `$DAQBIN/startOrderer frib_e2sar_evb 2> orderer.err | $DAQBIN/glom --dt 1000 -s 0xff --nobuild | $DAQBIN/stdintoring frib_e2sar_evb |& cat`
- Terminal 2: `$DAQBIN/ringFragmentSource -n localhost --evbport 30999 --info=test --ids=0 --ring=tcp://localhost/reas_t00 --expectbodyheaders`
- Terminal 3: `cat /scratch/e2sar/data/run-0075-00.evt | $DAQBIN/stdintoring reas_t00`

An event-built fragment from one EVB stage looks like:

```
-----------------------------------------------------------
Event 76 bytes long
Body Header:
Timestamp:    69440
SourceID:     0
Barrier Type: 0
004c 0000 0f40 0001 0000 0000 0000 0000 
0034 0000 0000 0000 0034 0000 001e 0000 
0014 0000 0f40 0001 0000 0000 0000 0000 
0000 0000 000c 0000 01f4 0f0e 4020 0008 
1b20 0000 0000 993a 1c7a 0000
```

The same event from a second level of event-building with `glom --nobuild`:

```
-----------------------------------------------------------
Event 128 bytes long
Body Header:
Timestamp:    69440
SourceID:     255
Barrier Type: 0
0080 0000 0f40 0001 0000 0000 0000 0000 
0068 0000 0000 0000 0068 0000 001e 0000 
0014 0000 0f40 0001 0000 0000 0000 0000 
0000 0000 004c 0000 0f40 0001 0000 0000 
0000 0000 0034 0000 0000 0000 0034 0000 
001e 0000 0014 0000 0f40 0001 0000 0000 
0000 0000 0000 0000 000c 0000 01f4 0f0e 
4020 0008 1b20 0000 0000 993a 1c7a 0000 
```
