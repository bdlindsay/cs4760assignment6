#include "oss.h"

// Brett Lindsay
// cs4760 assignment6
// oss.c

#define MAX_PROCESS_COUNT 18
#define MAX_USER_PROCS 12

const double NO_FAULT = .0010;
const double FAULT = .0015;

char *arg1; // to send process_id num to process
char *arg2; // to send pcb shm_id num to process 
char *arg3; // to send runInfo shm_id num to process 
pcb_t *pcbs[MAX_PROCESS_COUNT] = { NULL };
run_info_t *runInfo = NULL;
sim_stats_t stats; 
int sys_mem[8]; // bit array for 256K memory
int freePages = 256; // dynamically keep track of free frame #
int device_q[19] = { -1 }; // gauranteed 1 free slot
int backOfQ = 0;
int frontOfQ = 0;
double nextQPop = -1.00;
// signal handler prototypes
void free_mem();

main (int argc, char *argv[]) {
	char *arg0 = "userProcess";
	arg1 = malloc(sizeof(int)); // process num in pcbs
	arg2 = malloc(sizeof(int)); // shm_id to pcb
	arg3 = malloc(sizeof(int)); // shm_id to runInfo
	int i, shm_id, q, n;
	double r; // for random "milli seconds"
	int usedPcbs[1] = { 0 }; // bit vector 0-17 needed for 19 PCBs
	bool isPcbsFull = false;
	int next, nextCreate = 0; // points to next available PID
	int res_pid;
	srand (time(NULL));
	signal(SIGINT, free_mem);

	// init sim_stats_t for averages
	stats.tPut = 0;
	stats.turnA = 0.000;
	stats.waitT = 0.000;
	stats.totalCpuTime = 0.000;
	stats.cpuU = 0.000;

	// create shared runInfo
	if((shm_id = shmget(IPC_PRIVATE,sizeof(run_info_t*),IPC_CREAT | 0755)) == -1){
		perror("shmget:runinfo");
	}	
	runInfo = (run_info_t*) shmat(shm_id,0,0);
	fprintf(stderr, "shm: %d\n",shm_id);
	runInfo->shm_id = shm_id;
	initRunInfo(shm_id);

	while(1) { // infinite loop until alarm finishes
		if (runInfo->lClock > 60) {
			fprintf(stderr,"Timeout duration reached\n");
			raise(SIGINT);
		}
		if (nextCreate < runInfo->lClock) {
			isPcbsFull = true;
			for (i = 0; i < MAX_PROCESS_COUNT; i++) {
				if (testBit(usedPcbs,i) == 0) { 
					next = i;	
					isPcbsFull = false;	// need to set false when process killed
					break;
				}	
			}
			if (isPcbsFull) {
				//fprintf(stderr, "pcbs array was full trying again...\n");
			} else { // create a new process
				// create and assign pcb to the OS's array
				// init PCB
				fprintf(stderr, "Creating new process at pcb[%d]\n",next);
				pcbs[next] = initPcb(next);
				setBit(usedPcbs,next);

				// args for userProcess
				sprintf(arg1, "%d", next); // process num in pcbs
				sprintf(arg2, "%d", pcbs[next]->shm_id); // pcb for userProcess
				sprintf(arg3, "%d", runInfo->shm_id); // runInfo for userProcess

				if ((pcbs[next]->pid = fork()) == -1) {
					perror("fork");
					// reset init values if fork fails
					removePcb(pcbs,next);
					clearBit(usedPcbs,next);
				}
				if (pcbs[next] != NULL && pcbs[next]->pid == 0) { // child process 
					execl("userProcess", arg0, arg1, arg2, arg3, 0);
				} 
			}
			if (pcbs[next] != NULL) { // don't change nextCreate if fork failed
				// next process creation time
				r = (double)((rand() % 500) + 1) / 1000; // 1-500 milli
				nextCreate = runInfo->lClock + r;
			}
		} // end create process if block

		// read each processes current request	
		monitorMemRefs();
		// signal front of queue if time has passed
		updateQueue();

		// check if all processes are waiting on device
		deadlock();

		// check for finished processes
		updatePcbs(usedPcbs);

		// check if free frames low
		if (freePages < 26) {
			get_page();
		}
	} // end infinite while	

	// cleanup after normal execution - never reached in current implementation
	cleanUp(); // clean up with free() cleanUpPcbs()
}

