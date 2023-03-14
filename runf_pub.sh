#!/bin/bash
trap 'kill 0' EXIT
./raft_follower_pubsub 51112 127.0.0.1:51111 &
./raft_follower_pubsub 51114 127.0.0.1:51111 &
./raft_follower_pubsub 51116 127.0.0.1:51111 &
./raft_follower_pubsub 51118 127.0.0.1:51111 &
wait
