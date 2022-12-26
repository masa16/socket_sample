extern "C" {
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include "my_sock.h"
}
#include <cassert>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#include <thread>
#include <map>
#include "host_data.hh"

#define MAXEVENTS 10
#define PIPE_EVENT 1
#define SERVER_EVENT 2
#define BUF_SZ 65536

//#define PF printf
#define PF(...) {}

typedef struct _pipe_data {
  void *ptr;
  size_t size;
} pipe_data_t;

typedef struct _ep_data_ptr {
  int fd;
  int fd_res;
  int type;
} ep_data_ptr_t;

typedef struct _res_count {
  int count;
  int fd;
} res_count_t;


void read_ok(int sock) {
  char ok[3] = "\0\0";
  PF("read_ok(sock=%d) begin\n",sock);
  ssize_t ret = read(sock, ok, sizeof(ok));
  PF("read_ok(sock=%d) end ret=%zd\n",sock,ret);
  if (ret == 0) {
    perror("read_ok ");
    return;
  }
  if (ret != sizeof(ok) || strncmp(ok,"OK",3) != 0) {
    perror("read_ok error ");
    abort();
  }
}

void write_ok(int sock) {
  static char ok[3] = "OK";
  int ret = write(sock, ok, sizeof(ok));
  PF("write_ok(sock=%d) ret=%d\n",sock,ret);
  if (ret != sizeof(ok)) {
    perror("write_ok error ");
    abort();
  }
}

class Sender {
public:
  std::vector<HostData> hosts_;
  std::vector<int> conn_socks_;
  std::vector<std::pair<int,int>> pipes_;
  std::map<int,res_count_t> count_down_;
  int success_count_;
  int follower_num_ = 0;
  int majority_num_;
  int epoll_fd_;
  int cnt_=0;
  int log_id_=0;
  int pipe_num_=0;
  bool quit_=false;
  struct epoll_event *events_;

  Sender() {
    epoll_fd_ = epoll_create1(0);
    events_ = (epoll_event *)calloc(MAXEVENTS, sizeof(epoll_event));
  }

  void read_server_data(std::string filename) {
    std::ifstream ifs(filename);
    std::string str, hostname, state;
    int id, req_port, pub_port;

    while(std::getline(ifs, str)) {
      std::istringstream iss(str);
      iss >> id >> hostname >> req_port >> pub_port >> state;
      hosts_.emplace_back(id, hostname, req_port, pub_port, state);
      //std::cout << "id=" << id << ", hostname=" << hostname << ", req_port=" << req_port << ", pub_port=" << pub_port << ", state=" << state <<", is_leader=" << hosts_.back().is_leader_ << std::endl;
    }
  }

  void set_socket_flag(int sock, int flag) {
    int flags = fcntl( sock, F_GETFL, 0 );
    if (flags == -1) {
      perror("fcntl F_GETFL error ");
      abort();
    }
    flags |= flag;
    if (fcntl( sock, F_SETFL, flags ) == -1) {
      perror("fcntl F_SETFL error ");
      abort();
    }
  }

