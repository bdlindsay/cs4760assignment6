#include "oss.h"

// Brett Lindsay
// cs4760 assignment6
// oss.c

#define MAX_PROCESS_COUNT 18
#define MAX_USER_PROCS 12
#define DEVICE_Q_SIZE 13
const double NO_FAULT = .0010;
const double FAULT = .0015;

char *arg1; // to send process_id num to process
char *arg2; // to send pcb shm_id num to process 
char *arg3; // to send runInfo shm_id num to process 
pcb_t *pcbs[MAX_USER_PROCS] = { NULL };
run_info_t *runInfo = NULL;
sim_stats_t stats; 
int sys_mem[8]; // bit array for 256K memory
int freePages = 256; // dynamically keep track of free frame #
int device_q[DEVICE_Q_SIZE] = { -1 }; // gauranteed 1 free slot
int backOfQ = 0;
int frontOfQ = 0;
double nextQPop = 0;
int deadlocks = 0;
int lowMem = 0;
int quits = 0;
int timeUpdate = 10;
int numProcs = 1;
// signal handler prototypes
void free_mem();

main (int argc, char *argv[]) {
	char *arg0 = "userProcess";
	arg1 = malloc(sizeof(int)); // process num in pcbs
	arg2 = malloc(sizeof(int)); // shm_id to pcb
	arg3 = malloc(sizeof(int)); // shm_id to runInfo
	int shm_id, q, n;
	int i = MAX_USER_PROCS;
	double r; // for random "milli seconds"
	int usedPcbs[1] = { 0 }; // bit vector PCBs
	bool isPcbsFull = false;
	int next = 0; // points to next available PID
	double nextCreate = 0; // time for next process creation 
	int res_pid;
	struct timeval tm;
	gettimeofday(&tm, NULL);
	srandom (tm.tv_sec + tm.tv_usec * 1000000ul);
	signal(SIGINT, free_mem);

	// init sim_stats_t for averages
	stats.tPut = 0;
	stats.turnA = 0.000;
	stats.waitT = 0.000;
	stats.totalCpuTime = 0.000;
	stats.cpuU = 0.000;
	// reset files
	FILE *fp = fopen ("frameTables.txt", "w");
	fclose (fp);
	fp = fopen("processStats.txt", "w");
	fprintf(fp, "Individual Process Stats\n");
	fclose(fp);

	// create shared runInfo
	if((shm_id = shmget(IPC_PRIVATE,sizeof(run_info_t*),IPC_CREAT | 0755)) == -1){
		perror("shmget:runinfo");
	}	
	runInfo = (run_info_t*) shmat(shm_id,0,0);
	runInfo->shm_id = shm_id;
	initRunInfo(shm_id);

	while(1) { // infinite loop until alarm finishes
		if (runInfo->lClock > 300) {
			fprintf(stderr,"Timeout duration reached\n");
			raise(SIGINT);
		}
		if (nextCreate <= runInfo->lClock) {
			isPcbsFull = true;
			if (i == MAX_USER_PROCS) {
				i = 0;
			}
			for (i; i < MAX_USER_PROCS; i++) {
				if (testBit(usedPcbs,i) == 0) { 
					next = i;	
					isPcbsFull = false;	// need to set false when process killed
					break;
				}	
			}
			if (isPcbsFull) {
				//fprintf(stderr, "PCBS FULL\n");
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
					numProcs++;
					execl("userProcess", arg0, arg1, arg2, arg3, 0);
				} 
				// only oss
				if (pcbs[next] != NULL) { // don't change nextCreate if fork failed
					// next process creation time
					r = (double)((random() % 500) + 1) / 1000; // 1-500 milli
					//r = FAULT;
					nextCreate = (double) runInfo->lClock + r;
				}
			}
		} // end create process if block

		// read each processes current request	
		monitorMemRefs();

		// optimize
		static int op = 0;
		if ( (op % 5) == 4) {
			// signal front of queue if time has passed
			updateQueue();
			// check if all processes are waiting on device
			deadlock();
			// check for finished processes
			updatePcbs(usedPcbs);
		}
		op++;

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
	int i, val;
	for (i = 0; i < MAX_USER_PROCS; i++) {
		if (pcbs[i] == NULL || pcbs[i]->isCompleted) { // if no process or completed
			if (pcbs[i] != NULL) {
				numProcs--;
			}
			continue;
		}
		if (pcbs[i]->isWaiting) { // if onQueue
			//fprintf(stderr, "Process %d is waiting on queue or old ref\n", i);
			continue;
		}	
	
		// if valid, let the process continue
		if (pcbs[i]->page_table[pcbs[i]->pg_ref.lAddr].isValid && pcbs[i]->pg_ref.hasNewReq) {
			//		pcbs[i]->pg_ref.hasNewReq) { 
			// if page in memory, let it continue
			pcbs[i]->pg_ref.hasNewReq = false;
			sem_post (&pcbs[i]->sem);
			updateClock(NO_FAULT);
			
			stats.totalCpuTime += NO_FAULT;
			pcbs[i]->pg_ref.isHit = true;
			pcbs[i]->hits++;
			//fprintf (stderr, "Process %d pg_ref-%d in memory...continuing. --- %d\n",
			//					i, pcbs[i]->pg_ref.lAddr, pcbs[i]->numRefs);
			// update time stamp of when used last
			int curr_pg = pcbs[i]->pg_ref.lAddr;
			pcbs[i]->page_table[curr_pg].timeStamp = runInfo->lClock;
			pcbs[i]->page_table[curr_pg].refBit = 1; // set ref bit
			if (pcbs[i]->pg_ref.isWrite == 1) {
				pcbs[i]->page_table[curr_pg].isDirtyBit = 1;
			}
		} else {
			// if page not in memory, put it on the queue for device
			pcbs[i]->pg_ref.hasNewReq = false;
			device_q[backOfQ++] = i;
			pcbs[i]->isWaiting = true;
			//fprintf (stderr, "Process %d to device queue. (size: %d-%d)\n", i, backOfQ, frontOfQ);
			
			pcbs[i]->pg_ref.isHit = false;
			pcbs[i]->misses++;
			// process i waits on device
			if (backOfQ == DEVICE_Q_SIZE) { // don't let queue overflow
				if (frontOfQ == 0) {
					fprintf (stderr, "Caught/Fix Error: A queue error Occurred!\n");
				}
				backOfQ = 0;
			}
		}
	}
}

