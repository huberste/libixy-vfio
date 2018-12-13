# libixy-vfio - the VFIO library for the userspace network driver in 1000 lines of code

ixy is a simple userspace packet processing framework.
It takes exclusive control of a network adapter and implements the *whole driver* in userspace.
Its architecture is similar to [DPDK](http://dpdk.org/) and [Snabb](http://snabb.co) and completely different from (seemingly similar) frameworks such as netmap, pfq, pf_ring, or XDP (all of which rely on kernel components).
In fact, reading both DPDK and Snabb drivers was crucial to understand some parts of the [Intel 82599 datasheet](https://www.intel.com/content/dam/www/public/us/en/documents/datasheets/82599-10-gbe-controller-datasheet.pdf) better.

Check out the [draft of our paper](https://www.net.in.tum.de/fileadmin/bibtex/publications/papers/ixy_paper_draft2.pdf) or [watch the recording of our talk at 34C3](https://media.ccc.de/v/34c3-9159-demystifying_network_cards) to learn more.

ixy is designed for educational purposes to learn how a network card works at the driver level.
Lack of kernel code and external libraries allows you to look through the whole code from startup to the lowest level of the driver.
Low-level functions like handling DMA descriptors are rarely more than a single function call away from your application logic.

A whole ixy app, including the whole driver, is only ~1000 lines of C code.
Check out the `ixy-fwd` and `ixy-pktgen` example apps and look through the code.
The code often references sections in the [Intel 82599 datasheet](https://www.intel.com/content/dam/www/public/us/en/documents/datasheets/82599-10-gbe-controller-datasheet.pdf) or the [VirtIO specification](http://docs.oasis-open.org/virtio/virtio/v1.0/virtio-v1.0.pdf), so keep them open while reading the code.
You will be surprised how simple a full driver for a network card can be.



# Features
* Driver for Intel NICs in the `ixgbe` family, i.e., the 82599ES family (aka Intel X520)
* Driver for paravirtualized virtio NICs
* Less than 1000 lines of C code for a packet forwarder including the whole driver
* No kernel modules needed
* Simple API with memory management, similar to DPDK, easier to use than APIs based on a ring interface (e.g., netmap)
* Support for multiple device queues and multiple threads
* Super fast, can forward > 25 million packets per second on a single 3.0 GHz CPU core
* Super simple to use: no dependencies, no annoying drivers to load, bind, or manage - see step-by-step tutorial below
* BSD license

# Supported hardware
Tested on an Intel 82599ES (aka Intel X520), X540, and X550. Might not work on all variants of these NICs because our link setup code is a little bit dodgy.

# How does it work?
Check out the [draft of our paper](https://www.net.in.tum.de/fileadmin/bibtex/publications/papers/ixy_paper_draft2.pdf).

If you prefer to dive into the code: Start by reading the apps in [src/app](https://github.com/emmericp/ixy/tree/master/src/app) then follow the function calls into the driver. The comments in the code refer to the [Intel 82599 datasheet](https://www.intel.com/content/dam/www/public/us/en/documents/datasheets/82599-10-gbe-controller-datasheet.pdf) (Revision 3.3, March 2016).
