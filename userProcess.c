#include "oss.h"

// Brett Lindsay 
// cs4760 assignment 5
// userProcess.c

pcb_t *pcb = NULL;
run_info_t *runInfo = NULL;
int pNum;

// prototypes
void addToClock(double);
void intr_handler();
void error_h();
int calcTotalRes();

main (int argc, char *argv[]) {
	int shm_id;
	int r; // random
	int i; // index
	double rd; // random double
	pNum = atoi(argv[1]);
	signal(SIGINT,intr_handler);
	signal(SIGFPE,error_h);
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

		fprintf(stderr, "Process %d: release all and die\n", pNum);

	// detach from shared
	shmdt(runInfo);
	runInfo = NULL;

	shmdt(pcb);
	pcb = NULL;
}

void addToClock(double d) {
	// wait for chance to change clock
	sem_wait(&runInfo->sem);
	runInfo->lClock += d;
	fprintf(stderr, "userProcess%d: lClock + %.03f : %.03f\n", pNum, d, runInfo->lClock);
	// signal others may update clock
	sem_post(&runInfo->sem);
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

void error_h() {
	signal(SIGFPE,error_h);
	fprintf(stderr,"Error out of my control occurred gah!\n");
	pcb->isCompleted = true;
	raise(SIGINT);

}
