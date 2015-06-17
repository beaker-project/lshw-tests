This is a repository of test data sampled from various kinds of hardware, as 
well as a shared library which can be used with ``LD_PRELOAD`` to trick 
[lshw](http://www.ezix.org/project/wiki/HardwareLiSter) into thinking it is 
scanning the sample hardware.

This makes it possible to work on lshw patches while checking for any 
unexpected changes or regressions in the output across a wide range of 
hardware.

Running
-------

To run the tests, first build the wrapper library:

    make

You can then run all test cases, comparing your lshw XML output to the expected 
XML which is checked into this repository:

    ./run-tests.sh

If you have built lshw from source, pass the binary's path in ``LSHW``:

    LSHW=../lshw/src/lshw ./run-tests.sh
