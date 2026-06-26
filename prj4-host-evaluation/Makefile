CXX := g++
CXXFLAGS := -O2 -std=c++11 -Wall

TARGET := kv_fs_bench
OBJS := kv_fs_backend.o

$(TARGET): kv_fs_bench.cc $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

kv_fs_backend.o: kv_fs_backend.cc kv_fs_backend.h
	$(CXX) $(CXXFLAGS) -c kv_fs_backend.cc -o kv_fs_backend.o

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean

