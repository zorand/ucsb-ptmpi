/*  Zoran Dimitrijevic
 *  zoran@cs.ucsb.edu
 *
 *  ptmpi.cc
 */

#define PTMPI
#define MAX_THREAD 1000

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
#include <sys/time.h>

#include "ptmpi.h"

// Struct for MPI node thread startup
typedef struct {
	int argc;
	char **argv;
	MPI *mpi;
	int local;
} Tinit;

// All threads in the process
pthread_t *threads;
pthread_t out_comm_start, in_comm_start;
pthread_t out_comm, in_comm;

// Pointer to the array of queues
// recv_queue[i] will be queue for i-th MPI node thread in this process
MPI_ReceiverQueues *recv_queue;

// Out comm queues and syncs
MPI_Queue *out_comm_queue;
pthread_mutex_t *out_queue_mutex;
pthread_mutex_t out_comm_mutex;
pthread_cond_t out_comm_cond;
int out_comm_request=0;
int one_more_chance=1;

// ------------------------------------
// MPI_Node Constructor & destructor
// ------------------------------------
MPI_Node::MPI_Node(int local_me, MPI *m){
	mpi=m;
	MPI_node_local_ID = local_me;
	MPI_node_ID=mpi->local2MPI(local_me);
	MPI_world_size=mpi->MPI_nodes_cnt;

#ifdef DEBUG2
	printf("Created node %d\n",MPI_node_ID);
#endif
}

MPI_Node::~MPI_Node(){
}

// ------------------------------------
// MPI functions:
// ------------------------------------
int MPI_Node::MPI_Comm_rank(MPI_Comm commrank, int *myrank){
	if(commrank!=MPI_COMM_WORLD){
		fprintf(stderr,"ERROR: invalid COMMUNICATOR RANK\n");
		*myrank=-1;
		return -1;
	} else {
		*myrank=MPI_node_ID;
		return 0;
	}
}

int MPI_Node::MPI_Init(int *argc, char ***argv){
	return 0;
}

int MPI_Node::MPI_Comm_size(MPI_Comm commrank, int *size){
	if(commrank!=MPI_COMM_WORLD){
		fprintf(stderr,"ERROR: invalid COMMUNICATOR RANK\n");
		*size=0;
		return -1;
	} else {
		*size=MPI_world_size;
		return 0;
	}
}

int MPI_Node::MPI_Finalize(){
	return 0;
}

// -----------------------------------------------------------------
// MPI_Send
// -----------------------------------------------------------------
int MPI_Node::
MPI_Send(void* message, int count, MPI_Datatype datatype,
		 int dest, int tag, MPI_Comm comm)
{
#ifdef DEBUG2
	printf("entering MPI_Send\n");
#endif
	MPI_QueueElem *elem;
	int dest_p=mpi->MPI2p(dest);
	
	if(mpi->me==dest_p){
		return recv_queue[mpi->MPI2local(dest)].
		   Send(1,message,count,datatype,MPI_node_ID,tag,comm,NULL);
	} else{
		elem=new MPI_QueueElem(NULL,NULL);
		elem->count=count;
		elem->datatype=datatype;
		elem->dest=dest;
		elem->tag=tag;
		elem->comm=comm;
		elem->buffer_count=count*MPI_datasize(datatype);;
		elem->message=malloc(elem->buffer_count);
		elem->free_msg=1;
		elem->source=MPI_node_ID;
		memcpy(elem->message,message,elem->buffer_count);
		elem->state=BUFFERED_SEND;
		pthread_mutex_lock(&out_queue_mutex[dest_p]);
		out_comm_queue[dest_p].add(elem);
		pthread_mutex_unlock(&out_queue_mutex[dest_p]);

		pthread_mutex_lock(&out_comm_mutex);
		if(out_comm_request==0){
			out_comm_request=1;
			pthread_cond_signal(&out_comm_cond);
		}
		pthread_mutex_unlock(&out_comm_mutex);
	}
#ifdef DEBUG2
	printf("Exiting MPI_Send\n");
#endif
}