void updateQueue() { 
	int i, nextFree;
	if (frontOfQ == backOfQ) { // empty queue
		return;
	}
	// if no next queue time and queue not empty, set next queue pop
	if (nextQPop == -1) {
		nextQPop = runInfo->lClock + FAULT;
	} 

	if (runInfo->lClock >= nextQPop) { // frontOfQ pops when FAULT time has passed
		for (i = 0; i < 256; i++) { // find next free in sys_mem
			if ((nextFree = testBit(sys_mem, i)) != 0) {
				break;
			}
			if (i == 255) {
				fprintf (stderr, "Error: ran out of memory in sys_mem\n");
			}
		}
		int p = device_q[frontOfQ];
		if (device_q[frontOfQ] == -1) {
			frontOfQ++; // can never be > backOfQ b/c of initial check
			if (frontOfQ == DEVICE_Q_SIZE) {
				frontOfQ = 0;
			}	
			if (frontOfQ == backOfQ) {
				fprintf (stderr, "Caught/Fix Error: Queue == -1 caused empty queue.\n");
				return;
			}
		}
		while (pcbs[p] == NULL) { // process was on queue and then quit?
			frontOfQ++;
			if (frontOfQ == DEVICE_Q_SIZE) {
				frontOfQ = 0;
			}
			quits++;
			if (frontOfQ == backOfQ) {
				fprintf (stderr, "Caught/Fix Error: PCB%d == NULL caused empty queue.\n", p);
				return;
			} else {
				p = device_q[frontOfQ];
			}
		}
		// set os data
		setBit(sys_mem, nextFree);
		freePages--; // dynamically keep track of free frames
		// set time stamp of inital load
		int curr_pg = pcbs[p]->pg_ref.lAddr;
		pcbs[p]->page_table[curr_pg].timeStamp = runInfo->lClock;
		pcbs[p]->page_table[curr_pg].refBit = 1; // set ref bit
		if (pcbs[p]->pg_ref.isWrite == 1) {
			pcbs[p]->page_table[curr_pg].isDirtyBit = 1;
		}
	//	fprintf (stderr, "Process %d handled for logical addr %d --- %d\n", 
	//		p, pcbs[p]->pg_ref.lAddr, pcbs[p]->numRefs);
		// set pcb data
		pcbs[p]->page_table[pcbs[p]->pg_ref.lAddr].pAddr = nextFree;
		pcbs[p]->page_table[pcbs[p]->pg_ref.lAddr].isValid = true;

		sem_post(&pcbs[p]->sem); // signal process on queue can go
		pcbs[p]->isWaiting = false;
		updateClock(NO_FAULT); // once fault has fixed, still adds time
		stats.totalCpuTime += NO_FAULT;
		// next queue position
		frontOfQ++;
		if (frontOfQ == DEVICE_Q_SIZE) { // keep frontOfQ in bounds
			frontOfQ = 0;
		}
		if (frontOfQ != backOfQ) { // set new nextQPop if list not empty
			nextQPop = runInfo->lClock + FAULT;
		} else {
			nextQPop = -1; // no one on queue
		}
	} // end of queuePop logic
}

