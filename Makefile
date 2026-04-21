CXX = g++-15
CXXFLAGS = -std=c++23 -O2 -Wall -Wextra -Wconversion -MMD -MP

OBJS = cpu.o main.o
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
