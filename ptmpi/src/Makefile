# zoran@cs.ucsb.edu
# Makefile for cs240b

# Makefile for cluster

CC = g++
LIBS = -lpthread -lnsl
#FLAGS = -pg -g
FLAGS = -O -g
EXECS = ptmpi runptmpi

#DEFINE = DEBUG
DEFINE = NODEBUG

all: $(EXECS)
sve: ptmpi.o comm_mpi.o init_mpi.o mpi_main.o net.o all

ptmpi:	ptmpi.o comm_mpi.o mpi_main.o net.o init_mpi.o
	$(CC) -o ptmpi $(LIBS) ptmpi.o comm_mpi.o init_mpi.o mpi_main.o net.o 

ptmpi.o:	ptmpi.cc ptmpi.h
	$(CC) -c $(FLAGS) -D$(DEFINE) ptmpi.cc

comm_mpi.o:	comm_mpi.cc ptmpi.h
	$(CC) -c $(FLAGS) -D$(DEFINE) comm_mpi.cc

init_mpi.o:	init_mpi.cc ptmpi.h
	$(CC) -c $(FLAGS) -D$(DEFINE) init_mpi.cc

mpi_main.o:	mpi_main.cc ptmpi.h
	$(CC) -c $(FLAGS) -D$(DEFINE) mpi_main.cc

net.o:	net.cc
	$(CC) -c $(FLAGS) -D$(DEFINE) net.cc

runptmpi: runptmpi.c
	$(CC) -o runptmpi runptmpi.c

clean:
	rm $(EXECS) *.o

print:
	lpr Makefile ptmpi.h net.cc init_mpi.cc comm_mpi.cc ptmpi.cc mpi_main.cc
