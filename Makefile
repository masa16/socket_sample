CC := gcc
CX := g++
CXFLAGS := -g -O3 -std=c++17 -Wall
LDFLAGS := -lm -lgflags -pthread

T1 := raft_leader
T2 := raft_follower
ALL := $(T1) $(T2)

all: $(ALL)

%: %.cc
	$(CX) $(CXFLAGS) $(LDFLAGS) -o $@ $^

%: %.c
	$(CC) $(CCFLAGS) $(LDFLAGS) -o $@ $^

run: all
	./raft ../../raft.conf

clean:
	rm -f *~ *.exe *.stackdump $(ALL) *.o
