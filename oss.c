#include<unistd.h>
#include<signal.h>
#include<stdio.h>
#include<stdlib.h>
#include<sys/types.h>
#include<sys/ipc.h>
#include<sys/shm.h>
#include<sys/msg.h>
#include<time.h>
#include"ErrorLogging.h"
#include"IPCUtilities.h"
#include"PeriodicTimer.h"
#include"ProcessControlBlock.h"
#include"ProcessUtilities.h"

#define MAX_PROCESSES 18
#define ID_SECONDS 1
#define ID_NANO_SECONDS 2
#define ID_PCB 3
#define ID_MSG_TO 4
#define ID_MSG_FROM 5
#define TOTAL_PROCESS_LIMIT 100
#define NANO_PER_SECOND 1000000000
#define MAX_LINES_WRITE 10000
#define MAX_NEW_PROC_S 1
#define MAX_NEW_PROC_NS 100000
#define MAX_DISPATCH 100000

// define time quantums
#define REALTIME_QUANTUM 10000000

// Global variable definitions
// Probably not the cleanest to have these global - but I'm not sure of a better way
// since we may need to deallocate these outside of main if we get a signal causing 
// us to cancel

// Pointer to shared global process control block
// will be stored in shared memory
struct ProcessControlBlock* pcb = NULL;

// shared memory id of process control block
int shmidPCB = 0;

// Pointer to shared global seconds integer
int* seconds = NULL;

// shared memory id of seconds
int shmidSeconds = 0;

// Pointer to shared memory global nanoseconds integer
int* nanoSeconds = NULL;

// shared memory id of nano seconds
int shmidNanoSeconds = 0;

// MsgID of queue to send messages to child
int msgIdToChild = 0;

// MsgID of queue to send responses back to oss
int msgIdToOss = 0;

// To save time, instead of passing argv[0] to each function, store it here
const char* processName = NULL;

// Bit array to mark each PCB entry as occupied or not
int pcbOccupied[MAX_PROCESSES];

// Total processed spawned - need to track so after 100 we can quit
int totalProcessesCompleted = 0;

// Total lines written
int totalLinesWritten = 0;

// Output log file
FILE* ossLog = NULL;

typedef struct {
	long mtype;
	char mtext[50];
} mymsg_t;

// Function prototypes
void allocateAllSharedMemory();
void deallocateAllSharedMemory();
void allocateAllSharedMessageQueues();
void deallocateAllSharedMessageQueues();
void excuteOss();
void handleInterruption(int);
void spawnProcess(pid_t *);
int  getProcessToDispath(pid_t *, int *);

int main(int argc, char** argv)
{
	srand(time(NULL));
	processName = argv[0];
	
	signal(SIGINT, handleInterruption);
	signal(SIGALRM, handleInterruption);
	
	printf("Begin execution of oss\n");

	// Initialize bit vector of PCB table to empty
	int i; 
	for (i = 0; i < MAX_PROCESSES; ++i)
		pcbOccupied[i] = 0;

	printf("Allocated shared IPC resources\n");
	// Allocate and attach our shared memory
	allocateAllSharedMemory();

	// Allocate message queues
	allocateAllSharedMessageQueues();
	
	*seconds = 0;
	*nanoSeconds = 0;
	printf("Intialized system clock:\nSeconds: %d\nNanoSeconds: %d\n", *seconds, *nanoSeconds);

	ossLog = fopen("oss.log", "w");
	setPeriodic(10);

	// Main logic	
	executeOss();

	int status;
	pid_t childpid;
	while((childpid = wait(&status)) > 0);
	
	if (ossLog != NULL)
		fclose(ossLog);

	printf("Deallocate shared IPC resources\n");

	// We're done, should be able to deallocate shared memory safely	
	deallocateAllSharedMemory();
	
	// Deallocate all our message queues
	deallocateAllSharedMessageQueues();

	return 0;
}

