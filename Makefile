BENCHMARK_LIBS = -pthread

HEADERS = \
  include/bev/linear_ringbuffer.hpp \
  include/bev/io_buffer.hpp

all: benchmark tests

benchmark: benchmark.cpp $(HEADERS)
	g++ $< -O2 -g3 -I./include -o $@ $(CFLAGS) $(CXXFLAGS) $(BENCHMARK_LIBS)

tests: tests.cpp $(HEADERS)
	g++ $< -g3 -I./include -o $@ $(CFLAGS) $(CXXFLAGS)


PREFIX ?= /usr
install:
	install -d include/ $(DESTDIR)/$(PREFIX)
