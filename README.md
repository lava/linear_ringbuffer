# Contents

Despite the name, this repository contains implementations for two different
buffers:

  * Linear Ringbuffer: `include/bev/linear_ringbuffer.hpp`
  * IO Buffer:  `include/bev/io_buffer.hpp`

This top-level `README` mainly describes the former. Take a look at the block comments
in the respective source files for the most up-to-date and specific documentation.

# Linear Ringbuffer

This is an implementation of a ringbuffer that will always expose its contents
as a flat array using the mmap trick. It is mainly useful for interfacing with
C APIs, where this feature can vastly simplify program logic by eliminating all
special case handling when reading or writing data that wraps around the edge
of the ringbuffer.


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
a pointer and size, and to afterwards call `commit()` to adjust the write
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
produce/consume appear atomic to the buffer, otherwise data loss
can occur.


# Errors and Exceptions

The ringbuffer provides two way of initialization, one using exceptions
and one using error codes. After initialization is completed,
all operations on the buffer are `noexcept` and will never return
an error.

To use error codes, the `linear_ringbuffer(delayed_init {})` constructor
can be used. In this case, the internal buffers are not allocated until
`linear_ringbuffer::initialize()` is called, and all other member function
must not be called before the buffers have been initialized.

    bev::linear_ringbuffer rb(linear_ringbuffer::delayed_init {});
    int error = rb.initialize(MIN_BUFSIZE);
    if (error) {
       [...]
    }

The possible error codes returned by `initialize()` are:

  * `ENOMEM`: The system ran out of memory, file descriptors, or the maximum
              number of mappings would have been exceeded.

  * `EINVAL`: The `minsize` argument was 0, or `2*minsize` did overflow.

  * `EAGAIN`: Another thread allocated memory in the area that was intended
              for the second copy of the buffer. Callers are encouraged
              to try again.

If exceptions are preferred, the `linear_ringbuffer(int minsize)`
constructor will attempt to initialize the internal buffers immediately and
throw a `bev::initialization_error` on failure, which is an exception class
derived from `std::runtime_error`. The error code as described above is
stored in the `errno_` member of the exception.


# Concurrency

It is safe to be use the buffer concurrently for a single reader and a single writer,
but mutiple readers or multiple writers must serialize their accesses with a mutex.

If the ring buffer is used in a single-threaded application, the
`linear_ringbuffer_st` class can be used to avoid paying for atomic
increases and decreases of the internal size.


# Comparison

Note that the main purpose of this class is not performance but convenience
from erasing special-case handling when using the buffer, so no
effort was put into creating a comprehensive benchmarking suite.

Nonetheless, I was curious how this would compare to alternative approaches
to implementing buffers for the same use-case, so I added an implementation
of an `io_buffer` inspired the `boost::beast::flat_static_buffer` together
with a simple [benchmark](./benchmark.cpp).

On my machine, I do not observe any measurable performance difference between
`linear_ringbuffer` and `io_buffer`.

However, simple `dd` reports almost 4 times the speed when piping from `/dev/zero`
to `/dev/null`. I do not know if this is due to some inefficiency in the buffer
implmenetation, in the test setup, or in the accounting, however inspecting
the source code of `dd` did not reveal any crazy performance tricks. (If anyone
does figure out the reason, please contact me)


Aside from performance, here is an overview of the differences I'm aware of
between `linear_ringbuffer` and `io_buffer`:

The `linear_ringbuffer` is very pleasant to use because
   - it has a nicer, very simple API, and
   - it supports concurrent reading and writing.

However, on the negative side
   - it takes twice as much address space (not actual memory, though)
   - the initialization us unavoidable racy, and finally
   - it needs some kernel+glibc support. While this shouldnt be problematic
     in a desktop/server environment, as a non-representative data point I was
     not able to cross-compile this for a mips-linux-uclibc environment.


On the other hand, the `io_buffer`
   - can use arbitrary existing buffer, i.e. there's no no need to allocate
     memory at all,
   - and even when it does allocate, the allocation is platform-independent,
     i.e. does not depend on `mremap()` semantics.

On the other hand,
   - it does not support concurrency at all since calling `prepare()`
     invalidates the read head,
   - and the API is somewhat harder to use because depending on the access
     pattern it might not be possible to use the full capacity of the buffer.