void monitorMemRefs() {
	// See what each process needs in memory next
	int i;
	for (i = 0; i < MAX_PROCESS_COUNT; i++) {
		if (pcbs[i] == NULL || pcbs[i]->isCompleted) {
			continue;
		}
		pcb_t *pcb = pcbs[i];
		// if valid, let the process continue
		if (!pcb->pg_req.isDone && pcb->page_table[pcb->pg_req.lAddr].isValid) { 
			fprintf (stderr, "Process %d pg_ref in memory...continuing.\n", i);
			// if page in memory, let it continue
			updateClock(NO_FAULT);
			pcb->pg_req.isHit = true;
			sem_post (&pcbs[i]->sem);
		} else {
			fprintf (stderr, "Putting process %d on device queue.\n", i);
			// if page not in memory, put it on the queue for device
			device_q[backOfQ++] = i;
			pcb->pg_req.isHit = false;
			// process i waits on device
			if (backOfQ == 19) { // don't let queue overflow
				backOfQ = 0;
			}
		}
	}
}

void updateQueue() {
	int i, nextFree;
	if (frontOfQ == backOfQ) {
		// empty queue
		return;
	}
	// if no next queue time and queue not empty, set next queue pop
	if (nextQPop == -1 && frontOfQ != backOfQ) {
		nextQPop = runInfo->lClock + FAULT;
	} 
	// release a process from device queue if its time
	if (runInfo->lClock > nextQPop && nextQPop != -1) {
		// find nextFree spot in sys_mem
		for (i = 0; i < 256; i++) { // TODO switch to dynamic method
			if ((nextFree = testBit(sys_mem, i)) == 0) {
				break;
			}
			if (i == 256) {
				fprintf (stderr, "sys_mem maxed out somehow\n");
			}
		}
		setBit(sys_mem, nextFree);
		freePages--; // dynamically keep track of free frames
		pcb_t *pcb = pcbs[device_q[frontOfQ]];
		pcb->page_table[pcb->pg_req.lAddr].pAddr = nextFree;
		pcb->page_table[pcb->pg_req.lAddr].isValid = true;
		sem_post(&pcb->sem);
		if (++frontOfQ != backOfQ) {
			nextQPop = runInfo->lClock + FAULT;
		} else {
			nextQPop = -1; // no one on queue
		}
	}
}

void get_page() {
	// free the oldest 5% of pages from memory
	fprintf (stderr, "get_page() called\n");
}

void deadlock() {
	// if all processes are queued for device (i.e. waiting on their semaphore)
	// advance logical clock to fulfill the request at the head
	int i;
	bool allWaiting = true;
	for (i = 0; i < 18; i++) {
		if (pcbs[i] == NULL) {
			continue;
		}
		if (!pcbs[i]->isWaiting) {
			allWaiting = false;
		}
	}
	if (allWaiting) {
		// queue full advance the clock to let a process go
		updateClock(FAULT);
	}
}

// check for finished processes
void updatePcbs(int usedPcbs[]) {
	int i;

	for (i = 0; i < MAX_PROCESS_COUNT; i++) { // for each pcb 
		// continue if no pcb
		if (pcbs[i] == NULL) {
			continue;
		}	
		// if there is a completed process
		if (pcbs[i]->isCompleted) {
			// collect data on userProcess
			stats.tPut++;
			stats.turnA += pcbs[i]->totalSysTime;
			stats.waitT += pcbs[i]->totalSysTime - pcbs[i]->totalCpuTime;
			stats.totalCpuTime += pcbs[i]->totalCpuTime; 

			// remove pcb
			fprintf(stderr,"oss: Removing finished pcb[%d]\n",i);
			removePcb(pcbs, i);
			clearBit(usedPcbs,i);
		}
	} 
} // end updatePcbs()

// update clock for 1 iteration, or update by a custom millisec. amt
void updateClock(double r) {
	// update the clock
	runInfo->lClock += r;
	fprintf(stderr, "lClock: %.03f\n", runInfo->lClock); 
}

void initRunInfo(int shm_id) {
	// TODO make lClock not shared?
	// init system memory 256K
	// bit vector init to 0 for all

	// init lClock
	runInfo->lClock = 0.000;
} // end initRunInfo

// init and return pointer to shared pcb
pcb_t* initPcb(int pNum) {
	int shm_id, i;
	pcb_t *pcb;

	if ((shm_id = shmget(IPC_PRIVATE, sizeof(pcb_t*), IPC_CREAT | 0755)) == -1) {
		perror("shmget");
		exit(1);
	}	
	if ((pcb = (pcb_t*) shmat(shm_id,0,0)) == (void*)-1) {
		perror("shmat");
		exit(1);
	}
	pcb->pg_req.pNum = pNum;
	pcb->totalSysTime = 0.000;
	pcb->totalCpuTime = 0.000;
	pcb->cTime = runInfo->lClock;
	pcb->dTime = -1.000;
	pcb->isCompleted = false;
	pcb->shm_id = shm_id;
	
	// generate process size
	pcb->p_size = (rand() % 17) + 15; // min process size of 2K (15K - 32K size)

	// init page table for this process
	for (i = 0; i < pcb->p_size + 1; i++) {
		pcb->page_table[i].isValid = false;
		pcb->page_table[i].pAddr = -1;
		pcb->page_table[i].protectionBit = 0;
		pcb->page_table[i].isDirtyBit = false;
		pcb->page_table[i].refBit = 0;
	}
	// init semaphore for userProcess - block self 
	sem_init(&pcb->sem, 0, 0); // init 0
	
	return pcb;
} // end initPcb()

