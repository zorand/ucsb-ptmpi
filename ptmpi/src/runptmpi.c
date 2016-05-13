#include <stdio.h>
#include <unistd.h>

#define PTMPI_DIR "/home/class_home/zoran/ptmpi/"
#define LOCAL_SCRIPT "local_start"

main(int argc, char **argv){
	FILE *conf;
	int er;
	int cnt,p;
	int port,threads;
	char node[256];
	char nodestr[2048];
	char localscript[2048];
	char conffilename[1024];
	int i,j;
	int child;
	char *ptmpi_argv[100];

	if(argc<3){
		printf("Usage: runptmpi <conf_file> args\n");
		exit(-1);
	}
	
	conf=fopen(argv[1],"r");
	if(conf==NULL){
		printf("Error opening conf file...\n");
		exit(-2);
	}

	sprintf(localscript,"%s%s",PTMPI_DIR,LOCAL_SCRIPT);
	sprintf(conffilename,"%s%s",PTMPI_DIR,argv[1]);
	
	fscanf(conf,"%d%d", &cnt, &p);
	
	for(i=0;i<p;i++){
		fscanf(conf,"%255s",node);
		fscanf(conf,"%d",&port);
		fscanf(conf,"%d",&threads);

		sprintf(nodestr,"%d",i);
		
		child=fork();
		if(!child){
			ptmpi_argv[0]="ssh";
			ptmpi_argv[1]=node;
			ptmpi_argv[2]=localscript;
			ptmpi_argv[3]=nodestr;
			ptmpi_argv[4]=conffilename;
			for(j=2;j<=argc;j++) ptmpi_argv[j+3]=argv[j];
			ptmpi_argv[j+3]=NULL;
			j=0;
/*			while(ptmpi_argv[j]!=NULL){
				printf("%s\n",ptmpi_argv[j]);
				j++;
			}
*/
			er=execvp(ptmpi_argv[0], ptmpi_argv);
			printf("Return code from execvp is %d\n",er);
			exit(0);
		} else{
			printf("Started process %d\n",i);
			sleep(2);
		}
	}
	printf("started jobs...\n");
}
