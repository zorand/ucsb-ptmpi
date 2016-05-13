// zoran@cs.ucsb.edu
// init_mpi.cc

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

#include "ptmpi.h"

// ----------------------------------------------------------
// MPI init info about the system configuration
// ----------------------------------------------------------
MPI::MPI(FILE *fp, char *me_str){
	int i,j,k,l;
	int num;
	char buf[501];

	filep=fp;
// Number of MPI nodes in the system:
	fscanf(fp,"%d",&MPI_nodes_cnt);
	
// Number of processes in the system:
	fscanf(fp,"%d",&p);

// What is my process number - me=0..p-1
	me=atoi(me_str); 

// Just some basic checking:
	if((p<1) ||(p>MPI_nodes_cnt) || (me>=p)|| (me<0)) halt(-1);

#ifdef DEBUG
	printf("MPI_nodes_cnt:%d; p=%d; me=%d\n",MPI_nodes_cnt,p,me);
#endif

	pIP=new char* [p];
	port=new int [p];
	channel=new int [p];

	global2local=new int[MPI_nodes_cnt];
	global2p=new int[MPI_nodes_cnt];

	for(i=0,k=0;i<p;i++){
		fscanf(fp,"%500s",buf);
		pIP[i]=(char *)malloc(strlen(buf));
		strcpy(pIP[i],buf);
		fscanf(fp,"%d",&port[i]);
		fscanf(fp,"%d",&num);
		
// feof before we have all info about system configuration
		if(feof(fp))halt(-1);

// Init local2global table
		if(i==me){
			local_nodes_cnt=num;
			local2global=new int[num];
			for(l=0;l<num;l++) local2global[l]=k+l;
		}
		
// Init global2[p|local] tables
		for(l=0;l<num;l++,k++){
			if(k<MPI_nodes_cnt){
				global2p[k]=i;
				global2local[k]=l;
			} else halt(-1);
		}
	}

// Some basic checking of the input
	if(k!=MPI_nodes_cnt){
		printf("Sum of local_nodes_cnts != MPI_nodes_cnt\n");
		halt(-2);
	}
#ifdef DEBUG
	printf("i : global2local: global2p\n");
	for(i=0;i<MPI_nodes_cnt;i++)
		printf("%3d: %3d: %3d\n", i, global2local[i],global2p[i]);
	printf("i: local2global\n");
	for(i=0;i<local_nodes_cnt;i++)
		printf("%3d: %3d\n", i, local2global[i]);
#endif
}

MPI::~MPI(){
// put here lot of stuff... now we exit so there is no problem...
}

// ----------------------------------------------
// Communicator threads startup
// ----------------------------------------------
void *in_comm_startup(void *arg){
	MPI *mpi=(MPI *) arg;
	int listensock,in_sock;
	fd_set active_fd_set, read_fd_set;
	struct sockaddr_in clientname;
	size_t size;
	int flags[mpi->p];
	struct hostent *hostinfo;
	int i;
	char msg[2];
	
	listensock=make_socket(mpi->port[mpi->me]);
	if (listen(listensock,1)<0){
		perror("Listenning problem: cannot establish connections\n");
		exit(-1);
	}
	FD_ZERO(&active_fd_set);
	FD_SET(listensock,&active_fd_set);
	int in_cnt=0;
	for(i=0;i<mpi->p;i++) flags[i]=0;
	while(in_cnt < (mpi->p - mpi->me - 1) ){
		read_fd_set=active_fd_set;
		if (select (FD_SETSIZE, &read_fd_set, NULL, NULL, NULL) < 0){
			perror ("select");
			exit(EXIT_FAILURE);
        }
		if (FD_ISSET (listensock, &read_fd_set)){
			size = sizeof (clientname);

			/* Connection request on listenning socket. */
			in_sock = accept(listensock,(struct sockaddr *)&clientname,&size);
			if (in_sock < 0) printf("Accept Error\n");

         	printf("Server: connect from host %s, port %d.\n",
						inet_ntoa(clientname.sin_addr),
						ntohs (clientname.sin_port) );
			if(read_msg(in_sock,msg,2)<0){
				close(in_sock);
				continue;
			}
			i=msg[1];
			if( (i<mpi->me) || (i>=mpi->p) || (msg[0]!=MAGIC_WORD)){
				printf("Magic read error\n");
				close(in_sock);
				continue;
			}
			if((flags[i]==0)){
				hostinfo = gethostbyname(mpi->pIP[i]);
				if(hostinfo==NULL){
					printf("Cannot gethostbyname(%s)\n",mpi->pIP[i]);
					exit(-1);
				}
#ifdef DEBUG
	printf("%s :",inet_ntoa(*(struct in_addr *)hostinfo->h_addr));
	printf("%s\n",inet_ntoa(clientname.sin_addr) );
	printf("%x:%x\n",clientname.sin_addr.s_addr,
			((struct in_addr *)hostinfo->h_addr)->s_addr );
#endif
				if( (*(struct in_addr *)hostinfo->h_addr).s_addr ==
					clientname.sin_addr.s_addr){
					printf("Connected %d to %d\n",mpi->me,i);
					flags[i]=1;
					in_cnt++;
					mpi->channel[i]=in_sock;
				} else close(in_sock);
			}
			if( in_cnt == (mpi->p - mpi->me - 1) ){
				close(listensock);
#ifdef DEBUG
				printf("InComm startup: Closing listening socket\n");
#endif
				FD_CLR(listensock,&active_fd_set);
			}
		}
	}
}

void *out_comm_startup(void *arg){
	MPI *mpi=(MPI *) arg;
	char msg[2];
	int sock;
	struct sockaddr_in servername;
	int i,flag;

	msg[0]=MAGIC_WORD;
	msg[1]=mpi->me;  // !!!!!! Support for up to 256 processes just!	
	for(i=0;i<mpi->me;i++){
		flag=0;
		mpi->channel[i]=socket(PF_INET, SOCK_STREAM, 0);

		if (mpi->channel[i] < 0){
			perror ("Cannot create socket");
			exit(EXIT_FAILURE);
		}
		init_sockaddr(&servername, mpi->pIP[i], mpi->port[i]);
		while(flag < TRY_CNT){
			if(connect(mpi->channel[i],(struct sockaddr *)&servername,
				sizeof(servername)) < 0){
				perror("Connect error...\n");
				flag++;
				sleep(1);
			}
			else{
				if(write_msg(mpi->channel[i],msg,2)<0){
					close(mpi->channel[i]);
					i--;
				}
				break;
			}
		}
	}
}
