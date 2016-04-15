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

// Brett Lindsay
// cs4760 assignment 5
// oss.h

typedef enum {false, true} bool;

// info for num of a resource from which process
typedef struct {
	int pNum;
	int byteAddr;
	bool isWrite;
	bool isDone;
} info_t;

// a process control block
typedef struct {
	info_t mem_req;
	double totalSysTime; // time in system
	double totalCpuTime; // time spent computing
	double cTime; // create time
	double dTime; // destroy time
	bool isCompleted;
	int shm_id; // ref to id for shm
	int pid; // pid for fork() return value
	sem_t sem; // semaphore for each process to wait on
} pcb_t;	

// a resource descriptor
typedef struct {
	int addr;
	int protectionBit;
	bool isDirtyBit;
	int refBit;
} page_t;	

typedef struct {
	page_t pages[32]; // process max 32K
} table_t;	

// logical clock and resource descriptors in oss 
typedef struct {
	double lClock;
	sem_t sem; // semaphore for lClock access
	table_t page_tables[8]; // 32k tables -> 8 tables for 256K sys
	int free_frames[8]; // bit vector for the 256 pages
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
