CCFLAGS = -O2 --std=c++17

all: test

test: nvme_passthru
	g++ $(CCFLAGS) test.cc nvme_passthru.o -o test

nvme_passthru:
	g++ -c $(CCFLAGS) nvme_passthru.cc

clean:
	rm -f test
	rm -f nvme_passthru.o