void get_page() {
	int i, j, k;
	// free the oldest 5% of pages from memory
	//fprintf (stderr, "get_page() called\n");
	lowMem++;

	for (i = 0; i < MAX_USER_PROCS; i++) { // sweep each pcb
		if (pcbs[i] == NULL) {
			continue;
		}
		// turn off valid bit - ceiling of 5% of process size 
		int turnOff;
		if (pcbs[i]->p_size > 20) {
			turnOff = 2;
		} else {
			turnOff = 1;
		}
		double oldestTS = -1;
		int oldest = -1;
		int first = -1;
		for (j = 0; j < turnOff; j++) { // turn off 1 or 2 per process
			for (k = 0; k < pcbs[i]->p_size; k++) { // sweep frame table
				if (turnOff == 2 && k == oldest) {
					continue;
				}
				if (pcbs[i]->page_table[k].isValid) {
					if (oldest == -1) {
						oldestTS = pcbs[i]->page_table[k].timeStamp;
						oldest = k;
					} else if (pcbs[i]->page_table[k].timeStamp < oldest) {
						oldestTS = pcbs[i]->page_table[k].timeStamp;
						oldest = k;
					}
				}
			}
			// mark for replacement or free frame
			if (oldest != -1) {
				if (pcbs[i]->page_table[oldest].refBit == 1) {
					// mark for replacement
					pcbs[i]->page_table[oldest].refBit = 0;
				} else { // free frame and write back to disk if dirty*/
					/*if (pcbs[i]->page_table[oldest].isDirtyBit == 1) {
						fprintf (stderr, "Writing process %d page %d back to memory\n", i, oldest);
					} else {
						fprintf (stderr, "Process %d page %d not modified in sys_mem\n", i, oldest);
					}*/
					pcbs[i]->page_table[oldest].isDirtyBit = 0;
					pcbs[i]->page_table[oldest].isValid = false;
					clearBit(sys_mem, pcbs[i]->page_table[oldest].pAddr);
					freePages++;
				}
			}
			if (turnOff == 2) { // don't grab same one twice
				first = oldest;
				oldest = -1;
			}
		}
	}
		
}

void deadlock() {
	// if all processes are queued for device (i.e. waiting on their semaphore)
	// advance logical clock to fulfill the request at the head
	int i;
	bool allWaiting = true;
	for (i = 0; i < MAX_USER_PROCS; i++) {
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
		deadlocks++;
	}
}

