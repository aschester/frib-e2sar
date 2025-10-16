# FRIB-E2SAR dev

This is a repository for various E2SAR-related development codes for FRIB data processing. The subdirectories contain example codes for inspecting E2SAR configuration info and running simple workflows on FRIB data. The Python and bash scripts in the scripts/ directory are used to run receive-side workflows which include some processing steps (fitting, writing to a sink, etc.).

This document is not intended as a comprehensive User's Manual for this project, rather it is a loose and evolving set of developer notes so I don't forget how to do this stuff.

## Requirements

- E2SAR software and prereqs (https://github.com/JeffersonLab/E2SAR/wiki/Code-and-Binaries)
- NSCLDAQ 12.1 or later
- NSCLDAQ UnifiedFormat library
- CMake 3.18 or later
- Compiler support for C++17 standard

A Docker image based on Debian 11 (Bullseye) with preinstalled E2SAR binaries and prereqs is available here: https://hub.docker.com/r/aschester/e2sar-bullseye. The Docker image can be used to build images with Apptainer, Shifter, etc. The project will incorporate its own <a href="https://github.com/FRIBDAQ/UnifiedFormat">Unified Format Library</a> as a git submodule.

Since the E2SAR binaries are built against a newer Python version and boost libraries than are available through the standard Debian repositories, it is advisable to build a version of NSCLDAQ 12.1 under the above container with the same Python and boost used by E2SAR. See [Appendix A](#appendix-a-nscldaq-build-configuration) for an example of how to do this.

## Building the example codes

- Clone the repository from https://github.com/aschester/frib-e2sar.git. The current branch as of 15 Oct 2025 is 2.0-dev.
- Ensure the UnifiedFormat Library submodule is initialized properly by running the commands `git submodule init` and `git submodule update`. One can ensure the submodules are initialized properly by cloning the project via `git clone --recurse-submodules [URL of git repository]` as well.
- Build and install the project using the install script:

```
./install [-p <install_prefix>] [-c <build_cores>]
```

If no installation prefix is provided, the install path defaults to ~/frib-e2sar. By default the build will use one (1) core.

The install directory contains six (6) folders:
- bin/ : contains project binaries
- include/ : contains project headers
- ini/ : example initialization files for the `Segmenter` and `Reassembler`
- lib/ : contains project libraries, including those for the Unified Format subproject
- scripts/ : definitions of and run scripts for various FRIB-E2SAR workflows
- share/ : project documentation and examples for frib-e2sar (share/html/) and Unified Format (share/htmldocs/ and share/examples/)

## Running the examples

Running programs and scripts with the `-h` flag will show command-line options. Sensible defaults for parameters are set in most cases. All programs which use the E2SAR streaming libraries expect an EJFAT URI either passed on the command line or stored in the environment variable `EJFAT_URI`. Arguments read from the command line when will override preset values. If an initialization file is used to configure the E2SAR `Segmenter` or `Reassembler`, command-line options will override whatever values are read from the initialization file.

The EJFAT URI has the form:

`ejfat[s]://[<token>@]<cp_host>:<cp_port>/[lb/<lb_id>][?[data=<data_host>][&sync=<sync_host>:<sync_port>][&sessionid=<session id>]]`

You may need to use quotes around the URI string to prevent the shell from interpreting `&` as a command. For one-to-one reassembly (i.e., without the E2SAR control plane), the `Reassembler` should listen on the data port.

### Example workflow: running the FRIB-E2SAR event-building pipeline

Workflows are run using a series of Python scripts. As an example, lets look closer at the workflow used to build events over E2SAR. The E2SAR EVB pipeline scripts are:
- `run_reassemble_and_sort.py` : runs the `Reassembler` and, if sending raw NSCLDAQ 12 DDAS data, NSCLDAQ's `ddasSort`. The `Reassembler` outputs reassembled raw data into a raw ringbuffer where it is made available to the rest of the processing pipeline for sorting/fragment ordering, (optional) building events, and logging to a final data sink.
- `run_evb_and_log.py` : Run NSCLDAQ EVB pipeline to build events from ordered data and write it to disk with the NSCLDAQ `eventlog`
- `setup_evb.sh` : bash script to initialize the EVB and configure its clients. This script is called by `run_evb_and_log.py` and is generally not intended to be run standalone. Users may need to edit this script to match their particular source configuration.

The intention is that the user can make copies of all the necessary workflow scripts and edit them as needed for their own purposes. The various run scripts import classes from `workflows.py` and `process_runner.py`, so either local copies of these files must exist in the same directory as the run script or their install location needs to be added to the Python path.

The `run_reassemble_and_sort.py` script should be run first to create the ringbuffers needed by the EVB pipeline. Running `run_evb_and_log.py` will call the specified startup script to configure the EVB pipeline and wait for data. The data sources in the startup script are set by the user, similar to a `ReadoutCallouts.tcl` file; other EVB parameters can be set on via the command line. Both the `Reassembler`/`ddasSort` and the EVB pipe/`eventlog` applications will tell you when they are ready to receive data. A ready state for an infinite-duration reassembly process assuming NSCLDAQ 12 data from a DDAS system and the EJFAT load balancer looks like:

#### run_reassemble_and_sort.py.py
```
<daq-ejfat-01:e2sar >./run_reassemble_and_sort.py -u $(cat EJFAT_URI_send) -d 10
[reas] Starting: /user/0400x/frib-e2sar/bin/e2sarwf --recv --ini /scratch/e2sar/reassembler_config.ini --ip 35.11.82.130 --port 20000 --threads 1 --deq 1 --duration 10 --sinkname reas --uri < URI not shown >
[reas] Control plane will be ON
[reas] Expecting LB header NO
[reas] *** Make sure the LB has been reserved and the URI reflects the reserved instance information.
[reas] ----- Receiver configuration -----
[reas] Reassembler flags:
[reas]  useCP           1
[reas]  useHostAddress  0
[reas]  period_ms       100
[reas]  validateCert    1
[reas]  Ki, Kp, Kd      0, 0, 2
[reas]  setPoint        0
[reas]  epoch_ms        1000
[reas]  portRange       -1
[reas]  withLBHeader    0
[reas]  eventTimeout_ms 500
[reas]  rcvSocketBufSize        536870784 (bytes)
[reas]  weight          1
[reas]  min_factor      0.5
[reas]  max_factor      2
[reas] Receiving on ports 20000:20000
[sort] Starting: /usr/opt/daq/12.1-dev.e2sar/bin/ddasSort --source tcp://localhost/reas --sink reas_sort --window 10
[reas] 2025-Oct-15 20:11:08 Stats:
[reas]  Events Received: 0
[reas]  Events Lost in reassembly: 0
[reas]  Events Lost in enqueue: 0
[reas]  Data Errors: 0
[reas]  gRPC Errors: 0
[reas]  Events lost so far: 0
... more stats ...
```

#### run_evb_and_log.py
```
<daq-ejfat-01:e2sar >./run_evb_and_log.py --startup ./setup_evb.sh 
[evb] Starting: ./setup_evb.sh -source reas -sink frib_e2sar_evb -glomdt 1000 -window 20 
[log] Starting: /usr/opt/daq/12.1-dev.e2sar/bin/eventlog --source tcp://localhost/frib_e2sar_evb --number-of-sources 1 --segmentsize 1000g --oneshot 
[evb] Orderer listening on 30999
```

Running the `run_evb_and_log.py` script will also pop up an EVB GUI showing the registered data source(s) and statistics. Configuration of ring sources and initialization of the EVB pipe is performed in the `setup_evb.sh` script as mentioned previously. To send data through the E2SAR processing pipeline, run the `e2sarwf` program with the `--send` option:

```
<daq-ejfat-01:e2sar >~/frib-e2sar/bin/e2sarwf --send -u < URI not shown > -s file:///scratch/e2sar/data/15Jul2025-111813-run-0077-00.evt -r 2 --ddasraw
Control plane                ON
Number of send sockets:      4
*** Make sure the LB has been reserved and the URI reflects the reserved instance information.
Adding senders to LB:
< ip addr > 
Getting LB status:
        Contacting: < URI not shown >
        LB ID: < id >
----- Sender configuration -----
Segmenter flags:
        dpV6            0
        connectedSocket 1
        useCP           1
        syncPeriodMs    1000
        syncPeriods     2
        mtu             9000 (bytes)
        numSendSockets  4
        sndSockBufSize  536870784 (bytes)
Using URI:   < URI not shown >
Sending:     all events
Data ID:     0
Source ID:   0
evtBufSize:  1048576 bytes
sendRate:    2 Gbps
Queue size:  10240
E2SAR selected optimizations:  none
NSCLDAQ format version:        12
Event number is: event count
--------------------------------
Inter-event sleep time is 4194 microseconds
Segmenter started OK
Stopping sender threads...
Removing senders: < ip addr >
```

Once the data has been sent, the sender will report some statistics:

```
Completed, 210981 frames sent, 0 errors
Send loop runtime: 7.77169 seconds
Stopping sender threads...
Removing senders: < ip addr >
```

After the proper number of end runs is seen (as many as there are data sources unless otherwise specified), the EVB pipeline will restart and wait to receive more data. The restart process should take less than 5 seconds.

The above example assumes raw NSCLDAQ data coming from a DDAS system. The `run_reassemble_and_fit.py` script assumes the data is already built and in a format which can be handled by the NSCLDAQ EventEditor, `run_reassemble_and_log.py` is agnostic about whether the data is built or not, it will write whatever it receives to an event file. 

## Notes

- Ensure that `useCP` is set to the same value in both the `Segmenter` and Reassembler configuration files. The default assumption is that all data goes through the Control Plane.
- Event-building pipeline configuration is entirely hardcoded, so be cautious when changing, e.g., ringbuffer names.
- In most cases the pipeline can safely shut itself down when it encounters and error. Ctrl-C (SIGINT) will be propagated to all child processes and is generally the safest way to exit currently running scripts. In some cases hanging processes must be killed on the command line. Most likely this is going to be a stray ringFragmentSource.

## Appendix A: NSCLDAQ build configuration

Below is an example command to configure NSCLDAQ 12.1 build to use the same version of boost and Python as the E2SAR software. This example is run from the build directory under the top-level NSCLDAQ source directory after running `autoconf -if` to generate the configuration script:

```
../configure --prefix=/usr/opt/daq/12.1-dev.e2sar \
--with-incorp-build-cores=8 \
--enable-docs \
--enable-usb \
--enable-epics-tools \
--with-epics-rootdir=/usr/opt/epics \
--enable-ddas \
--enable-ddas-docs \
--with-xiaapidir=/usr/opt/xiaapi/4.4.0 \
--with-firmwaredir=/usr/opt/ddas/firmware/2.2-001/firmware \
--with-dspdir=/usr/opt/ddas/firmware/2.2-001/dsp \
--with-rootsys=/usr/opt/root/6.26.04 \
--enable-caen-digitizer-support \
--with-caen-digitizer-libroot=/usr/opt/caendigitizerlibs \
--enable-caen-nextgen \
--with-boost=/usr/local/include/boost \
--with-boost-libdir=/usr/local/lib \
CXX=/usr/opt/mpi/openmpi-4.1.4/bin/mpicxx \
PYTHON=/usr/bin/python3
```

## Appendix B: Running workflows at NERSC

A few notes for getting things running at NERSC:
- Docbook docs do not build properly on Perlmutter systems. Edit the top-level CMakeLists.txt file and comment out the docs build directory: `#add_subdirectory(docs)`. A fix would be nice but its low priority.
- FRIB-E2SAR software binaries may be installed in a non-standard location. Make a local copy of and edit `workflows.py` to point the `e2sarwf` variable to the installation binary directory: `e2sarwf=f"""{str(Path.home())}/frib-e2sar/bin/e2sarwf"""`.


