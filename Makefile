tests: tests.cpp
	g++ $+ -I. -o $@ $(CFLAGS) $(CXXFLAGS)