// -----------------------------------------------------------------
// MPI_ISend
// -----------------------------------------------------------------
int MPI_Node::
MPI_ISend(void* message, int count, MPI_Datatype datatype,
		 int dest, int tag, MPI_Comm comm,MPI_WaitType *wait_on)
{
#ifdef DEBUG2
	printf("entering MPI_ISend\n");
#endif
	MPI_QueueElem *elem;
	int dest_p=mpi->MPI2p(dest);
	
	if(mpi->me==dest_p){
		return recv_queue[mpi->MPI2local(dest)].
		   Send(0,message,count,datatype,MPI_node_ID,tag,comm,wait_on);
	} else{
		elem=new MPI_QueueElem(NULL,NULL);
		elem->count=count;
		elem->datatype=datatype;
		elem->dest=dest;
		elem->tag=tag;
		elem->comm=comm;
		elem->message=message;
		elem->free_msg=0;
		elem->source=MPI_node_ID;
		elem->WaitInit(1);
		*wait_on=elem;
		elem->state=IMEDIATE_SEND;
		pthread_mutex_lock(&out_queue_mutex[dest_p]);
		out_comm_queue[dest_p].add(elem);
		pthread_mutex_unlock(&out_queue_mutex[dest_p]);

		pthread_mutex_lock(&out_comm_mutex);
		if(out_comm_request==0){
			out_comm_request=1;
			pthread_cond_signal(&out_comm_cond);
		}
		pthread_mutex_unlock(&out_comm_mutex);
	}
#ifdef DEBUG2
	printf("Exiting MPI_ISend\n");
#endif
}

// -----------------------------------------------------------------
// MPI_Recv
// -----------------------------------------------------------------
int MPI_Node::
MPI_Recv(void* message, int count, MPI_Datatype datatype, 
		 int source, int tag, MPI_Comm comm, MPI_Status* status)
{
#ifdef DEBUG2
	printf("Entering MPI_Recv\n");
#endif

	return recv_queue[MPI_node_local_ID].
		   Recv(1,message,count,datatype,source,tag,comm,status,NULL);
#ifdef DEBUG2
	printf("Exiting MPI_Recv\n");
#endif
}

// -----------------------------------------------------------------
// MPI_IRecv
// -----------------------------------------------------------------
int MPI_Node::
MPI_IRecv(void* message, int count, MPI_Datatype datatype,int source, 
		 int tag, MPI_Comm comm, MPI_Status* status,MPI_WaitType *wait_on)
{
#ifdef DEBUG2
	printf("Entering MPI_IRecv\n");
#endif

	return recv_queue[MPI_node_local_ID].
		   Recv(0,message,count,datatype,source,tag,comm,status,wait_on);
#ifdef DEBUG2
	printf("Exiting MPI_IRecv\n");
#endif
}

// -----------------------------------------------------------------
// MPI_Wait
// -----------------------------------------------------------------
int MPI_Node::MPI_Wait(MPI_WaitType wait_on){
	if(wait_on!=NULL) wait_on->Wait();
	return 0;
}

