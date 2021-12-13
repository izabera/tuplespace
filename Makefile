CXXFLAGS = -std=c++20 -Wall -Wextra -fsanitize=thread

tuplespace: test.cpp tuplespace.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