// Execute the main loop of the oss
void executeOss()
{
	mymsg_t childMsg, ossMsg;

	// Define our queues - will actually just be arrays since our max is small and known
	pid_t* realTimeQueue = malloc(sizeof(pid_t) * MAX_PROCESSES);

	int bytesRead;

	int continueLooping = 0;
	int timeQuantum = 0;
	int processSpawnSeconds = 0, processSpawnNanoSeconds = 0;

	while (!continueLooping  && *seconds < 1)
	{
		// check to see if we need to spawn a new process
		if (*seconds >= processSpawnSeconds && *nanoSeconds >= processSpawnNanoSeconds)
		{
			spawnProcess(realTimeQueue);
			
			processSpawnSeconds = *seconds;
			processSpawnNanoSeconds = *nanoSeconds + (rand() % MAX_NEW_PROC_NS);
			
			if (processSpawnNanoSeconds >= NANO_PER_SECOND)
			{
				++processSpawnSeconds;
				processSpawnNanoSeconds -= NANO_PER_SECOND;
			}
		
			processSpawnSeconds += (rand() % MAX_NEW_PROC_S);
			printf("Scheduled for %d and %d\n", processSpawnSeconds, processSpawnNanoSeconds);
		}
		
		int index = getProcessToDispatch(realTimeQueue, &timeQuantum);
		
		// if we have a process to run, then we need to run it
		if (index != -1)
		{
			if (ossLog != NULL && totalLinesWritten < MAX_LINES_WRITE)
			{
				fprintf(ossLog, "OSS: Dispatching process with PID %d from queue 0 at time %d:%d\n", pcb[index].ProcessId, *seconds, *nanoSeconds);
				++totalLinesWritten;
			}
			childMsg.mtype = pcb[index].ProcessId;
			snprintf(childMsg.mtext, 50, "%d", timeQuantum);

			// dispatch process	
			if (msgsnd(msgIdToChild, &childMsg, sizeof(childMsg), 0) == -1)
				writeError("Failed when sending message to child\n", processName);
		
			int dispatchNanoSeconds = rand() % MAX_DISPATCH;

			if (ossLog != NULL && totalLinesWritten < MAX_LINES_WRITE)
			{
				fprintf(ossLog, "OSS: total time for this dispatch was %d nanoseconds\n", dispatchNanoSeconds);
				++totalLinesWritten;
			}
		
			*nanoSeconds += dispatchNanoSeconds;
			if (*nanoSeconds >= NANO_PER_SECOND)
			{
				*seconds += 1;
				*nanoSeconds -= NANO_PER_SECOND;
			}
			
			// regain control from child process
			if ((bytesRead = msgrcv(msgIdToOss, &ossMsg, sizeof(ossMsg), 1, 0)) == -1)
				writeError("Failed when receiving message message from child\n", processName);

			int returnedValue = atoi(ossMsg.mtext);
			if (returnedValue > 0)
			{
				*nanoSeconds += returnedValue;
				++totalProcessesCompleted;
				
				printf("Child %d finished\n", pcb[index].ProcessId);
				

				if (ossLog != NULL && totalLinesWritten < MAX_LINES_WRITE)
				{
					fprintf(ossLog, "OSS: Receiving that process with PID %d ran for %d nanoseconds\n", pcb[index].ProcessId, returnedValue);
					++totalLinesWritten;
				}

				pcbOccupied[index] = 0;
				pcb[index].ProcessId = 0;
				pcb[index].RealTime = 0;
			}
			else if (returnedValue == 0)
			{
				*nanoSeconds += timeQuantum;

				if (ossLog != NULL && totalLinesWritten < MAX_LINES_WRITE)
				{
					fprintf(ossLog, "OSS: Receiving that process with PID %d ran for %d nanoseconds\n", pcb[index].ProcessId, timeQuantum);
					++totalLinesWritten;
				}

				// Didn't complete, need to 
				if (pcb[index].RealTime)
					EnqueueValue(realTimeQueue, pcb[index].ProcessId, MAX_PROCESSES);
			}

			if (*nanoSeconds >= NANO_PER_SECOND)
			{
				*seconds += 1;
				*nanoSeconds -= NANO_PER_SECOND;
			}
		}
		else
		{
			int dispatchNanoSeconds = rand() % MAX_DISPATCH;
			if (ossLog != NULL && totalLinesWritten < MAX_LINES_WRITE)
			{
				fprintf(ossLog, "OSS: total time for this dispatch was %d nanoseconds\n", dispatchNanoSeconds);
				++totalLinesWritten;
			}

			*nanoSeconds += dispatchNanoSeconds;
			if (*nanoSeconds >= NANO_PER_SECOND)
			{
				*seconds += 1;
				*nanoSeconds -= NANO_PER_SECOND;
			}
		}
	}

	// deallocate queues
	free(realTimeQueue);
}