// -----------------------------------------------------------------
// MPI_Bcast
// -----------------------------------------------------------------
int MPI_Node::
MPI_Bcast(void* message, int count, MPI_Datatype datatype,
		 int root, MPI_Comm comm)
{
	MPI_WaitType wait_on_local[mpi->local_nodes_cnt];
	MPI_WaitType wait_on_comm[mpi->p];
	MPI_QueueElem *elem;
	int i,j;

#ifdef DBUG
	printf("I am %d and Starting: bcast from %d\n",MPI_node_ID, root);
#endif
	
	if(root==MPI_node_ID){
		for(i=0;i<mpi->p;i++) if(mpi->me==i){
			for(j=0;j<mpi->local_nodes_cnt;j++){
				if(j!=MPI_node_local_ID){
					recv_queue[j].Send(0,message,count,
						datatype,root,BCAST,comm,&wait_on_local[j]);
				}
			}
		} else{
			elem=new MPI_QueueElem(NULL,NULL);
			elem->count=count;
			elem->datatype=datatype;
			elem->dest=i;	// doesn't matter? here i is not MPI_node_ID but p
			elem->source=root;
			elem->tag=BCAST;
			elem->comm=comm;
			elem->message=message;
			elem->state=BROADCAST_NONLOCAL_SEND;
			wait_on_comm[i]=elem;
			elem->WaitInit(1);
			
			pthread_mutex_lock(&out_queue_mutex[i]);
			out_comm_queue[i].add(elem);
			pthread_mutex_unlock(&out_queue_mutex[i]);
	
			pthread_mutex_lock(&out_comm_mutex);
			if(out_comm_request==0){
				out_comm_request=1;
				pthread_cond_signal(&out_comm_cond);
			} else out_comm_request=1;
			pthread_mutex_unlock(&out_comm_mutex);
		}
	} else{
#ifdef DEBUG
	printf("%d trying to receive blocking from %d - recv_queue[%d]\n",MPI_node_ID,root,MPI_node_local_ID);
#endif
		return recv_queue[MPI_node_local_ID].
		Recv(1,message,count,datatype,root,BCAST,comm,NULL,NULL);
	}		
	// wait

	for(i=0;i<mpi->local_nodes_cnt;i++) if(i!=MPI_node_local_ID){
		MPI_Wait(wait_on_local[i]);
	}

	for(i=0;i<mpi->p;i++) if(i!=mpi->me){
		MPI_Wait(wait_on_comm[i]);
	}

#ifdef DEBUG
	printf("%d finished Broadcast!!!\n",root);
#endif
	return 0;
}

int MPI_Node::MPI_Barrier(MPI_Comm comm){
	char c;
//	MPI_Bcast((void *)&c,1,MPI_CHAR,0,comm);
}

double MPI_Node::MPI_Wtime(){
	struct timeval t;
	double time;
	gettimeofday(&t,NULL);
	time=t.tv_sec+ t.tv_usec/1000000.;
	return time;
}

// -------------------------------------------------
// MPI node startup
// -------------------------------------------------
void *mpi_startup(void *targ){
	Tinit *ta=(Tinit *) targ;
	MPI_Node *node;
	char **argv=(char **)malloc( (ta->argc) * sizeof(char *) );
	int i;
	
	argv[0]=(char *)malloc( (strlen(MPI_PRG_NAME)+1)*sizeof(char) );
	strcpy(argv[0],MPI_PRG_NAME);
	for(i=3;i<ta->argc;i++){
		argv[i-2]=(char *)malloc( (strlen(ta->argv[i])+1)*sizeof(char) );
		strcpy(argv[i-2],ta->argv[i]);
	}
	argv[i-2]=NULL;
	node=new MPI_Node(ta->local,ta->mpi);

#ifdef DEBUG1
	printf("Starting mpi_main for thread %d\n",ta->local);
#endif

	node->mpi_main(ta->argc-2, argv);
	delete node;

	for(i=0;i<ta->argc-1;i++) free(argv[i]);
	free(argv);
}

