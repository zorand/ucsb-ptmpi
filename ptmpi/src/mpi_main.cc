// zoran@cs.ucsb.edu
// mpi_main.cc

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include "ptmpi.h"

#define _MPI_MAIN_CC

// MPI node main function
// all other MPI functions that use global MPI data 
// have to be defined inside class MPI_Node in ptmpi.h
// all global MPI vars also have to be inside class MPI_Node

int MPI_Node::mpi_main(int argc, char **argv){
	int i,j,k,ii,jj,kk;
	register ELEM_TYPE sum;
	double t1,t2;
	int flag=0;
	
	tag=0;

/* MPI init*/
	MPI_Init(&argc, &argv);
   	MPI_Comm_size(MPI_COMM_WORLD, &p);
	MPI_Comm_rank(MPI_COMM_WORLD, &me);

    if(argc<3){
    	if(me==0) printf("Usage: mpirun -np <number of procs> %s <n> <nq>\n",argv[0]); 
		MPI_Finalize();
		exit(-1);
	} else{
		n=atoi(argv[1]);
		nq=atoi(argv[2]);
		q=n/nq;
		if(n%nq){
			if(me==0) printf("n mod nq must be 0!\n");
			MPI_Finalize();
			exit(-2);
		}
	}

	sub_size=nq*nq;
	q_local=(q+p-1)/p;

/* data alloc: plz read readme.txt for detailed explanation */
	a=(ELEM_TYPE **) malloc( sizeof(ELEM_TYPE *) * q_local);
	b=(ELEM_TYPE **) malloc( sizeof(ELEM_TYPE *) * q_local);
	c=(ELEM_TYPE **) malloc( sizeof(ELEM_TYPE *) * q_local);

	if(!a || !b || !c){
		printf("node %d cannot malloc!\n",me);
		flag=1;
	}

	bn_size=q*sub_size;
	b_size=bn_size*sizeof(ELEM_TYPE);
	if(!me) printf("n=%d,nq=%d,q=%d,b_size=%d,bn_size=%d\n",n,nq,q,b_size,bn_size);

	for(i=0;i<q_local;i++){
		a[i]=(ELEM_TYPE *) calloc( bn_size,sizeof(ELEM_TYPE) );
		b[i]=(ELEM_TYPE *) calloc( bn_size,sizeof(ELEM_TYPE) );
		c[i]=(ELEM_TYPE *) calloc( bn_size,sizeof(ELEM_TYPE) );

		if(!a[i] || !b[i] || !c[i]){
			printf("node %d cannot malloc!\n",me);
			flag=1;
		}
	}
	Buf=(ELEM_TYPE *) calloc( bn_size,sizeof(ELEM_TYPE) );
	if(!Buf){
		printf("node %d cannot malloc!\n",me);
		flag=1;
	}

MPI_Barrier(MPI_COMM_WORLD);

/* data init:*/
	for(i=0;i<q;i++)
		if( (i%p)==me){
			for(j=0;j<q;j++){
				for(ii=0;ii<nq;ii++)
					for(jj=0;jj<nq;jj++){
						a[i/p][ j*sub_size+ii*nq+jj ] = j*nq+jj+1;
						b[i/p][ j*sub_size+ii*nq+jj ] = i*nq+ii+j*nq+jj+2;
						/* ! here i is actualy column and j row*/
					}
			}
		}

#ifdef DEBUG
/* for debug: print a and b: */
//	print_matrix(a,"A");
//	print_matrix(b,"tran(B)");
#endif

/* calculate */
	if(!me) printf("mul started...\n");
	MPI_Barrier(MPI_COMM_WORLD);
	t1=MPI_Wtime(); /* use node clock */

	for(j=0;j<q;j++){
		/*first one need to broadcast next B column */
		if((j%p)==me) memcpy(Buf,b[j/p],b_size);
#ifndef FLOAT_OP
		MPI_Bcast(Buf,bn_size,MPI_DOUBLE,j%p,MPI_COMM_WORLD);
#else
		MPI_Bcast(Buf,bn_size,MPI_FLOAT,j%p,MPI_COMM_WORLD);
#endif

		for(i=0;i<q_local;i++)
			for(k=0;k<q;k++){
				/* A*B submatrix */
				for(ii=0;ii<nq;ii++)
					for(jj=0;jj<nq;jj++){
						sum=0;
						for(kk=0;kk<nq;kk++){
							sum+=a[i][k*sub_size+ii*nq+kk]*Buf[k*sub_size+jj*nq+kk];
						}
						c[i][j*sub_size+ii*nq+jj]+=sum;
					}
			}
	}
	t2=MPI_Wtime();
	MPI_Barrier(MPI_COMM_WORLD);

	if(me==0) printf("CPU %d time spent is %e seconds.\n",me,t2-t1);

/* print result: */
//	if(n<11) print_matrix(c,"C");

	if(me==0) printf("last elem: %lf\n",(float)c[q_local-1][bn_size-1]);

	MPI_Finalize();
}
