#include <cstdint>
#include <memory>
#include <functional>

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
// # Exceptions
//
// Both constructors of `io_buffer` may throw `std::bad_alloc` on allocation
// failure. (Although the constructor accepting a `unique_ptr` will in practice
// not allocate, because the type-erased deleter should be small enough for the
// small function optimization. However, this is not guaranteed.)
//
// All other operations on the buffer are noexcept. In particular, class `io_buffer_view`
// provides a fully noexcept interface.
//
//
// # Memory Management
//
// The constructor `io_buffer::io_buffer(std::unique_ptr<char> buffer, size_t size)` can be
// used to pass ownership of an existing memory region to an `io_buffer`. Custom deleters
// are supported with that constructor.
//
// The class `io_buffer_view` can be used to treat an existing memory region as an
// `io_buffer` without assuming ownership of the underlying memory.
//

using std::size_t;


// This class accepts an arbitrary region of memory and treats it as an `io_buffer`.
class io_buffer_view
{
public:
    struct slab {
        char* data;
        size_t size;
    };

    // NOTE: If the default constructor is used, the view is in undefined state
    // until `assign()` is called.
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


namespace detail {

// Class `io_buffer_storage` holds a pointer to the allocated memory region along
// with a type-erased deleter.
class io_buffer_storage
{
public:
    template<typename Deleter>
    io_buffer_storage(std::unique_ptr<char, Deleter> storage, size_t size);

protected:
    std::unique_ptr<char, std::function<void(char*)>> buffer_;    
};

} // namespace detail


// The actual `io_buffer` is an `io_buffer_view` that allocates its own storage.
class io_buffer
  : private detail::io_buffer_storage
  , public io_buffer_view
{
public:
    io_buffer(size_t size);

    template<typename Deleter>
    io_buffer(std::unique_ptr<char, Deleter> storage, size_t size);
};


} // namespace bev


// Implementation.

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <stdexcept>

namespace bev {
namespace detail {

template<typename Deleter>
io_buffer_storage::io_buffer_storage(std::unique_ptr<char, Deleter> storage, size_t size)
{
    // Can use `if constexpr` here in C++17.
    if (std::is_reference<Deleter>::value) {
        std::function<void(char*)> deleter = std::ref(storage.get_deleter());
        buffer_ = std::unique_ptr<char, std::function<void(char*)>>{storage.release(), deleter};
    } else {
        // Non-reference deleters must be at least MoveConstructible.
        buffer_ = std::unique_ptr<char, std::function<void(char*)>>{
            storage.release(), std::function<void(char*)>(std::move(storage.get_deleter()) )};
    }
}

} // namespace detail


io_buffer::io_buffer(size_t size)
  : detail::io_buffer_storage(std::unique_ptr<char>(std::allocator<char>().allocate(size)), size)
  , io_buffer_view(this->detail::io_buffer_storage::buffer_.get(), size)
{
  char* storage = detail::io_buffer_storage::buffer_.get();
  // In C++17, the loop can be replaced by
  // `std::uninitialized_default_construct(storage, storage+size)`.
  for (size_t i=0; i<size; ++i) {
    new (storage+i) char;
  }
}


template<typename Deleter>
io_buffer::io_buffer(std::unique_ptr<char, Deleter> storage, size_t size)
  : detail::io_buffer_storage(std::move(storage), size)
  , io_buffer_view(this->detail::io_buffer_storage::buffer_.get(), size)
{
}


io_buffer_view::io_buffer_view() noexcept = default;


inline io_buffer_view::io_buffer_view(char* data, size_t size) noexcept
  : buffer_(data)
  , length_(size)
  , head_(0)
  , tail_(0)
{
}


inline void io_buffer_view::assign(char* data, size_t size) noexcept
{
    buffer_ = data;
    length_ = size;
    head_ = 0;
    tail_ = 0;
}


inline char* io_buffer_view::read_head() noexcept
{
    return buffer_ + head_;
}


inline char* io_buffer_view::write_head() noexcept
{
    return buffer_ + tail_;
}


inline size_t io_buffer_view::size() const noexcept
{
    return tail_ - head_;
}


inline size_t io_buffer_view::capacity() const noexcept
{
    return length_ - this->size();
}


inline size_t io_buffer_view::free_size() const noexcept
{
    return length_ - tail_;
}


inline io_buffer_view::slab io_buffer_view::prepare(size_t n) noexcept
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


inline void io_buffer_view::commit(std::size_t n) noexcept
{
    // assert: tail_ + n < size
    tail_ += n;
}


inline void io_buffer_view::consume(std::size_t n) noexcept
{
    // assert: size() <= n
    head_ += n;
    if (head_ >= tail_) {
        head_ = tail_ = 0;
    }
}


inline void io_buffer_view::clear() noexcept
{
    head_ = tail_ = 0;
}

} // namespace bev

