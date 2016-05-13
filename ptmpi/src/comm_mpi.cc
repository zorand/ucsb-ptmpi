// zoran@cs.ucsb.edu
// comm_mpi.cc

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "ptmpi.h"

// returns size in bytes of MPI type datatype

int MPI_datasize(MPI_Datatype datatype){
	switch(datatype){
		case MPI_CHAR: return sizeof(char);
		break;
		case MPI_SHORT: return sizeof(short);
		break;
		case MPI_INT: return sizeof(int);
		break;
		case MPI_LONG: return sizeof(long);
		break;
		case MPI_UNSIGNED_CHAR: return sizeof(unsigned char);
		break;
		case MPI_UNSIGNED_SHORT: return sizeof(unsigned short);
		break;
		case MPI_UNSIGNED: return sizeof(unsigned int);
		break;
		case MPI_UNSIGNED_LONG: return sizeof(unsigned long);
		break;
		case MPI_FLOAT: return sizeof(float);
		break;
		case MPI_DOUBLE: return sizeof(double);
		break;
		case MPI_LONG_DOUBLE: return sizeof(long double);
		break;
		case MPI_BYTE: return sizeof(char);
		break;
		case MPI_PACKED:
			printf("MPI_PACKET unsupported!\n");
			return 1;
		break;
		default:
			printf("Unsupported Datatype!\n");
			return 1;
		break;
	}
}

// -------------------------------------------------------------------
// MPI_QueueElem Class
// Wait method will wait until Elem is COMPLETED and delete itself
// -------------------------------------------------------------------
MPI_QueueElem::MPI_QueueElem(MPI_QueueElem *p, MPI_QueueElem *n){
	prev=p;
	next=n;
	status=NULL;
	state=EMPTY;
	waiting=0;
	free_msg=0;
	pthread_mutex_init(&mutex,NULL);
	pthread_cond_init(&cond,NULL);
}

MPI_QueueElem::~MPI_QueueElem(){
	if(free_msg) free(message);
	pthread_mutex_destroy(&mutex);
	pthread_cond_destroy(&cond);
}

void MPI_QueueElem::WaitInit(int number){
// Initial design had pthread_mutex and cond init in this method:
// How expensive are pthread_inits?

	waiting=number;
}

int MPI_QueueElem::Wait(){
	int overflow;

	pthread_mutex_lock(&mutex);
	while(state!=COMPLETED)
		pthread_cond_wait(&cond, &mutex);

	if(status) overflow=status->overflow;
	else overflow=0;
	pthread_mutex_unlock(&mutex);

	if(waiting<=0) delete this;
	return overflow;
}

void MPI_QueueElem::Signal(){
	if(waiting){
		pthread_mutex_lock(&mutex);
		if(state!=BROADCAST_WAIT) state=COMPLETED;
		waiting--;
		pthread_cond_signal(&cond);
		pthread_mutex_unlock(&mutex);
	} else delete this;
}

// ------------------------------------------------
// MPI_Queue class used in system
// ------------------------------------------------
MPI_Queue::~MPI_Queue(){
	MPI_QueueElem *tmp, *cur=first;
	while(cur){
		tmp=cur->next;
		delete cur;
		cur=tmp;
	}
	first=last=NULL;
}

int MPI_Queue::add(MPI_QueueElem *elem){
	if(first==NULL){
		elem->prev=NULL;
		elem->next=NULL;
		first=last=elem;
		return 1;
	} else{
		elem->prev=last;
		elem->next=NULL;
		last->next=elem;
		last=elem;
		return 0;
	}	
}

MPI_QueueElem *MPI_Queue::add(){
	MPI_QueueElem *elem;
	if(first==NULL){
		elem=new MPI_QueueElem(NULL,NULL);
		first=last=elem;
	} else{
		elem=new MPI_QueueElem(last,NULL);
		last->next=elem;
		last=elem;
	}
	return elem;
}	

int MPI_Queue::remove(MPI_QueueElem *elem){
	if(elem->prev==NULL) first=elem->next;
	else elem->prev->next=elem->next;

	if(elem->next==NULL) last=elem->prev;
	else elem->next->prev=elem->prev;

// Note that elem is not deleted !!!	
	return 0;
}

inline int MPI_Queue::del(MPI_QueueElem *elem){
	int ret=remove(elem);
	delete elem;
	return ret;
}

// ------------------------------------------------
// MPI_ReceiverQueues Class: one per each MPI node
// Recv Request queue and Receiver Buffer queue
// ------------------------------------------------
MPI_ReceiverQueues::MPI_ReceiverQueues(){
	recv_buffer=new MPI_Queue();
	recv_request=new MPI_Queue();
	pthread_mutex_init(&use_mutex,NULL);
	pthread_cond_init(&recv_cond,NULL);
}

MPI_ReceiverQueues::~MPI_ReceiverQueues(){
	MPI_QueueElem *cur;

	delete recv_buffer;
	delete recv_request;
	pthread_mutex_destroy(&use_mutex);
	pthread_cond_destroy(&recv_cond);
}

