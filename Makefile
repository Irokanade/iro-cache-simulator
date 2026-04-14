CXX = g++-15
CXXFLAGS = -std=c++20 -O2 -Wall -Wextra -MMD -MP

OBJS = cpu.o cache_debugger.o main.o
DEPS = $(OBJS:.o=.d)

all: main

main: $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o main

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f main $(OBJS) $(DEPS)

-include $(DEPS)

.PHONY: all clean