// -------------------------------------------------
// In Comm Thread Function
// -------------------------------------------------
void *in_comm_f(void *m){
	MPI *mpi=(MPI *)m;
	char *msgbuf[mpi->p];
	int msgcnt[mpi->p],hdrcnt[mpi->p],cnt;
	int msgsize[mpi->p];
	SocketMsgHdr msghdr[mpi->p];
	fd_set active_fd_set,read_fd_set;
	int synflag[mpi->p], endcnt=1;
	int i,j;

	FD_ZERO(&active_fd_set);
	for(i=0;i<mpi->p;i++)
		if(i!=mpi->me){
			msgcnt[i]=0;
			hdrcnt[i]=0;
			FD_SET(mpi->channel[i],&active_fd_set);
		}
		
	while(1){
		read_fd_set=active_fd_set;
		if (select (FD_SETSIZE, &read_fd_set, NULL, NULL, NULL) < 0){
#ifdef DEBUG
			perror("Select error in in_comm_f: assuming end will come soon");
#endif
			return NULL;
        }
		for(i=0;i<mpi->p;i++) if(i!=mpi->me){
			if (FD_ISSET (mpi->channel[i], &read_fd_set)){
				if(hdrcnt[i]==0) msgbuf[i]=(char *)&msghdr[i];
				if(hdrcnt[i]<sizeof(SocketMsgHdr)){
					cnt=read(mpi->channel[i],msgbuf[i],sizeof(SocketMsgHdr)-hdrcnt[i]);
					if(cnt<0){
						printf("Socket read error\n");
						exit(-1);
					}
					if(cnt==0){
						if(sizeof(SocketMsgHdr)-hdrcnt[i]){
							close(mpi->channel[i]);
							FD_CLR(mpi->channel[i],&active_fd_set);
							if(++endcnt==mpi->p) return NULL;
						}
						else printf("pokusavam da citam 0 bajtova\n");
						continue;
					}
					hdrcnt[i]+=cnt;
					msgbuf[i]+=cnt;
					if(hdrcnt[i]==sizeof(SocketMsgHdr)){
						if(msghdr[i].magic!=MAGIC_WORD){
							printf("Magic word read error\n");
							exit(-1);
						}
#ifdef DEBUG
	printf("Msg Header: MAGIC=%d; dest=%d; source=%d; tag=%d; size=%d\n",
		(int)msghdr[i].magic,msghdr[i].dest,msghdr[i].source,
		msghdr[i].tag,msghdr[i].count);
#endif
						msgcnt[i]=0;
						msgsize[i]=msghdr[i].count*MPI_datasize(msghdr[i].datatype);
					// buffered receive: proper way is to check first if
					// receive request is issued ! should be changed after
					// code is debugged
						msgbuf[i]=(char *)malloc(msgsize[i]);
					}
				}
				if(hdrcnt[i]==sizeof(SocketMsgHdr)){
					cnt=read(mpi->channel[i],msgbuf[i]+msgcnt[i],msgsize[i]-msgcnt[i]);
					if(cnt<0){
						printf("Socket read error\n");
						exit(-1);
					}
					if(cnt==0){
						printf("Read cnt=0\n");
					//	close(mpi->channel[i]);
						continue;
					}

					msgcnt[i]+=cnt;
					if(msgcnt[i]==msgsize[i]){
#ifdef DEBUG2
	printf("poruka velicine: %d\n",cnt);
	printf("saljem poruku:\n");
#endif
						hdrcnt[i]=0;
						if(msghdr[i].tag!=BCAST){
							recv_queue[mpi->MPI2local(msghdr[i].dest)].
							Send(1,msgbuf[i],msghdr[i].count,msghdr[i].datatype,
								msghdr[i].source,msghdr[i].tag,msghdr[i].comm,NULL);
						} else{
							//BROADCAST_SEND:
							MPI_QueueElem *temp=new MPI_QueueElem(NULL,NULL);
							temp->WaitInit(mpi->local_nodes_cnt-1);
							temp->source=msghdr[i].source;
							temp->tag=BCAST;
							temp->comm=msghdr[i].comm;
							temp->state=BROADCAST_WAIT;
							temp->message=msgbuf[i];
							temp->count=msghdr[i].count;
							temp->buffer_count=msgsize[i];
							temp->free_msg=1;
							for(j=0;j<mpi->local_nodes_cnt;j++){
								recv_queue[j].Send(3,msgbuf[i],msghdr[i].count,
								msghdr[i].datatype,msghdr[i].source,BCAST,
								msghdr[i].comm,&temp);
							}
						}
#ifdef DEBUG2						
	printf("poslao poruku\n");
#endif
					}
				}
			}
		}		
	}
}

