CXX = g++-15
CXXFLAGS = -std=c++20 -O2 -Wall -Wextra

OBJS = cpu.o cache_debugger.o main.o

all: main

main: $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o main

%.o: %.cpp cpu.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f main $(OBJS)

.PHONY: all clean