void spawnProcess(pid_t* realTimeQueue)
{
	int index, found;
	for (index = 0, found = 0; index < MAX_PROCESSES && !found; ++index)
	{
		if (pcbOccupied[index] == 0)
			found = 1;
	}
	
	// If we can't find a spot, don't spawn
	if (!found)
		return;

	pid_t newChild = createChildProcess("./user", processName);

	if (ossLog != NULL && totalLinesWritten < MAX_LINES_WRITE)
	{
		fprintf(ossLog, "OSS: Generating process with PID %d  and putting it in queue 0 at time %d:%d\n", newChild, *seconds, *nanoSeconds);
		++totalLinesWritten;
	}

	pcb[index].ProcessId = newChild;
	pcb[index].RealTime = 1; // Hardcode to be realtime first, others lateri
	pcbOccupied[index] = 1;
	if (pcb[index].RealTime)
		EnqueueValue(realTimeQueue, pcb[index].ProcessId, MAX_PROCESSES);	
}

int getProcessToDispatch(pid_t* realTimeQueue, int* timeQuantum)
{
	pid_t processToDispatch = 0;
	int index = -1; // index in process control table of process to be dispatched

	processToDispatch = DequeueValue(realTimeQueue, MAX_PROCESSES);
	*timeQuantum = REALTIME_QUANTUM;	


	if (processToDispatch == 0)
	{
		// look into other queues
	}

	if (processToDispatch != 0)
	{
		int i;
		for (i = 0; i < MAX_PROCESSES && index == -1; ++i)
		{
			if (pcb[i].ProcessId == processToDispatch)
				index = i;
		}
	}
	
	if (processToDispatch == 0)
		index = -1;

	return index;
}

// Allocate our shared memory and attach it to this process
void allocateAllSharedMemory()
{
	struct ProcessControlBlock pc;

	// allocate PCB Memory block 
	shmidPCB = allocateSharedMemory(ID_PCB, sizeof(pc) * MAX_PROCESSES, processName);
	pcb = shmat(shmidPCB, 0, 0);

	// allocate seconds
	shmidSeconds = allocateSharedMemory(ID_SECONDS, sizeof(int), processName);
	seconds = (int*)shmat(shmidSeconds, 0, 0);
	
	// allocate nano seconds
	shmidNanoSeconds = allocateSharedMemory(ID_NANO_SECONDS, sizeof(int), processName);
	nanoSeconds = (int*)shmat(shmidNanoSeconds, 0, 0);
	
	if (pcb == NULL || pcb == -1)
		writeError("Failed to attach PCB memory to this process\n", processName);

	if (seconds == NULL || seconds == -1)
		writeError("Failed to attach seconds memory to this process\n", processName);

	if (nanoSeconds == NULL || nanoSeconds == -1)
		writeError("Failed to attach nano seconds memory to this process\n", processName);
}

// Detach and deallocate shared memory
void deallocateAllSharedMemory()
{
	int returnValue = 0;

	// First, we have shared memory attached to this process, then we need to detach from it
	if (pcb != NULL)
	{
		if ( shmdt(pcb) == -1)
			writeError("Failed to detach PCB from this process\n", processName);
	}

	if (seconds != NULL)
	{
		if ( shmdt(seconds) == -1)
			writeError("Failed to detach seconds from this process\n", processName);
	}

	if (nanoSeconds != NULL)
	{
		if ( shmdt(nanoSeconds) == -1)
			writeError("Failed to detach nano seconds from this process\n", processName);
	}

	// Now that it's detached from this process, deallocate it
	deallocateSharedMemory(shmidPCB, processName);
	deallocateSharedMemory(shmidSeconds, processName);
	deallocateSharedMemory(shmidNanoSeconds, processName);
}

void allocateAllSharedMessageQueues()
{
	msgIdToChild = allocateMessageQueue(ID_MSG_TO, processName);
	msgIdToOss = allocateMessageQueue(ID_MSG_FROM, processName);	
}

void deallocateAllSharedMessageQueues()
{
	if (msgIdToChild != 0)
		deallocateMessageQueue(msgIdToChild, processName);

	if (msgIdToOss != 0)
		deallocateMessageQueue(msgIdToOss, processName);
}

void handleInterruption(int signo)
{
	if (signo == SIGALRM || signo == SIGINT)
	{
		if (ossLog != NULL)
			fclose(ossLog);

		deallocateAllSharedMessageQueues();
		deallocateAllSharedMemory();

		printf("oss exiting due to signal\n");
		kill(0, SIGKILL);	
	}
}
