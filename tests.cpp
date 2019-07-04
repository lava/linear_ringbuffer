#include <linear_ringbuffer.hpp>

#include <iostream>
#include <assert.h>

void print_mappings()
{
	pid_t pid = getpid();
	char cmd[128];
	sprintf(cmd, "cat /proc/%d/maps", pid);
	system(cmd);
}


int main() {
	bev::linear_ringbuffer rb(4096);
	int n = rb.capacity();

	// Test 1: Check that we can read and write the full capacity
	// of the buffer.
	std::cout << "Test 1..." << std::flush;
	char* sbuf = static_cast<char*>(malloc(rb.capacity()));
	char* tbuf = static_cast<char*>(malloc(rb.capacity()));
	std::fill_n(sbuf, n, 'x');
	std::fill_n(tbuf, n, '\0');

	assert(rb.free_size() == rb.capacity());
	::memcpy(rb.write_head(), sbuf, n);
	rb.commit(n);

	::memcpy(tbuf, rb.read_head(), rb.size());
	rb.consume(n);

	assert(rb.size() == 0);

	std::fill_n(tbuf, n, 'x');
	std::cout << "success\n";

	// Test 2: Check than we can read and write "over the edge", i.e.
	// starting in one copy of the buffer and ending in the other.
	std::cout << "Test 2..." << std::flush;
	rb.clear();
	std::fill_n(sbuf, n, 'y');
	rb.commit(n/2);
	rb.consume(n/2);

	// Arbitrarily use some amount n/2 < m < n to ensure we write
	// over the edge.
	int m = n/2 + n/4;
	::memcpy(rb.write_head(), sbuf, m);
	rb.commit(m);

	assert(rb.size() == m);

	::memcpy(tbuf, rb.read_head(), m);
	rb.consume(m);

	assert(rb.size() == 0);
	for (int i=0; i<m; ++i) {
		assert(tbuf[i] == 'y');
	}

	std::cout << "success\n";

	// Test 3: Check that for-loop iteration works
	const char* test3 = "Test 3...success\n";
	rb.clear();
	::strcpy(reinterpret_cast<char*>(rb.write_head()), test3);
	rb.commit(strlen(test3));
	for (char c : rb) {
		std::cout << c;
	}
}
