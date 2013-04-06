#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "sender.h"

struct Ctx
{
	int sock;
	sockaddr_in target;
};
typedef struct Ctx Ctx;

void *sender_open (const char *ip, int port)
{
	Ctx *ctx = new Ctx;
	ctx->sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (ctx->sock == -1) {
		fprintf(stderr, "%s: create sock err\n", __func__);
		exit(-1);
	}

	ctx->target.sin_family = AF_INET;
	//将一个无符号短整型数值转换为网络字节序，即大端模式(big-endian)
	ctx->target.sin_port = htons(port);
	//将一个点分十进制的IP转换成一个长整数型数（u_long类型）
	ctx->target.sin_addr.s_addr = inet_addr(ip);

	return ctx;
}

void sender_close (void *snd)
{
	Ctx *c = (Ctx*)snd;
	close(c->sock);
	delete c;
}

int sender_send (void *snd, const void *data, int len)
{
	assert(len < 65536);
	Ctx *c = (Ctx*)snd;
	return sendto(c->sock, data, len, 0, (sockaddr*)&c->target, sizeof(sockaddr_in));
}
