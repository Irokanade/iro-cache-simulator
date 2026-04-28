all:
	g++ -O2 -Wall -Wextra -Wconversion -std=c++23 main.cpp -o main

clean:
	rm -f main