// check for finished processes
void updatePcbs(int usedPcbs[]) {
	int i;
	FILE *fp;
	for (i = 0; i < MAX_USER_PROCS; i++) { // for each pcb 
		// continue if no pcb
		if (pcbs[i] == NULL) {
			continue;
		}	
		// if there is a completed process
		if (pcbs[i]->isCompleted) {
			// collect data on userProcess
			stats.tPut++;
			stats.turnA += pcbs[i]->totalSysTime;
			stats.waitT += pcbs[i]->totalWaitTime;
			//stats.totalCpuTime += pcbs[i]->totalCpuTime; 

			// handle things on device queue?
			int j;
			for (j = frontOfQ; j < backOfQ; j++) {
				if (device_q[j] == i) { // if process ended while on queue?? 
					fprintf (stderr, "Caught/Fix Error: Queue had removed process on it.\n");
					device_q[j] = -1;
				}
			}
			if ((fp = fopen("processStats.txt","a")) == NULL) { // write process stats to file
				perror ("fopen:");
			}
			fprintf(fp, "Process %d finished with stats:\n", i);
			fprintf(fp, "Hits: %d, Misses: %d\n", pcbs[i]->hits, pcbs[i]->misses);

			double hitRatio = (double)((double)pcbs[i]->hits/((double)pcbs[i]->hits+pcbs[i]->misses));
			fprintf(fp, "Hit Ratio: %.05f\t", hitRatio);
			fprintf(fp, "EAT: %.05f\n\n", (double)(FAULT * (1-hitRatio)) + (double)(NO_FAULT * hitRatio));

			fclose(fp); // close file

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
	if (runInfo->lClock > timeUpdate) {
		FILE *fp = fopen ("frameTables.txt", "a");
		int i = 0;
		int test = 256 - freePages;
		fprintf (fp, "\nFRAME TABLE:\n");
		for (; i < 256; i++) {
			if ( (test = testBit(sys_mem,i)) == 0) {
				fprintf (fp, "., ");
			} else {
				fprintf (fp, "+, ");
			}
			if ( (i % 32) == 31) {
				fprintf (fp, "\n");
			}
		}
		fclose(fp);
		timeUpdate += 10;
	}
}

void initRunInfo(int shm_id) {
	// init lClock - for userProcess reading
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
	pcb->pg_ref.pNum = pNum;
	pcb->pg_ref.hasNewReq = false;
	pcb->hits = 0;
	pcb->misses = 0;
	pcb->numRefs = 0;
	pcb->totalSysTime = 0.000;
	pcb->totalCpuTime = 0.000;
	pcb->totalWaitTime = 0.000;
	pcb->cTime = runInfo->lClock;
	pcb->dTime = -1.000;
	pcb->isCompleted = false;
	pcb->isWaiting = false;
	pcb->shm_id = shm_id;
	
	// generate process size
	pcb->p_size = (random() % 18) + 15; // process size (15K - 32K size)

	// init page table for this process
	for (i = 0; i < pcb->p_size; i++) { // i < 32 max case
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

	// clean up zombies
	waitpid(pcbs[i]->pid, NULL, 0);
	for (n = 0; n < pcbs[i]->p_size; n++) {
		if (pcbs[i]->page_table[n].isValid) {
			pcbs[i]->page_table[n].isValid = false;
			/*if (pcbs[i]->page_table[n].isDirtyBit == 1) {
				fprintf (stderr, "Writing process %d page %d back to memory\n", i, n);
			} else {
				fprintf (stderr, "Process %d page %d not modified in sys_mem\n", i, n);
			}*/
			clearBit(sys_mem, pcbs[i]->page_table[n].pAddr); 
			freePages++;
		}
	}

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
	for(i = 0; i < MAX_USER_PROCS; i++) {
		if (pcbs[i] != NULL) {
			removePcb(pcbs, i);
		}
	}
}

// clean up with free(), clean up runInfo, call cleanUpPcbs()
void cleanUp() {
	int shm_id = runInfo->shm_id;

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

	// make sure processes are killed
	for (z = 0; z < MAX_USER_PROCS; z++) {
		if (pcbs[z] != NULL) {
			if (pcbs[z]->pid != -1) {
				kill(pcbs[z]->pid,SIGINT);
				waitpid(pcbs[z]->pid,NULL,0);
			}
		}
	}
	// end stats
	stats.turnA = (double) runInfo->lClock/stats.tPut;
	stats.turnA /= (double) stats.tPut;
	stats.waitT /= (double) stats.tPut;
	//stats.cpuU = (double) (stats.totalCpuTime - (double)(stats.tPut * stats.waitT) - 
	//	(double)(deadlocks*.0015)) / stats.totalCpuTime;
	stats.cpuU = stats.totalCpuTime / runInfo->lClock; 
	
	if ((fp = fopen("endStats.txt","w")) == NULL) {
		perror("fopen:endstats");
	} else {
		// overwrite/write to file
		fprintf(fp,"End Stats:\nThroughput: %d\nAvg Turnaround: %.3f\nAvg Wait Time: %.3f\n",
			stats.tPut, stats.turnA, stats.waitT);
		fprintf(fp,"TotalCpuTime: %.3f\nTotal Run Time: %.3f\nCPU Utilization: %.3f\n",
			stats.totalCpuTime, runInfo->lClock, stats.cpuU);
		//fprintf(fp,"Deadlocks fixed: %d\n", deadlocks);
		//fprintf(fp,"Low Memory Warnings Fixed: %d\n", lowMem); 
		fclose(fp);
	}
	// write to stderr
	fprintf(stderr,"End Stats:\nThroughput: %d\nAvg Turnaround: %.3f\nAvg Wait Time: %.3f\n",
		stats.tPut, stats.turnA, stats.waitT);
	fprintf(stderr,"TotalCpuTime: %.3f\nTotal Run Time: %.3f\nCPU Utilization: %.3f\n",
		stats.totalCpuTime, runInfo->lClock, stats.cpuU);
	//fprintf(stderr,"Deadlocks fixed: %d\n", deadlocks);
	//fprintf(stderr,"Low Memory Warnings Fixed: %d\n", lowMem); 
	//fprintf(stderr,"Internal Errors: %d\n", quits);
	// clean up with free(), remove lClock, call cleanUpPcbs()
	cleanUp();

	signal(SIGINT, SIG_DFL); // resore default action to SIGINT
	raise(SIGINT); // take normal action for SIGINT after my clean up
}
