#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <semaphore.h>
#include <time.h>

// Brett Lindsay
// cs4760 assignment 6
// oss.h

typedef enum {false, true} bool;

// info for num of page to reference
typedef struct {
	unsigned pNum:5;
	unsigned lAddr:5;
	unsigned isWrite:1;
	unsigned isHit:1;
	unsigned hasNewReq:1;
	/*int pNum;
	int lAddr;
	bool isWrite;
	bool isHit; // hit or miss in memory
	bool isDone;*/
} info_t;

// a page in memory
typedef struct {
	unsigned isValid:1;
	unsigned pAddr:5;
	unsigned protectionBit:1;
	unsigned isDirtyBit:1;
	unsigned refBit:1;
	double timeStamp;
	/*bool isValid;
	int pAddr;
	int protectionBit;
	bool isDirtyBit;
	int refBit;*/
} page_t;	

// a process control block
typedef struct {
	info_t pg_ref;
	bool isWaiting; // if waiting on device
	int p_size; // size of process in pages
	page_t page_table[32]; // a whole page table (32K)	
	double totalSysTime; // time in system
	double totalCpuTime; // time spent computing
	double totalWaitTime; // time spent waiting
	double cTime; // create time
	double dTime; // destroy time
	int hits;
	int misses;
	int numRefs;
	bool isCompleted;
	int shm_id; // ref to id for shm
	int pid; // pid for fork() return value
	sem_t sem; // semaphore for each process to wait on
} pcb_t;	

// logical clock in oss 
typedef struct {
	double lClock;
	int shm_id; // run_info_t shm_id 
} run_info_t;

// simulation stats 
typedef struct {
	int tPut; // throughput
	double turnA; // turnaround
	double waitT; // waiting time
	double totalCpuTime; // total Cpu time to calc utilil.
	double cpuU; // cpu utilization
} sim_stats_t;

// helper functions
pcb_t* initPcb(int);
void updateClock(double);
void cleanUpPcbs(pcb_t *pcbs[]);
void cleanUp();
void removePcb(pcb_t *pcbs[], int i);
void updatePcbs(int usedPcbs[]);
void setBit(int*,int);
void clearBit(int*,int);
int testBit(int*,int);
void initRunInfo(int);
void deadlock();
void monitorMemRefs();
void updateQueue();
void get_page();
