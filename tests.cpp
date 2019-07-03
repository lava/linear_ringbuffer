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
	for (int i=0; i<n; ++i) {
		sbuf[i] = 'x';
		tbuf[i] = 0;
	}

	assert(rb.free_size() == rb.capacity());
	::memcpy(rb.end(), sbuf, n);
	rb.commit(n);

	::memcpy(tbuf, rb.begin(), rb.size());
	rb.consume(n);

	assert(rb.size() == 0);

	for (int i=0; i<n; ++i) {
		assert(tbuf[i] == 'x');
	}
	std::cout << "success\n";

	// Test 2: Check than we can read and write "over the edge", i.e.
	// starting in one copy of the buffer and ending in the other.
	std::cout << "Test 2..." << std::flush;
	for (int i=0; i<n; ++i) {
		sbuf[i] = 'y';
	}
	rb.commit(n/2);
	rb.consume(n/2);

	// Arbitrarily use some amount n/2 < m < n to ensure we write
	// over the edge.
	int m = n/2 + n/4;
	::memcpy(rb.end(), sbuf, m);
	rb.commit(m);

	assert(rb.size() == m);

	::memcpy(tbuf, rb.begin(), m);
	rb.consume(m);

	assert(rb.size() == 0);
	for (int i=0; i<m; ++i) {
		assert(tbuf[i] == 'y');
	}

	std::cout << "success\n";
}
