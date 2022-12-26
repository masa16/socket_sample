#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <errno.h>
#include "my_sock.h"


int main(int argc, char** argv)
{
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    perror("socket error ");
    exit(0);
  }
  if (argc != 2 || argv[1]==0) {
    printf("Usage : \n $> %s [port number]\n", argv[0]);
    exit(0);
  }
  int port = atoi(argv[1]);
  struct sockaddr_in addr = {0,};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  const size_t addr_size = sizeof(addr);

  int opt = 1;
  // ポートが解放されない場合, SO_REUSEADDRを使う
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) == -1) {
    perror("setsockopt error ");
    close(sock);
    exit(0);
  }
  if (bind(sock, (struct sockaddr*)&addr, addr_size) == -1) {
    perror("bind error ");
    close(sock);
    exit(0);
  }
  printf("bind port=%d\n", port);

  // クライアントのコネクション待ち状態は最大10
  if (listen(sock, 10) == -1) {
    perror("listen error ");
    close(sock);
    exit(0);
  }
  printf("listen success!\n");

  int last_id = 0;
  int sock_client = 0;
  char *buffer = (char*)malloc(32*1024*1024);

 ACCEPT:
  // 接続が切れた場合, acceptからやり直す
  printf("last_id=%d\n",last_id);
  int old_sock_client = sock_client;
  struct sockaddr_in accept_addr = {0,};
  socklen_t accpet_addr_size = sizeof(accept_addr);
  sock_client = accept(sock, (struct sockaddr*)&accept_addr, &accpet_addr_size);
  printf("sock_client=%d\n", sock_client);
  if (sock_client==0 || sock_client<0) {
    perror("accept error ");
    exit(0);
  }
  printf("accept success!\n");
  if (old_sock_client > 0) {
    close(old_sock_client);
  }

  while (true) {
    ae_req_t ae_req;

    if (my_recv(sock_client, &ae_req, sizeof(ae_req_t))) goto ACCEPT;
    if (my_recv(sock_client, buffer, ae_req.size)) goto ACCEPT;

    ae_res_t ae_res;
    ae_res.id = last_id = ae_req.id;
    ae_res.status = 0;

    if (my_send(sock_client, &ae_res, sizeof(ae_res_t))) goto ACCEPT;
  }
  return 0;
}
