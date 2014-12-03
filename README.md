Toy-OS
======

A toy operating system built by OS one-six-one

**Tests**
------------------------

1. Virtual memory integrity and TLB tests
* Test if kernel is properly handling faults when the TLB is full
![uw-testbin case](https://github.com/SSDong/Toy-OS/raw/master/snapshots/vm-data1.PNG)
![uw-testbin case](https://github.com/SSDong/Toy-OS/raw/master/snapshots/vm-data3.PNG)

2. Read-only memory tests
* Test the protection of read-only memory
![uw-testbin case](https://github.com/SSDong/Toy-OS/raw/master/snapshots/romemwrite.PNG)

3. physical memory management tests
* Test if kernel can reclaim and re-use of physical memory so that it can run programs sequentially forever without crashing. 
![uw-testbin case](https://github.com/SSDong/Toy-OS/raw/master/snapshots/physical memory.PNG)




