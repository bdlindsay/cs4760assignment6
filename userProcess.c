#include "oss.h"

// Brett Lindsay 
// cs4760 assignment 6
// userProcess.c

pcb_t *pcb = NULL;
run_info_t *runInfo = NULL;
int pNum;
int numRefs = 0;
// prototypes
void intr_handler();

main (int argc, char *argv[]) {
	int shm_id;
	int r; // random
	int i; // index
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

  while (1) {
		pcb->pg_ref.lAddr = random() % pcb->p_size;
		pcb->pg_ref.isWrite = random() % 2; // false or true
		pcb->pg_ref.isDone = false;
		// wait for oss to signal
		//pcb->isWaiting = true;
		double startWait = runInfo->lClock;
		//sem_wait(&pcb->sem);
		while (pcb->isWaiting == true) {};
		double endWait = runInfo->lClock;
		//pcb->isWaiting = false;
		pcb->pg_ref.isDone = true; // set in oss ??
		pcb->totalWaitTime += endWait - startWait;
		numRefs++;
		fprintf (stderr, "Process %d finished ref %d for %d\n", pNum, numRefs, pcb->pg_ref.lAddr);	
		//fflush(stderr);
		if (numRefs > 100) {
			/*int shouldQuit = random() % 2; // no/yes
			if (shouldQuit) {
				break;
			} else {
				numRefs = 0;
			}*/
			break;
		}
	}
	pcb->dTime = runInfo->lClock;
	pcb->totalSysTime = pcb->dTime - pcb->cTime;
	pcb->totalCpuTime = (double) (numRefs * .0010);
//	pcb->totalCpuTime = pcb->totalSysTime - pcb->totalWaitTime;
	pcb->isCompleted = true;

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
	pcb->totalCpuTime = (double) (numRefs * .0010);
	pcb->isCompleted = true;

	fprintf(stderr,"Received SIGINT: Process %d cleaned up and dying.\n",pNum);

	raise(SIGINT);
}	