// -------------------------------------------------
// Out Comm Thread Function
// -------------------------------------------------
void *out_comm_f(void *m){
	MPI *mpi=(MPI *)m;
	MPI_QueueElem *buf[mpi->p];
	char *msgbuf[mpi->p];
	int msgcnt[mpi->p],hdrcnt[mpi->p],msgsize[mpi->p],cnt;
	SocketMsgHdr msghdr[mpi->p];
	fd_set active_fd_set,write_fd_set;
	int endcnt=0, wait=1;
	int i;

// Create active_fd_set - all outgoing sockets
	FD_ZERO(&active_fd_set);
	for(i=0;i<mpi->p;i++)
		if(i!=mpi->me){
			buf[i]=NULL;
			msgcnt[i]=0;
			hdrcnt[i]=0;
			FD_SET(mpi->channel[i],&active_fd_set);
		}

// Waiting for any msg to send		
	while(1){
		if(wait){
			pthread_mutex_lock(&out_comm_mutex);
			while(out_comm_request==0){
#ifdef DEBUG
	printf("Blocking on out_comm_request!\n");
#endif
				pthread_cond_wait(&out_comm_cond,&out_comm_mutex);
			}
			if(out_comm_request==-1){
#ifdef DEBUG
	printf("Expecting end soon!\n");
#endif
				one_more_chance=0;
				for(i=0;i<mpi->p;i++) if(i!=mpi->me){
					pthread_mutex_lock(&out_queue_mutex[i]);
					if(out_comm_queue[i].first){
#ifdef DEBUG
	printf("onemorechance\n");
#endif
						one_more_chance=1;
					}
					pthread_mutex_unlock(&out_queue_mutex[i]);			
				}
				if(!one_more_chance){
					pthread_mutex_unlock(&out_comm_mutex);
#ifdef DEBUG
	printf("returns\n");
#endif
					return NULL;
				}
			}
			if(out_comm_request==1){
				out_comm_request=0;
			}
			pthread_mutex_unlock(&out_comm_mutex);
		}
		wait=1;
		write_fd_set=active_fd_set;
		if (select (FD_SETSIZE, NULL, &write_fd_set, NULL, NULL) < 0){
#ifdef DEBUG
				perror("Select error in out_comm_f: should kill out_comm - but continueing");
#endif
        }

		for(i=0;i<mpi->p;i++) if(i!=mpi->me){
if(out_comm_request==-1) printf("x");
			if(buf[i]==NULL){
				// try if there is something on the queue
				pthread_mutex_lock(&out_queue_mutex[i]);
				buf[i]=out_comm_queue[i].first;
				if(buf[i]!=NULL) out_comm_queue[i].remove(buf[i]);
				pthread_mutex_unlock(&out_queue_mutex[i]);
			}
			if ((buf[i]!=0) && FD_ISSET(mpi->channel[i],&write_fd_set)){
				wait=0;
				if(hdrcnt[i]==0){
					msghdr[i].magic=MAGIC_WORD;
					msghdr[i].comm=buf[i]->comm;
					msghdr[i].dest=buf[i]->dest;
					msghdr[i].source=buf[i]->source;
					msghdr[i].tag=buf[i]->tag;
					msghdr[i].datatype=buf[i]->datatype;
					msghdr[i].count=buf[i]->count;
					msgbuf[i]=(char *)&msghdr[i];
				}
				if(hdrcnt[i]<sizeof(SocketMsgHdr)){
					cnt=write(mpi->channel[i],msgbuf[i],sizeof(SocketMsgHdr)-hdrcnt[i]);
					if(cnt<0){
						printf("Socket write error\n");
						exit(-1);
					}
					if(cnt==0){
						if(sizeof(SocketMsgHdr)-hdrcnt[i]){
					//		close(mpi->channel[i]);
					//		FD_CLR(mpi->channel[i],&active_fd_set);
					//		if(++endcnt==mpi->p) return NULL;
						}
						else {
#ifdef DEBUG
							printf("pokusavam da pisem 0 bajtova\n");
#endif
						}
						continue;
					}
					hdrcnt[i]+=cnt;
					msgbuf[i]+=cnt;
					if(hdrcnt[i]==sizeof(SocketMsgHdr)){
						msgcnt[i]=0;
						msgsize[i]=msghdr[i].count*MPI_datasize(msghdr[i].datatype);
						msgbuf[i]=(char *)buf[i]->message;
					}
				}
				if(hdrcnt[i]==sizeof(SocketMsgHdr)){
					cnt=write(mpi->channel[i],msgbuf[i],msgsize[i]-msgcnt[i]);
					if(cnt<0){
						printf("Socket write error\n");
						exit(-1);
					}
					if(cnt==0){
						printf("Write cnt=0\n");
					//	close(mpi->channel[i]);
						continue;
					}

					msgcnt[i]+=cnt;
					msgbuf[i]+=cnt;
					if(msgcnt[i]==msgsize[i]){
						hdrcnt[i]=0;
						buf[i]->Signal();
					}
#ifdef DEBUG
	printf("Finished sending message to %d\n",i);
#endif DEBUG 
					buf[i]=NULL;
				}
			}
		}		
	}
}

