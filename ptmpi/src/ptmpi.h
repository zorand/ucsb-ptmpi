#ifndef __PTMPI_H_
#define __PTMPI_H_

#ifdef DEBUG
#define DEBUG1
#define DEBUG2
#define DEBUG3
#endif

// zoran@cs.ucsb.edu
// ptmpi.h

#define MIN(X,Y) (X)<(Y)?(X):(Y)
#define DEF_NUMBER_OF_PROC 4
#define MAGIC_WORD 42
#define TRY_CNT 30

#define MPI_PRG_NAME "mpi_prog"
#define CONF_FILE "/home/class_home/zoran/ptmpi/conf_ptmpi.rc"

// --------------------------------------
// Declaration of net.cc functions
// --------------------------------------
int make_socket (unsigned short int port);
void init_sockaddr (struct sockaddr_in *,const char *,unsigned short int);
int write_msg(int fd, char *msg, int n);
int read_msg(int fd, char *msg, int n);

// ----------------------------------------------------
// Declaration of Comm init functions in init_mpi.cc
// ----------------------------------------------------
void *in_comm_startup(void *arg);
void *out_comm_startup(void *arg);

// ------------------------------------
// MPI types:
// ------------------------------------
#define MPI_ANY_SOURCE (-1)
#define MPI_ANY_TAG (-1)
#define BCAST (-2)

enum MPI_Comm{
	MPI_COMM_WORLD=1
};

enum MPI_Datatype{
	MPI_CHAR=1,
	MPI_SHORT,
	MPI_INT,
	MPI_LONG,
	MPI_UNSIGNED_CHAR,
	MPI_UNSIGNED_SHORT,
	MPI_UNSIGNED,
	MPI_UNSIGNED_LONG,
	MPI_FLOAT,
	MPI_DOUBLE,
	MPI_LONG_DOUBLE,
	MPI_BYTE,
	MPI_PACKED
};

// Returns size of datatype in bytes - in comm_mpi.cc
int MPI_datasize(MPI_Datatype datatype);

struct MPI_Status{
	int source;
	int tag;
	int count;
	int overflow;
};

// ---------------------------
// Startup types:
// ---------------------------
class MPI{
	FILE *filep;
	int argc;
	char **argv;
  public:
	int me;
	int p;
	int *port;
	char * *pIP;
	int MPI_nodes_cnt;
	int local_nodes_cnt;
	int *local2global;
	int *global2local;
	int *global2p;
	int listensock;
	int *channel;

	pthread_mutex_t *out_mutex;
	pthread_cond_t *out_cond;
	
	MPI(FILE *fp, char *me_str);
	~MPI();

	int local2MPI(int local){ return local2global[local];}
	int MPI2local(int global){ return global2local[global];}
	int MPI2p(int global){ return global2p[global];}

	void halt(int er){
		printf("Usage:\n");
		printf("ptmpi <np> <pr_cnt> <me> {<IP> <port> <num>}\n");
		exit(er);
	}
};

// Message Header for each message transmitted over the channel
struct SocketMsgHdr{
	char magic;
	MPI_Comm comm;
	int dest;
	int source;
	int tag;
	MPI_Datatype datatype;
	int count;
	SocketMsgHdr(){magic=MAGIC_WORD;}	
};

// Small Message:
struct SocketSmallMsg{
	char magic;
	int index;
};

enum MPI_QueueElemStatus{
	EMPTY=0,
	COMPLETED,
	OVERFLOW,
	IN_USE,
	SEND_STARTED,
	BUFFERED_SEND,
	BLOCKED_SEND,
	IMEDIATE_SEND,
	BROADCAST_SEND,
	BROADCAST_WAIT,
	BROADCAST_NONLOCAL_SEND
};

// ---------------------------------------------------
// Thread Communicator Class:
// elem in the queue of buffered messages or requests
// ---------------------------------------------------
class MPI_QueueElem{
	pthread_mutex_t mutex;
	pthread_cond_t cond;
  public:
	MPI_QueueElemStatus state;
	int waiting;
	MPI_QueueElem *wait_on; /// Quick hack for broadcast!