// bit array methods
// sets kth bit to 1
void setBit(int v[], int k) {
	v[(k/32)] |= 1 << (k % 32);
}
// sets kth bit to 0
void clearBit(int v[], int k) {
	v[(k/32)] &= ~(1 << (k % 32));
}
// returns value of kth bit
int testBit(int v[], int k) {
	return ((v[(k/32)] & (1 << (k % 32))) != 0);	
}

// detach and remove a pcb
void removePcb(pcb_t *pcbs[], int i) {
	int shm_id, n;
	if (pcbs[i] == NULL) {
		return;
	}
	for (n = 0; n < 32; n++) {
		if (pcbs[n]->page_table[n].isValid) {
			clearBit(sys_mem, n); 
			freePages++;
			// isValid cleared by setting pcb to NULL
		}
	}

	// clean up zombies
	waitpid(pcbs[i]->pid,NULL,0);
	// remove pcb semaphore
	sem_destroy(&pcbs[i]->sem);

	// clean up shared memory
	shm_id = pcbs[i]->shm_id;
	if((n = shmdt(pcbs[i])) == -1) {
		perror("shmdt:pcb");
	}
	if((n = shmctl(shm_id, IPC_RMID, NULL)) == -1) {
		perror("shmctl:IPC_RMID:pcb");
	}
	pcbs[i] = NULL;
}

// call removePcb on entire array of pcbs
void cleanUpPcbs(pcb_t *pcbs[]) {
	int i;
	for(i = 0; i < MAX_PROCESS_COUNT; i++) {
		if (pcbs[i] != NULL) {
			removePcb(pcbs, i);
		}
	}
}

// clean up with free(), clean up runInfo, call cleanUpPcbs()
void cleanUp() {
	int shm_id = runInfo->shm_id;

	// destory runInfo semaphore
	sem_destroy(&runInfo->sem);

	if ((shmdt(runInfo)) == -1) {
		perror("shmdt:runInfo");
	}
	if ((shmctl(shm_id, IPC_RMID, NULL)) == -1) {
		perror("shmctl:IPC_RMID:runInfo");	
	}

	cleanUpPcbs(pcbs);
	free(arg1);
	free(arg2);
	free(arg3);
}

// SIGINT handler
void free_mem() {
	int z;
	FILE *fp;
	fprintf(stderr, "Recieved SIGINT. Cleaning up and quiting.\n");
	if (backOfQ == 18) {
		fprintf (stderr, "ERR-Queue would have overflowed\n");
	}
	// end stats
	stats.turnA /= (double) stats.tPut;
	stats.waitT /= (double) stats.tPut;
	stats.cpuU = stats.totalCpuTime / runInfo->lClock; 
	
	if ((fp = fopen("endStats.txt","w")) == NULL) {
		perror("fopen:endstats");
	} else {
		// overwrite/write to file
		fprintf(fp,"End Stats:\nThroughput: %d\nAvg Turnaround: %.3f\nAvg Wait Time: %.3f\n",
			stats.tPut, stats.turnA, stats.waitT);
		fprintf(fp,"TotalCpuTime: %.3f\nTotal Run Time: %.3f\nCPU Utilization: %.3f\n",
			stats.totalCpuTime, runInfo->lClock, stats.cpuU);
		fclose(fp);
	}
	// write to stderr
	fprintf(stderr,"End Stats:\nThroughput: %d\nAvg Turnaround: %.3f\nAvg Wait Time: %.3f\n",
		stats.tPut, stats.turnA, stats.waitT);
	fprintf(stderr,"TotalCpuTime: %.3f\nTotal Run Time: %.3f\nCPU Utilization: %.3f\n",
		stats.totalCpuTime, runInfo->lClock, stats.cpuU);
	// make sure processes are killed
	for (z = 0; z < MAX_PROCESS_COUNT; z++) {
		if (pcbs[z] != NULL) {
			if (pcbs[z]->pid != -1) {
				kill(pcbs[z]->pid,SIGINT);
				waitpid(pcbs[z]->pid,NULL,0);
			}
		}
	}
	// clean up with free(), remove lClock, call cleanUpPcbs()
	cleanUp();

	signal(SIGINT, SIG_DFL); // resore default action to SIGINT
	raise(SIGINT); // take normal action for SIGINT after my clean up
}
