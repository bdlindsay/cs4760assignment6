#include "oss.h"

// Brett Lindsay 
// cs4760 assignment 6
// userProcess.c

pcb_t *pcb = NULL;
run_info_t *runInfo = NULL;
int pNum;

// prototypes
void intr_handler();

main (int argc, char *argv[]) {
	int shm_id;
	int r; // random
	int i; // index
	double rd; // random double
	pNum = atoi(argv[1]);
	signal(SIGINT,intr_handler);
	srand(time(NULL));

	// get pcb info
	shm_id = atoi(argv[2]);
	if ((pcb = (pcb_t*) shmat(shm_id,0,0)) == (void*) -1) {
		perror("shmat:pcb");
	}	
	
	// get runInfo
	shm_id = atoi(argv[3]);
	if ((runInfo = (run_info_t*) shmat(shm_id,0,0)) == (void*) -1) {
		perror("shmat:runInfo");
	}
	fprintf(stderr, "Process %d: Starting\n", pNum);

	pcb->pg_req.lAddr = 10;
	pcb->isWaiting = true;
	sem_wait(&pcb->sem);
	pcb->isWaiting = false;

	pcb->isCompleted = true;
	fprintf(stderr, "Process %d: release all and die\n", pNum);
	// detach from shared
	shmdt(runInfo);
	runInfo = NULL;

	shmdt(pcb);
	pcb = NULL;
}

void intr_handler() {
	signal(SIGINT, SIG_DFL); // change to default SIGINT behavior
	
	// detach from shared if not already
	if (pcb != NULL) {
		shmdt(pcb);
	}	
	if (runInfo != NULL) {
		shmdt(runInfo);
	}	

	fprintf(stderr,"Received SIGINT: Process %d cleaned up and dying.\n",pNum);

	raise(SIGINT);
}	
