# Linear Ringbuffer

This is an implementation of a ringbuffer that will always expose its contents
as a flat array using the mmap trick. It is mainly useful for interfacing with
C APIs, where this feature will save the programmer from doing a lot of the
arithmetic he would otherwise have to do.

Please see the lengthy comment in `linear_ringbuffer.hpp` for the actual README.