  void add_to_epoll(int sock, int resp, int event_type) {
    struct epoll_event event;
    ep_data_ptr_t *d = (ep_data_ptr_t*)malloc(sizeof(ep_data_ptr_t));
    d->fd = sock;
    d->fd_res = resp;
    d->type = event_type;
    event.data.ptr = d;
    event.events = EPOLLIN;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, sock, &event) == -1 ){
      perror("epoll_ctl error ");
      abort();
    }
  }

  void add_pipe(std::pair<int,int> pipe) {
    pipes_.emplace_back(pipe);
    ++pipe_num_;
    set_socket_flag(pipe.first, O_NONBLOCK);
    add_to_epoll(pipe.first, pipe.second, PIPE_EVENT);
  }

  void connect_follower() {
    for (auto &h : hosts_) {
      int conn_sock = 0;
      if (!h.is_leader_) {
        conn_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (conn_sock < 0) {
          perror("socket creation error ");
          abort();
        }
      }
      conn_socks_.emplace_back(conn_sock);
    }
    std::vector<bool> is_connected(hosts_.size(), false);
    int delay = 1;
    while (true) {
      bool ready = true;
      for (size_t i=0; i < hosts_.size(); ++i) {
        auto &h = hosts_[i];
        int conn_sock = conn_socks_[i];
        if (!h.is_leader_ && !is_connected[i]) {
          unsigned short port = (short)h.req_port_;
          const char* ip_address = h.hostname_.c_str();
          struct sockaddr_in addr = {0,};

          std::cout << "id=" << h.id_ << ", ip_addr=" << h.hostname_ <<
            ", req_port=" << h.req_port_ << ", pub_port=" << h.pub_port_ <<
            ", is_leader=" << h.is_leader_ << std::endl;

          addr.sin_family = AF_INET;
          addr.sin_port = htons(port);
          addr.sin_addr.s_addr = inet_addr(ip_address);
          if (::connect(conn_sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
            ready = false;
            perror("connect error ");
            continue;
          }
          is_connected[i] = true;
          ++follower_num_;

          set_socket_flag(conn_sock, O_NONBLOCK);
          add_to_epoll(conn_sock, -1, SERVER_EVENT);
        }
      }
      if (ready) break;
      sleep(delay);
      if (delay < 16) delay *= 2;
    }
    majority_num_ = follower_num_/2;
  }

  void send_to_servers(int log_id, int fd, void *buffer, size_t size) {
    ae_req_t ae_req = {log_id, size};
    for (auto conn_sock : conn_socks_) {
      char *buf = (char*)buffer;
      ssize_t sz = size;
      if (conn_sock > 0) {
        int r;
        r = my_send(conn_sock, &ae_req, sizeof(ae_req_t));
        if (r != 0) {
          fprintf(stderr, "connection closed");
          abort();
        }
        r = my_send(conn_sock, buf, sz);
        if (r != 0) {
          fprintf(stderr, "connection closed");
          abort();
        }
      }
    }
  }

  void check_count(int id) {
    PF("check_count: log_id=%d count_down_.size=%zd\n", id, count_down_.size());
    decltype(count_down_)::iterator cd = count_down_.find(id);
    if (cd != count_down_.end()) {
      --cd->second.count;
      if (cd->second.count == majority_num_) {
        PF("check_count!: log_id=%d fd=%d count=%d\n", id, cd->second.fd, cd->second.count);
        write_ok(cd->second.fd);
        count_down_.erase(id);
      } else {
        PF("check_count+: log_id=%d fd=%d count=%d\n", id, cd->second.fd, cd->second.count);
      }
    } else {
      PF("check_count?: log_id=%d not found\n", id);
    }
  }

  void respond_server(struct epoll_event &event) {
    ep_data_ptr_t *d = (ep_data_ptr_t*)(event.data.ptr);
    ae_res_t ae_res;

    int r = my_recv(d->fd, &ae_res, sizeof(ae_res_t));
    if (r != 0) {
      fprintf(stderr, "connection closed");
      abort();
    }
    check_count(ae_res.id);
  }

  void respond_pipe(struct epoll_event &event) {
    pipe_data_t pipe_dat;
    ep_data_ptr_t *d = (ep_data_ptr_t*)(event.data.ptr);
    ssize_t size = ::read(d->fd, &pipe_dat, sizeof(pipe_dat));

    if ( size == -1 ) {
      if ( errno == EAGAIN ) {
        printf("respond_pipe: EAGAIN\n");
        return;
      } else {
        perror("recv error ");
        abort();
      }
    } else if ( size == 0 ) {
      printf("Closed? by %d.\n", d->fd);
      close(d->fd);
      close(d->fd_res);
      free(d);
      //quit_ = true;
      return;
    } else if ( size != sizeof(pipe_dat) ) {
      perror("read pipe_dat size error ");
      abort();
    }
    if (pipe_dat.ptr == NULL) {
      printf("Closed! by %d.\n", d->fd);
      write_ok(d->fd_res);
      close(d->fd);
      close(d->fd_res);
      free(d);
      quit_ = true;
      --pipe_num_;
      //return;
      exit(0);
    }
    ++log_id_;
    count_down_[log_id_] = {follower_num_, d->fd_res};
    PF("respond_pipe: log_id=%d fd=%d fd_res=%d size=%zd count_down_.size()=%zd\n", log_id_, d->fd, d->fd_res, size, count_down_.size());
    send_to_servers(log_id_, d->fd_res, pipe_dat.ptr, pipe_dat.size);
  }

  void handle_event() {
    int event_cnt=0;
    PF("Wait at epoll_wait #%d.\n", cnt_);
    fflush(stdout);
    event_cnt = epoll_wait(epoll_fd_, events_, MAXEVENTS, -1);
    PF("event_cnt=%d\n", event_cnt);

    for (int i=0; i<event_cnt; i++){
      struct epoll_event &e = events_[i];
      if ( !(e.events & EPOLLIN) ) {
        perror("epoll_wait error ");
        abort();
      } else {
        ep_data_ptr_t *d = (ep_data_ptr_t*)(e.data.ptr);
        switch(d->type) {
        case SERVER_EVENT:
          respond_server(e);
          break;
        case PIPE_EVENT:
          respond_pipe(e);
          break;
        }
      }
    }
    ++cnt_;
  }

  bool quit() {
    if (quit_) {
      PF("pipe_num_=%d count_down_.size()=%zd\n",pipe_num_, count_down_.size());
      fflush(stdout);
      for (auto &cd : count_down_) {
        write_ok(cd.second.fd);
      }
      count_down_.clear();
    }
    if (pipe_num_==0) {
      for (auto conn_sock : conn_socks_) {
        close(conn_sock);
      }
    }
    return pipe_num_==0;
  }
};

