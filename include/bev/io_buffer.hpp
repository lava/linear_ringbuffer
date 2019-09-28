#include <cstdint>
#include <memory>

namespace bev {

// # IO Buffer
//
// A buffer whose main purpose it is to store binary data streams
// that must be buffered while being moved from a source to a sink,
// e.g. audio data waiting to go to the sound card or network traffic
// being pushed from one socket to another.
//
// The design is heavily inspired by `boost::beast::flat_static_buffer`,
// but the API has been adjusted for consistency with the `linear_ringbufer`
// in this repository and stripped of all boost dependencies.
//
//
// # Usage
//
// Writing data into the buffer:
//
//     bev::io_buffer iob;
//     bev::io_buffer::slab slab = iob.prepare(512);
//     ssize_t n = ::read(socket, slab.data, slab.size);
//     iob.commit(n);
//
// Reading data from the buffer:
//
//     bev::io_buffer iob;
//     ssize_t n = ::write(socket, iob.read_head(), iob.size());
//     iob.consume(n);
//
//
// # Multi-threading
//
// No concurrent operations are allowed.
//
//
// # Memory Management
//
// Class `io_buffer` is using the `std::default_allocator` to allocate its internal buffer.
// In order to use a custom allocator, `io_buffer_<Allocator> can be used.
// In addition, in order to use a custom region of memory directly without allocations,
// the class `io_buffer_view` can be used.

using std::size_t;


// This class accepts an arbitrary region of memory and treats it as an `io_buffer`.
class io_buffer_view
{
public:
    struct slab {
        char* data;
        size_t size;
    };

    io_buffer_view() noexcept;
    io_buffer_view(char* data, size_t n) noexcept;
    void assign(char* data, size_t n) noexcept;

    // NOTE: The returned `slab.size` might be less than requested.
    slab prepare(size_t size) noexcept;
    void commit(size_t n) noexcept;
    void consume(size_t n) noexcept;
    void clear() noexcept;

    char* read_head() noexcept;
    char* write_head() noexcept;

    size_t size() const noexcept;      // Amount of data inside the buffer.
    size_t free_size() const noexcept; // Amount of data that can be committed.
    size_t capacity() const noexcept;  // Amount of data that can be prepared.

private:
    char* buffer_;
    size_t length_;
    size_t head_;
    size_t tail_;
};


// The actual `io_buffer` is an `io_buffer_view` that allocates its own storage.
template<typename Allocator>
class io_buffer_
  : public Allocator
  , public io_buffer_view
{
public:
    static_assert(std::is_same<typename Allocator::value_type, char>::value,
        "Only char allocators are currently supported.");

    io_buffer_(size_t size);
    io_buffer_(const Allocator& allocator, size_t size);

private:
    struct allocator_deleter {
        void operator()(char* p); 

        Allocator* alloc_;
        size_t size_;
    };

    std::unique_ptr<char, allocator_deleter> buffer_;    
};


using io_buffer = io_buffer_<std::allocator<char>>;

} // namespace bev


// Implementation.

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <stdexcept>

namespace bev {

template<typename Allocator>
io_buffer_<Allocator>::io_buffer_(size_t size)
  : Allocator()
  , buffer_(static_cast<Allocator*>(this)->allocate(size), allocator_deleter {static_cast<Allocator*>(this), size})
{
  this->io_buffer_view::assign(buffer_.get(), size);
}


template<typename Allocator>
io_buffer_<Allocator>::io_buffer_(const Allocator& alloc, size_t size)
  : Allocator(alloc)
  , buffer_(static_cast<Allocator*>(this)->allocate_(size), allocator_deleter {static_cast<Allocator*>(this), size})
{
  this->io_buffer_view::assign(buffer_.get(), size);
}


template<typename Allocator>
void io_buffer_<Allocator>::allocator_deleter::operator()(char* p)
{
    alloc_->deallocate(p, size_);
}


io_buffer_view::io_buffer_view() noexcept = default;


io_buffer_view::io_buffer_view(char* data, size_t size) noexcept
  : buffer_(data)
  , length_(size)
  , head_(0)
  , tail_(0)
{
}


void io_buffer_view::assign(char* data, size_t size) noexcept
{
    buffer_ = data;
    length_ = size;
    head_ = 0;
    tail_ = 0;
}


char* io_buffer_view::read_head() noexcept
{
    return buffer_ + head_;
}


char* io_buffer_view::write_head() noexcept
{
    return buffer_ + tail_;
}


size_t io_buffer_view::size() const noexcept
{
    return tail_ - head_;
}


size_t io_buffer_view::capacity() const noexcept
{
    return length_ - this->size();
}


size_t io_buffer_view::free_size() const noexcept
{
    return length_ - tail_;
}


io_buffer_view::slab io_buffer_view::prepare(size_t n) noexcept
{
    // Make as much room as we can
    if (n > this->free_size()) {
        std::size_t size = tail_ - head_;
        ::memmove(buffer_, buffer_ + head_, size);
        tail_ = size;
        head_ = 0;
    }

    // If we still don't have enough, adjust request.
    if (n > this->capacity()) {
        n = this->capacity();
    }

    return slab {buffer_ + tail_, n};
}


void io_buffer_view::commit(std::size_t n) noexcept
{
    // assert: tail_ + n < size
    tail_ += n;
}


void io_buffer_view::consume(std::size_t n) noexcept
{
    // assert: size() <= n
    head_ += n;
    if (head_ >= tail_) {
        head_ = tail_ = 0;
    }
}


void io_buffer_view::clear() noexcept
{
    head_ = tail_ = 0;
}

} // namespace bev

