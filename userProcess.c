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
	int err;
	double rd; // random double
	pNum = atoi(argv[1]);
	signal(SIGINT,intr_handler);
	
	struct timeval tm;
	gettimeofday(&tm, NULL);
	srandom(tm.tv_sec + tm.tv_usec * 1000000ul);

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

  while (!pcb->isCompleted) {
		//fprintf (stderr, "Process %d: next memref\n", pNum);
		pcb->pg_ref.lAddr = random() % pcb->p_size;
		pcb->pg_ref.isWrite = random() % 2; // false or true
		pcb->pg_ref.hasNewReq = true;

		// wait for oss to signal
		double startWait = runInfo->lClock;
		err = sem_wait(&pcb->sem);
		if (err == -1) {
			fprintf (stderr, "sem_wait error occured\n");
		}
		double endWait = runInfo->lClock;
		pcb->numRefs++;

		//pcb->pg_ref.hasNewReq = false;
		pcb->totalWaitTime += endWait - startWait + .0005; // overhead from oss

		fprintf (stderr, "Process %d finished ref for page %d\n", pNum, pcb->pg_ref.lAddr);	
		fflush(stderr);
		// Should I end?
		if (pcb->numRefs > 50) {
			/*int shouldQuit = random() % 2; // no/yes
			if (shouldQuit) {
				break;
			} else {
				numRefs = 0;
			}*/
			pcb->isCompleted = true;
		}
	} // end isCompleted while
	pcb->dTime = runInfo->lClock;
	pcb->totalSysTime = pcb->dTime - pcb->cTime;
	pcb->totalCpuTime = (double) (pcb->numRefs * .0010);

	fprintf(stderr, "Process %d: finished\n", pNum);

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

	pcb->dTime = runInfo->lClock;
	pcb->totalSysTime = pcb->dTime - pcb->cTime;
	pcb->totalCpuTime = (double) (pcb->numRefs * .0010);
	pcb->isCompleted = true;

	fprintf(stderr,"Received SIGINT: Process %d cleaned up and dying.\n",pNum);

	raise(SIGINT);
}	