	class MPI_QueueElem *next, *prev;
  //message info
	MPI_Comm comm;
	int source;
	int tag;
	int dest;
	int count;
	MPI_Datatype datatype;
	MPI_Status *status;
  //message data
  	int free_msg;		//if true free(message) on delete
	int buffer_count;
	void *message;

  public:
	MPI_QueueElem(MPI_QueueElem *p, MPI_QueueElem *n);
	~MPI_QueueElem();
	void WaitInit(int number);
	int Wait();
	void Signal();
	MPI_QueueElem *Next(){ return next;}
	MPI_QueueElem *Prev(){ return prev;}
};

// ---------------------------------
// MPI_Queue Class
// ---------------------------------
class MPI_Queue{
	MPI_QueueElem *first, *last;
  public:
	MPI_Queue(){ first=NULL; last=NULL; }
	~MPI_Queue();
	int add(MPI_QueueElem *elem);
	MPI_QueueElem *add();
	int remove(MPI_QueueElem *elem);
	inline int del(MPI_QueueElem *elem);
	inline int empty(){ if(first==NULL) return 1; else return 0; }

	friend void *out_comm_f(void *m);
	friend class MPI_ReceiverQueues;
};

// -----------------------------------------------------------------------
// One instance of this class exist per each MPI node thread
// Every thread receves msgs that are in the Send buffer or puts
// request on the Recv Requests Queue
// -----------------------------------------------------------------------
class MPI_ReceiverQueues{
	MPI_Queue *recv_buffer;
	MPI_Queue *recv_request;
	
	pthread_mutex_t use_mutex;
	pthread_cond_t recv_cond;
  public:
	MPI_ReceiverQueues();
	~MPI_ReceiverQueues();

	int Send(int buffered,void *message,int count,MPI_Datatype datatype,
			 int source,int tag,MPI_Comm comm,MPI_QueueElem **wait_on);
	int Recv(int blocking,void *message,int count,MPI_Datatype datatype,
			 int source,int tag,MPI_Comm comm,MPI_Status *status,
			 MPI_QueueElem **wait_on);
};

// -------------------------------------------------------------------------
// MPI Node Class:
// Each MPI node is just an thread which creates the instance of this class
// and runs mpi_main function
// -------------------------------------------------------------------------
class MPI_Node{
	typedef MPI_QueueElem * MPI_WaitType;
  private:
	MPI *mpi;
	int MPI_node_local_ID;
	int MPI_node_ID;
	int MPI_world_size;

	int MPI_Init(int *argc, char ***argv);
	int MPI_Comm_rank(MPI_Comm commrank, int *myrank);
	int MPI_Comm_size(MPI_Comm commrank, int *size);
	int MPI_Finalize();
	int MPI_Send(void* message, int count, MPI_Datatype datatype,
				 int dest, int tag, MPI_Comm comm);
	int MPI_ISend(void* message, int count, MPI_Datatype datatype,
				 int dest, int tag, MPI_Comm comm, MPI_WaitType *wait_on);
	int MPI_Recv(void* message, int count, MPI_Datatype datatype, 
				 int source, int tag, MPI_Comm comm, MPI_Status* status);
	int MPI_IRecv(void* message,int count,MPI_Datatype datatype,int source,
				 int tag,MPI_Comm comm,MPI_Status* status,MPI_WaitType *wait_on);
	int MPI_Wait(MPI_WaitType wait_on);

	int MPI_Bcast(void *message, int count, MPI_Datatype datatype,
				 int root, MPI_Comm comm);
	int MPI_Barrier(MPI_Comm comm);
	double MPI_Wtime();
	
  public:
	MPI_Node(int local_me, MPI *m);
	~MPI_Node();
	
// MPI global data
  private:
#include "mpi_global_data.h"

  public:

// MPI main function
	int mpi_main(int argc, char **argv);
};

#endif //__PTMPI_H_