void worker(std::vector<std::pair<int,int>> pipes)
{
  Sender sender;
  sender.read_server_data("hosts.dat");
  sender.connect_follower();
  for (auto pipe : pipes) {
    sender.add_pipe(pipe);
  }
  for (auto pipe : pipes) {
    write_ok(pipe.second);
  }
  while(1) {
    sender.handle_event();
  }
}

int main(int argc, char** argv)
{
  int r;
  int pipe_out[2], pipe_in[2];
  r = pipe(pipe_out);
  //int r = socketpair(AF_UNIX, SOCK_STREAM, SOCK_STREAM, pair);
  if (r == -1) {
    perror("pipe create error ");
    abort();
  }
  r = pipe(pipe_in);
  if (r == -1) {
    perror("pipe create error ");
    abort();
  }
  std::vector<std::pair<int,int>> pipes{{pipe_out[0],pipe_in[1]}};
  std::thread th(worker, pipes);

  char *buffer = (char*)calloc(1,BUF_SZ);
  int i=0;
  pipe_data_t pipe_dat;
  ssize_t ret;
  read_ok(pipe_in[0]);
  for (i=0; i<10; ++i) {
    pipe_dat = {buffer, BUF_SZ};
    ret = write(pipe_out[1], &pipe_dat, sizeof(pipe_dat));
    printf("write %zd bytes to pipe\n", ret);
    if (ret != sizeof(pipe_dat)) {
      perror("write pipe error ");
      abort();
    }
    read_ok(pipe_in[0]);
    printf(" read_ok\n");
  }
  sleep(1);
  pipe_dat = {NULL, 0};
  ret = write(pipe_out[1], &pipe_dat, sizeof(pipe_dat));
  printf("write %zd bytes to pipe\n", ret);
  if (ret != sizeof(pipe_dat)) {
    perror("write pipe error ");
    abort();
  }
  th.join();
  return 0;
}