// -----------------------------------------------------------
// This method is called by local MPI node thread or by 
// In Communicator thread 
// Can be called by more than one thread
// -----------------------------------------------------------
int MPI_ReceiverQueues::
Send(int buffered, void *message, int count, MPI_Datatype datatype, 
	 int source, int tag, MPI_Comm comm, MPI_QueueElem * *wait_on)
{
	MPI_QueueElem *cur;
	int msg_size,recv_bufsize;

	msg_size=count*MPI_datasize(datatype);

	pthread_mutex_lock(&use_mutex);
	cur=recv_request->first;
	while(cur){
		if( (cur->state==EMPTY) &&
			(cur->source==source || cur->source==MPI_ANY_SOURCE) && 
			(cur->tag==tag || cur->tag==MPI_ANY_TAG) && cur->comm==comm)
		{
			if(tag==BCAST && cur->tag==MPI_ANY_TAG){
				printf("MPI_ANY_TAG matched BCAST ! \n");
			}
			cur->state=SEND_STARTED;
			recv_request->remove(cur);
			pthread_mutex_unlock(&use_mutex);

			recv_bufsize=cur->buffer_count * MPI_datasize(cur->datatype);
			memcpy(cur->message,message,MIN(msg_size,recv_bufsize) );
			if(cur->status){
				cur->status->tag=tag;
				cur->status->source=source;
				cur->status->count=MIN(msg_size,recv_bufsize);
				if(recv_bufsize<msg_size) cur->status->overflow=-1;
				else cur->status->overflow=0;
			}
			cur->Signal();
			if(buffered==0) *wait_on=NULL;
			else if(buffered==3){
				(*wait_on)->Signal();
			}
			return 0;
		} else{
			cur=cur->next;
		}
	}
// recv did not yet call Recv... copy msg in buffer (this is non-blocking)
	switch(buffered){
		case 0: // IMEDIATE SEND
			cur=recv_buffer->add();
			cur->source=source;
			cur->tag=tag;
			cur->comm=comm;

			// Imediate send - no buffer - sender will call wait later
			cur->state=IMEDIATE_SEND;
			cur->free_msg=0;
			cur->buffer_count=msg_size;
			cur->message=message;
			cur->datatype=datatype;
			cur->WaitInit(1);
			pthread_mutex_unlock(&use_mutex);
			*wait_on=cur;
			return 0;
		break;
		case 1: // BUFFERED SEND
			cur=recv_buffer->add();
			cur->source=source;
			cur->tag=tag;
			cur->comm=comm;
			cur->state=IN_USE;
			// init wait for this also...
			pthread_mutex_unlock(&use_mutex);
			//Buffered Send
			cur->buffer_count=msg_size;
			cur->datatype=datatype;
			cur->message=malloc(msg_size);
			cur->free_msg=1;
			memcpy(cur->message,message,msg_size);

			pthread_mutex_lock(&use_mutex);
			cur->state=BUFFERED_SEND;
			pthread_cond_signal(&recv_cond); // if recv was blocked on IN_USE elem-change this to cur
			pthread_mutex_unlock(&use_mutex);
			return 0;
		break;
		case 3: // BROADCAST
			cur=recv_buffer->add();
			cur->source=source;
			cur->tag=tag;
			cur->comm=comm;

			cur->state=BROADCAST_SEND;
			cur->free_msg=0;
			cur->buffer_count=msg_size;
			cur->message=message;
			cur->datatype=datatype;
			cur->wait_on=*wait_on;
			cur->WaitInit(1);
			pthread_mutex_unlock(&use_mutex);
			return 0;
		break;
		default:
			printf("Illegal ReceiverQueues Send option!\n");
			exit(-1);
		break;
	}
}

// -------------------------------------------------------------------
// This method is called only by thread associated with this queue 
// It cannot be called by more than one threaüd
// -------------------------------------------------------------------
int MPI_ReceiverQueues::
Recv(int blocking,void *message,int count,MPI_Datatype datatype,int source,
	 int tag,MPI_Comm comm,MPI_Status *status,MPI_QueueElem **wait_on)
{
	MPI_QueueElem *cur;
	int msg_size,overflow;

	msg_size=count*MPI_datasize(datatype);
	pthread_mutex_lock(&use_mutex);
	cur=recv_buffer->first;
	while(cur){
		if( (cur->state==BUFFERED_SEND || cur->state==BROADCAST_SEND ||
			 cur->state==IMEDIATE_SEND || cur->state==IN_USE) && 
			(cur->source==source || source==MPI_ANY_SOURCE) && 
			(cur->tag==tag || tag==MPI_ANY_TAG) && cur->comm==comm)
		{
//If buffer is being copied, wait to finish - this can be improved
			while(cur->state==IN_USE){
#ifdef DEBUG
	printf("Wait on IN_USE elem!\n");
#endif
				pthread_cond_wait(&recv_cond, &use_mutex);
			}
			recv_buffer->remove(cur);
			pthread_mutex_unlock(&use_mutex);

			memcpy(message,cur->message,MIN(msg_size,cur->buffer_count) );
			if(msg_size<cur->buffer_count) overflow=-1;
			else overflow=0;
			if(status){
				status->tag=cur->tag;
				status->source=cur->source;
				status->overflow=overflow;
			}
//			if(cur->state==BUFFERED_SEND){
//				// It is better to free ASAP - this can be removed
//				cur->free_msg=0;
//				free(cur->message);
//			}
			cur->Signal();
			if(cur->state==BROADCAST_SEND)
				cur->wait_on->Signal();
			return overflow;
		} else{
			cur=cur->next;
		}
	}

// sender did not send msg yet
	cur=recv_request->add();
	cur->state=EMPTY;
	cur->message=message;
	cur->buffer_count=count;
	cur->comm=comm;
	cur->tag=tag;
	cur->source=source;
	cur->datatype=datatype;
	cur->status=status;
	cur->WaitInit(1);
	pthread_mutex_unlock(&use_mutex);
	if(blocking){
		return cur->Wait();
	} else{
		*wait_on=cur;
		return 0;
	}
}

