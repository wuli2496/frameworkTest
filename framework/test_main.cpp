#include <stdio.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <errno.h>
#include <assert.h>
#include <arpa/inet.h>
#include <libgen.h>

#include "processpool.h"
#include "cgi_conn.h"


int main(int argc, char**argv)
{
	
	if (argc <= 2)
	{
		printf("usage:%s ip_address port_number\n", basename(argv[0]));
		return -1;
	}

	const char* ip = argv[1];
	int port = atoi(argv[2]);

	int listenfd = socket(AF_INET, SOCK_STREAM, 0);
	assert(listenfd >= 0);
	
	int ret = -1;
	sockaddr_in ser_addr;
	memset(&ser_addr, 0, sizeof(ser_addr));
	ser_addr.sin_family = AF_INET;
	ser_addr.sin_port = htons(port);
	inet_pton(AF_INET, ip, &ser_addr.sin_addr);

	ret = bind(listenfd, (sockaddr*)&ser_addr, sizeof(ser_addr));
	assert(ret != -1);

	ret = listen(listenfd, 5);
	assert(ret != -1);

	#if 1
	ProcessPool<cgi_conn>* pool = ProcessPool<cgi_conn>::create(listenfd);
	
	if (pool)
	{
		pool->run();
		delete pool;
	}
	#endif

	close(listenfd);
	
	return 0;
}
