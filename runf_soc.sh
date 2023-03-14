#!/bin/bash
trap 'kill 0' EXIT
./raft_follower_socket 51112 &
./raft_follower_socket 51114 &
./raft_follower_socket 51116 &
./raft_follower_socket 51118 &
wait
