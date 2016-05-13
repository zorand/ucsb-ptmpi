// mpi_global_data.h
// zoran@cs.ucsb.edu

/* 
 *  Zoran Dimitrijevic
 *  zoran@cs.ucsb.edu
 */
#ifdef _MPI_MAIN_CC

#define ELEM_TYPE double
#undef FLOAT_OP

#else
//Global data :

#define ELEM_TYPE double
#undef FLOAT_OP

int p;    /* number of procs*/
int me;   /* my id */
MPI_Status status;
int msg, tag;

ELEM_TYPE **a;
ELEM_TYPE **b;
ELEM_TYPE **c;
ELEM_TYPE *Buf;

int n;      /* matrix dim */
int nq;		/* sub-matrix dim */
int q;		/* number of submatrix */
int sub_size,b_size,bn_size;
int q_local;

#endif
