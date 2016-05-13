// zoran@cs.ucsb.edu
// net.cc

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

int make_socket (unsigned short int port){
	int sock;
	struct sockaddr_in name;
	int val=1;

	/* Create the socket. */
	sock = socket (PF_INET, SOCK_STREAM, 0);
	if (sock < 0){
		perror ("socket");
		exit (EXIT_FAILURE);
	}

	setsockopt (sock,SOL_SOCKET,SO_REUSEADDR, &val, sizeof(int));

	/* Give the socket a name. */
	name.sin_family = AF_INET;
	name.sin_port = htons (port);
	name.sin_addr.s_addr = htonl (INADDR_ANY);
	if (bind (sock, (struct sockaddr *) &name, sizeof (name)) < 0){
		perror ("bind");
		exit (EXIT_FAILURE);
	}
	return sock;
}

void init_sockaddr (struct sockaddr_in *name,
               const char *hostname,
               unsigned short int port)
{
	struct hostent *hostinfo;
	name->sin_family = AF_INET;
	name->sin_port = htons (port);
	hostinfo = gethostbyname (hostname);
	if (hostinfo == NULL){
		fprintf (stderr, "Unknown host %s.\n", hostname);
		exit (EXIT_FAILURE);
	}
	name->sin_addr = *(struct in_addr *) hostinfo->h_addr;
}

int write_msg(int fd, char *msg, int n){
	int pnum;
	char *pmsg=msg;
	while(n>0){
		pnum=write(fd,pmsg,n);
		if(pnum<0){
			printf("write error\n");
			return -1;
		} else{
			n=n-pnum;
			pmsg+=pnum;
		}
	}
#ifdef DEBUG
	printf("write ends...\n");
#endif
}

int read_msg(int fd, char *msg, int n){
	int pnum;
	char *pmsg=msg;
	while(n>0){
		pnum=read(fd,pmsg,n);
		if(pnum<0){
			printf("read error\n");
			exit(-1);
		} else if(pnum==0){
			return -1;
		}else {
			n=n-pnum;
			pmsg+=pnum;
		}
		
	}
#ifdef DEBUG
	printf("read ends...\n");
#endif
}

