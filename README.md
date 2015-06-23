This is a repository of test data sampled from various kinds of hardware, as 
well as a shared library which can be used with ``LD_PRELOAD`` to trick 
[lshw](http://www.ezix.org/project/wiki/HardwareLiSter) into thinking it is 
scanning the sample hardware.

This makes it possible to work on lshw patches while checking for any 
unexpected changes or regressions in the output across a wide range of 
hardware.


Running the tests
-----------------

To run the tests, first build the wrapper library:

    make

You can then run all test cases, comparing your lshw XML output to the expected 
XML which is checked into this repository:

    ./run-tests.sh

If you have built lshw from source, pass the binary's path in ``LSHW``:

    LSHW=../lshw/src/lshw ./run-tests.sh


Status of the tests
-------------------

These tests are expected to pass with the latest published release of lshw, 
B.02.17.

However, note that if you run them all on your x86_64 workstation some failures 
are expected due to compile-time differences in lshw across platforms.


Collecting new hardware samples
-------------------------------

On the system you wish to collect a sample from, run the ``collect-sample.sh`` 
script. This script needs root access because many of the kernel special files 
it reads from are readable only by root.

    sudo ./collect-sample.sh testdata/<new-sample-name>

Note that the script will emit many errors when it runs, due to attempting to 
read from various ``/sys`` special files that are not actually readable.

On pre-systemd distros it may be necessary to manually mount debugfs (TODO 
check if we can just grab ``/proc/bus/usb/devices`` instead):

    sudo mount -t debugfs none /sys/kernel/debug
