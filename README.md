# Linear Ringbuffer

This is an implementation of a ringbuffer that will always expose its contents
as a flat array using the mmap trick. It is mainly useful for interfacing with
C APIs, where this feature will save the programmer from doing a lot of the
arithmetic he would otherwise have to do.


# Data Layout

From the outside, the ringbuffer contents always look like a flat array
due to the mmap trick:


        (head)  <-- size -->    (tail)
         v                       v
    /-----------------------------------|-------------------------------\
    |  buffer area                      | mmapped clone of buffer area  |
    \-----------------------------------|-------------------------------/
     <---------- capacity ------------->


    --->(tail)              (head) -----~~~~>
         v                   v
    /-----------------------------------|-------------------------------\
    |  buffer area                      | mmapped clone of buffer area  |
    \-----------------------------------|-------------------------------/


# Usage

The buffer provides two (pointer, length)-pairs that can be passed to C APIs,
`(write_head(), free_size())` and `(read_head(), size())`.

The general idea is to pass the appropriate one to a C function expecting
a pointer and size, and afterwards to call `commit()` to adjust the write
head or `consume()` to adjust the read head.

Writing into the buffer:

    bev::linear_ringbuffer rb;
    FILE* f = fopen("input.dat", "r");
    ssize_t n = ::read(fileno(f), rb.write_head(), rb.free_size());
    rb.commit(n);

Reading from the buffer:

    bev::linear_ringbuffer rb;
    FILE* f = fopen("output.dat", "w");
    ssize_t n = ::write(fileno(f), rb.read_head(), rb.size();
    rb.consume(n);

If there are multiple readers/writers, it is the calling code's
responsibility to ensure that the reads/writes and the calls to
produce/consume appear atomic to the buffer, otherwise data races
will occur.

# Errors and Exceptions

The ringbuffer provides two way of initialization, one using exceptions
and one using error codes.

To use error codes, use the `linear_ringbuffer(delayed_init {})` constructor.
In this case, the internal buffers are not allocated until
`linear_ringbuffer::initialize()` is called, and all other member function
must not be called before the buffers have been initialized.

    bev::linear_ringbuffer rb(linear_ringbuffer::delayed_init {});
    int error = rb.initialize(MIN_BUFSIZE);
    if (error) {
       [...]
    }

There should be only three possible error values:

  ENOMEM - The system ran out of memory, file descriptors, or the maximum
           number of mappings would have been exceeded.

  EINVAL - The `minsize` argument was 0, or 2*`minsize` did overflow.

  EAGAIN - Another thread allocated memory in the area that was intended
           to use for the second copy of the buffer. Callers are encouraged
           to try again.

If you prefer using exceptions, the `linear_ringbuffer(int minsize)`
constructor will attempt to initialize the internal buffers immediately and
throw `bev::initialization_error` on failure, which is an exception class
derived from `std::runtime_error`. The error code as described above is
stored in the `errno_` member of the exception.

# Concurrency

The buffer is "slightly thread-safe": it is safe to use concurrently by a
single reader and a single writer, but mutiple readers or multiple writers
must serialize their accesses with a mutex.

If the ring buffer is used in a single-threaded application, the
`linear_ringbuffer_st` class can be used to avoid paying for atomic
increases and decreases of the internal size.


# Performance

I don't really know, but I'd be glad if someone could contribute benchmarks
with comparisons to other ringbuffer implementations.

# Implementation Notes

Note that only unsigned chars are allowed as the element type. While we could
in principle add an arbitrary element type as an additional argument, there
would be comparatively strict requirements:

 - It needs to be trivially relocatable
 - The size needs to exactly divide PAGE_SIZE

Since the main use case is interfacing with C APIs, it seems more pragmatic
to just let the caller cast their data to `void*` rather than supporting
arbitrary element types.

The initialization of the buffer is subject to failure, and sadly this cannot
be avoided. [1] There are two sources of errors:

 1) Resource exhaustion. The maximum amount of available memory, file
 descriptors, memory mappings etc. may be exceeded. This is similar to any
 other container type.

 2) To allocate the ringbuffer storage, first a memory region twice the
 required size is mapped, then it is shrunk by half and a copy of the first
 half of the buffer is mapped into the (now empty) second half.
 If some other thread is creating its own mapping in the second half after
 the buffer has been shrunk but before the second half has been mapped, this
 will fail. To ensure success, allocate the buffers before branching into
 multi-threaded code.

[1] Technically, we could use `MREMAP_FIXED` to enforce creation of the
second buffer, but at the cost of potentially unmapping random mappings made
by other threads, which seems much worse than just failing. I've spent some
time scouring the manpages and implementation of `mmap()` for some trick to
make it work atomically but came up empty. If there is one, please tell me.