// ------------------------------------------------------------------------
// Real Main Function
// First agrument is the local number for this process in "cluster" conf.
// ------------------------------------------------------------------------
main(int argc, char **argv){
	MPI *mpi;
	Tinit *parg;
	int concurrency=DEF_NUMBER_OF_PROC;
	int local_node_num;
	int MPI_node_num;
	int i; 

	if(argc<3){
		printf("Usage: ptmpi <me>\n%s contains other info\n",CONF_FILE);
		exit(-1);
	}

	FILE *fp=fopen(argv[2],"r");
	if(fp==NULL){
		for(i=0;i<argc;i++) printf("%s ",argv[i]);
		printf("\nConf fopen error!\n");
		exit(-1);
	}	
	mpi=new MPI(fp,argv[1]);
	local_node_num=mpi->local_nodes_cnt;

// Create recv buffers
	recv_queue=new MPI_ReceiverQueues[local_node_num] ();

// Create out_comm stuff
	out_comm_queue=new MPI_Queue[mpi->p];
	out_queue_mutex=new pthread_mutex_t[mpi->p];
	for(i=0;i<mpi->p;i++)
		pthread_mutex_init(&out_queue_mutex[i],NULL);
	pthread_mutex_init(&out_comm_mutex,NULL);
	pthread_cond_init(&out_comm_cond,NULL);
	
// Set thread concurrency
	pthread_setconcurrency(concurrency);

// Create communicator startup threads
	if(mpi->p > 1){
#ifdef DEBUG1
	printf("Creating in and out comm_start threads...\n");
#endif
		pthread_create(&in_comm_start,NULL,in_comm_startup,(void *)mpi);
		pthread_create(&out_comm_start,NULL,out_comm_startup,(void *)mpi);
	}

// Wait to init socket comms
	if(mpi->p >1){
		pthread_join(in_comm_start,NULL);
		pthread_join(out_comm_start,NULL);
	}

	printf("MPI channel sockets initialized\n");
	printf("MPI starts soon...\n");

// Create one thread for all incoming comm and one for outgoing
	if(mpi->p>1){
		pthread_create(&in_comm,NULL, in_comm_f,(void *)mpi);
		pthread_create(&out_comm,NULL,out_comm_f,(void *)mpi);
	}
	
// Create and startup MPI nodes
	threads=(pthread_t *) malloc( local_node_num*sizeof(pthread_t) );
	parg=(Tinit *) malloc( local_node_num*sizeof(Tinit) );

	for (i=0; i<local_node_num; i++){
		parg[i].local=i;
		parg[i].argc=argc;
		parg[i].argv=argv;
		parg[i].mpi=mpi;
#ifdef DEBUG1
	printf("Creating thread for MPI node %d\n",mpi->local2MPI(i) );
#endif
		pthread_create(&threads[i],NULL,mpi_startup,(void *)&parg[i]);
	}

// Synchronize the completion of threads
	for (i=0; i<local_node_num; i++){
		pthread_join(threads[i],NULL);
	}

printf("MPI ended...\n");

// Wait for out_comm to send all messages:
	pthread_mutex_lock(&out_comm_mutex);
	out_comm_request=-1;
	pthread_cond_signal(&out_comm_cond);
	pthread_mutex_unlock(&out_comm_mutex);

	pthread_join(out_comm,NULL);
#ifdef DEBUG
	printf("Out comm ended!\n");
#endif

// Close all channel sockets and wait for in communicator to finish
//	if(mpi->p>1){
//		for (i=0;i<mpi->p;i++) if(mpi->me!=i) close(mpi->channel[i]);
//		pthread_join(in_comm,NULL);
//	}


// Warning : Sockets are not closed yet, if the buffers are not empty!
// But they should be empty... 
}
