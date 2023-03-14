CC := gcc
CX := g++
CXFLAGS := -g -O3 -std=c++17 -Wall
CCFLAGS := -g -O3 -Wall
LDFLAGS := -L$(HOME)/local/lib -lm -lzmq -lgflags -pthread

T1 := raft_leader
T2 := raft_follower
ALL := $(T1)_socket $(T2)_socket $(T1)_pubsub $(T2)_pubsub # $(T1)_reqrep $(T2)_reqrep

all: $(ALL)

%: %.cc
	$(CX) $(CXFLAGS) -o $@ $^ $(LDFLAGS)

%: %.c
	$(CC) $(CCFLAGS) -o $@ $^ $(LDFLAGS)

raft_leader_pubsub: raft_leader.cc
	$(CX) $(CXFLAGS) -DZMQ_PUBSUB -o $@ $^ $(LDFLAGS)

raft_follower_pubsub: raft_follower.c
	$(CC) $(CCFLAGS) -DZMQ_PUBSUB -o $@ $^ $(LDFLAGS)

raft_leader_reqrep: raft_leader.cc
	$(CX) $(CXFLAGS) -DZMQ_REQREP -o $@ $^ $(LDFLAGS)

raft_follower_reqrep: raft_follower.c
	$(CC) $(CCFLAGS) -DZMQ_REQREP -o $@ $^ $(LDFLAGS)

raft_leader_socket: raft_leader.cc
	$(CX) $(CXFLAGS) -o $@ $^ $(LDFLAGS)

raft_follower_socket: raft_follower.c
	$(CC) $(CCFLAGS) -o $@ $^ $(LDFLAGS)

run: all
	./raft ../../raft.conf

clean:
	rm -f *~ *.exe *.stackdump $(ALL) *.o
