CXX := g++
CXXFLAGS := -O2 -std=c++17 -Wall

TARGET := ftl_bench
OBJS := nvme_passthru.o

$(TARGET): ftl_bench.cc $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

nvme_passthru.o: nvme_passthru.cc nvme_passthru.h
	$(CXX) $(CXXFLAGS) -c nvme_passthru.cc -o nvme_passthru.o

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean

