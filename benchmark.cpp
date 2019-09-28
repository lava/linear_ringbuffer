#include <bev/linear_ringbuffer.hpp>
#include <bev/io_buffer.hpp>

#include <iostream>
#include <thread>

// Usage:
//
//    cat /dev/zero | ./benchmark (io_buffer|linear_ringbuffer) >/dev/null

std::atomic<int64_t> s_read_bytes;
std::atomic<int64_t> s_write_bytes;

void benchmark_linear_ringbuffer()
{
    bev::linear_ringbuffer b(64*1024);
    const int in = fileno(stdin);
    const int out = fileno(stdout);

    // TODO - Test if a version using select/poll/epoll would be faster.
    while (true) {
        ssize_t n = ::read(in, b.write_head(), b.free_size());
        if (n <= 0) break;
        b.commit(n);

        s_read_bytes.fetch_add(n, std::memory_order_relaxed);

        n = ::write(out, b.read_head(), b.size());
        if (n <= 0) break;
        b.consume(n);

        s_write_bytes.fetch_add(n, std::memory_order_relaxed);
    }

    perror("read or write:");
}

void benchmark_io_buffer()
{
    bev::io_buffer b(64*1024);
    const int in = fileno(stdin);
    const int out = fileno(stdout);
    constexpr int BLOCKSIZE = 32*1024;

    while (true) {
        auto slab = b.prepare(BLOCKSIZE);
        ssize_t n = ::read(in, slab.data, slab.size);
        if (n <= 0) break;
        b.commit(n);

        s_read_bytes.fetch_add(n, std::memory_order_relaxed);

        n = ::write(out, b.read_head(), b.size());
        if (n <= 0) break;
        b.consume(n);

        s_write_bytes.fetch_add(n, std::memory_order_relaxed);
    }
}

int main(int argc, char* argv[]) {
    // It's actually hard to really measure the performance overhead of the buffers,
    // themselves since in theory they should be much faster than the I/O. To make this
    // more rigorous, one could imagine testing against e.g. a Gigabit network, or by
    // artificially throttling the core on which the benchmark is running.

    if (argc <= 1) {
        std::cerr << "Usage: `cat <datasource> | ./benchmark (io_buffer|linear_ringbuffer) >/dev/null`\n";
        return 1;
    }

    std::thread *iothread;
    if (std::string(argv[1]) == "io_buffer") {
        iothread = new std::thread(benchmark_io_buffer);
    } else {
        iothread = new std::thread(benchmark_linear_ringbuffer);
    }

    int64_t read_old = s_read_bytes;
    int64_t write_old = s_write_bytes;
    while (true) {
        sleep(1);
        int64_t read = s_read_bytes;
        int64_t write = s_write_bytes;
        std::cerr << "read " << (read - read_old) / 1024 / 1024 << "MiB/s, write " << (write - write_old) / 1024 / 1024 << "MiB/s\n";
        read_old = read;
        write_old = write;
    }

    iothread->join();
}
