#include<unistd.h>
#include<math.h>
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
#define MAX_DISPATCH_BLOCKED 500000
#define MAX_PERCENT 100

// define time quantums
#define BASE_QUANTUM 1000000

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

// Total processed completed - need to track so after 100 we can quit
int totalProcessesCompleted = 0;

// Total processes spawned
int totalProcessesSpawned = 0;

// Total lines written
int totalLinesWritten = 0;

// Output log file
FILE* ossLog = NULL;

// Queue Objects
// Define our queues - will actually just be arrays since our max is small and known
pid_t* realTimeQueue;
pid_t* ioBlockedQueue;

// Multilevel queue for non-realtime processes
pid_t* firstPriorityQueue;
pid_t* secondPriorityQueue;
pid_t* thirdPriorityQueue;

// Local variables to produce statistics at end
long totalSleepingNanoSeconds = 0;
long totalWaitingNanoSeconds = 0;
long totalTurnaroundNanoSeconds = 0;
long totalIdleNanoSeconds = 0;

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
void spawnProcess();
int  getProcessToDispath(int *);
int checkCommandArgs(int, char**);
void displayStatistics();

int main(int argc, char** argv)
{
	// seed random which wwill be used to generate random numbers
	srand(time(NULL) * getpid());

	// Set our global process name - will be used for writing of errors
	processName = argv[0];
	
	// Set signals which will handle timer inerrupts and command line interruptions
	signal(SIGINT, handleInterruption);
	signal(SIGALRM, handleInterruption);
	
	printf("Begin execution of oss\n");

	// Handle if the command line argument for help has been passed
	if (checkCommandArgs(argc, argv))
		return 0;
	
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
	realTimeQueue = malloc(sizeof(pid_t) * MAX_PROCESSES);
	ioBlockedQueue = malloc(sizeof(pid_t) * MAX_PROCESSES);

	// Multilevel queue for non-realtime processes
	firstPriorityQueue = malloc(sizeof(pid_t) * MAX_PROCESSES);
	secondPriorityQueue = malloc(sizeof(pid_t) * MAX_PROCESSES);
	thirdPriorityQueue = malloc(sizeof(pid_t) * MAX_PROCESSES);

	int bytesRead;

	int stopLooping = 0;
	int timeQuantum = 0;
	int processSpawnSeconds = 0, processSpawnNanoSeconds = 0;
	
	while (!stopLooping)
	{
		// check to see if we need to spawn a new process
		if (*seconds >= processSpawnSeconds && *nanoSeconds >= processSpawnNanoSeconds)
		{
			spawnProcess();
			
			processSpawnSeconds = *seconds;
			processSpawnNanoSeconds = *nanoSeconds + (rand() % MAX_NEW_PROC_NS);
			
			if (processSpawnNanoSeconds >= NANO_PER_SECOND)
			{
				++processSpawnSeconds;
				processSpawnNanoSeconds -= NANO_PER_SECOND;
			}
		
			processSpawnSeconds += (rand() % MAX_NEW_PROC_S);
		}
		 
		
		// check to see if anything needs to be unblocked
		pid_t blockedPid = DequeueValue(ioBlockedQueue, MAX_PROCESSES);
		pid_t* tempBlockedQueue = malloc(sizeof(pid_t) * MAX_PROCESSES);
		InitializeQueue(tempBlockedQueue, MAX_PROCESSES);
		while(blockedPid != 0)
		{
			int blockedIndex = -1;
			for (blockedIndex = 0; blockedIndex < MAX_PROCESSES; ++blockedIndex)
			{
				if (pcb[blockedIndex].ProcessId == blockedPid)
					break;
			}
			
			if (pcb[blockedIndex].BlockedSeconds <= *seconds && pcb[blockedIndex].BlockedNanoSeconds <= *nanoSeconds)
			{
				printf("Dequeued process %d because it is passed time %d:%d\n", pcb[blockedIndex].ProcessId, pcb[blockedIndex].BlockedSeconds, pcb[blockedIndex].BlockedNanoSeconds);
				
				// Event has happened, process needs to be requeued for execution
				pcb[blockedIndex].IOBlocked = 0;
				pcb[blockedIndex].BlockedSeconds = 0;
				pcb[blockedIndex].BlockedNanoSeconds = 0;

				// logic for additional classes here
				EnqueueValue(realTimeQueue, pcb[blockedIndex].ProcessId, MAX_PROCESSES);
				
				// Now that we're unblocked, we need to track how much time we were blocked at 
				pcb[blockedIndex].NanoSecondsSleeping += ((*seconds - pcb[blockedIndex].BlockedAtSeconds) * (NANO_PER_SECOND)) + (*nanoSeconds - pcb[blockedIndex].BlockedAtNanoSeconds);

				pcb[blockedIndex].BlockedAtSeconds = 0;
				pcb[blockedIndex].BlockedAtNanoSeconds = 0;
	
				int dispatchTime =  rand() % MAX_DISPATCH_BLOCKED;
				*nanoSeconds += dispatchTime;
				if (*nanoSeconds >= NANO_PER_SECOND)
				{
					*seconds += 1;
					*nanoSeconds -= NANO_PER_SECOND;
				}
				
				if (ossLog != NULL && totalLinesWritten < MAX_LINES_WRITE)
				{
					fprintf(ossLog, "OSS: Process %d moved from blocked queue to queue %d taking %d nanoseconds, at time %d:%d\n", pcb[blockedIndex].ProcessId, pcb[blockedIndex].QueueNumber, dispatchTime, *seconds, *nanoSeconds);
					++totalLinesWritten; 
				}
			}
			else
			{
				EnqueueValue(tempBlockedQueue, pcb[blockedIndex].ProcessId, MAX_PROCESSES);
			}
			
			blockedPid = DequeueValue(ioBlockedQueue, MAX_PROCESSES);
		}
		
		blockedPid = DequeueValue(tempBlockedQueue, MAX_PROCESSES);
		while (blockedPid != 0)
		{
			EnqueueValue(ioBlockedQueue, blockedPid, MAX_PROCESSES);
			blockedPid = DequeueValue(tempBlockedQueue, MAX_PROCESSES);
		}
		
		free(tempBlockedQueue);

		int index = getProcessToDispatch(&timeQuantum);
		// if we have a process to run, then we need to run it
		if (index != -1)
		{
			pcb[index].NanoSecondsWaiting += ((*seconds - pcb[index].LastScheduledSeconds) * (NANO_PER_SECOND)) + (*nanoSeconds - pcb[index].LastScheduledNanoSeconds);
			pcb[index].LastScheduledSeconds = *seconds;
			pcb[index].LastScheduledNanoSeconds = *nanoSeconds;
		
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
			// Handle if the process terminating, using some portion of its time quantum
			if (returnedValue > 0)
			{
				*nanoSeconds += returnedValue;
				if (*nanoSeconds >= NANO_PER_SECOND)
				{
					*seconds += 1;
					*nanoSeconds -= NANO_PER_SECOND;
				}

				++totalProcessesCompleted;				
				printf("Child %d finished\n", pcb[index].ProcessId);
		
				// Add times to statistics	
				totalSleepingNanoSeconds += pcb[index].NanoSecondsSleeping;
				totalWaitingNanoSeconds += pcb[index].NanoSecondsWaiting;
				
				// Calculate turn around time for process
				totalTurnaroundNanoSeconds += (*seconds - pcb[index].CreatedAtSeconds) * NANO_PER_SECOND;
				totalTurnaroundNanoSeconds += *nanoSeconds - pcb[index].CreatedAtNanoSeconds;

				if (ossLog != NULL && totalLinesWritten < MAX_LINES_WRITE)
				{
					fprintf(ossLog, "OSS: Receiving that process with PID %d ran for %d nanoseconds and then completed\n", pcb[index].ProcessId, returnedValue);
					++totalLinesWritten;
				}
				
				int status;
				waitpid(pcb[index].ProcessId, &status, 0); 
				pcbOccupied[index] = 0;
				pcb[index].ProcessId = 0;
				pcb[index].RealTime = 0;
			}
			// Handle if the process used some portion of its time quantum and then was blocked.
			else if (returnedValue < 0)
			{
				returnedValue = returnedValue * -1;
				*nanoSeconds += returnedValue;
				
				if (*nanoSeconds >= NANO_PER_SECOND)
				{
					*seconds += 1;
					*nanoSeconds -= NANO_PER_SECOND;
				}
		
				// Need to keep track of when the process became blocked
				pcb[index].BlockedAtSeconds = *seconds;
				pcb[index].BlockedAtNanoSeconds = *nanoSeconds;
	
				printf("Child %d became blocked on IO\n", pcb[index].ProcessId);

				if (ossLog != NULL && totalLinesWritten < MAX_LINES_WRITE)
				{
					fprintf(ossLog, "OSS: Receiving that process with PID %d ran for %d nanoseconds and was blocked until time %d:%d\n", pcb[index].ProcessId, returnedValue, pcb[index].BlockedSeconds, pcb[index].BlockedNanoSeconds);
					++totalLinesWritten;
				}
				
				

				// Block this pid_t so that we can later seee if it's ready to be processed
				EnqueueValue(ioBlockedQueue, pcb[index].ProcessId, MAX_PROCESSES);	
			}
			// Else, process used it's whole portion of the time quantum
			else if (returnedValue == 0)
			{
				*nanoSeconds += timeQuantum;
				if (*nanoSeconds >= NANO_PER_SECOND)
				{
					*seconds += 1;
					*nanoSeconds -= NANO_PER_SECOND;
				}
				
				if (ossLog != NULL && totalLinesWritten < MAX_LINES_WRITE)
				{
					fprintf(ossLog, "OSS: Receiving that process with PID %d ran for %d nanoseconds\n", pcb[index].ProcessId, timeQuantum);
					++totalLinesWritten;
				}

				// Didn't complete, need to requeue this process
				if (pcb[index].RealTime)
				{
					EnqueueValue(realTimeQueue, pcb[index].ProcessId, MAX_PROCESSES);
				}
				else if (pcb[index].QueueNumber == 1)
				{
					pcb[index].QueueNumber = 2;
					EnqueueValue(secondPriorityQueue, pcb[index].ProcessId, MAX_PROCESSES);
				}
				else if (pcb[index].QueueNumber == 2)
				{
					pcb[index].QueueNumber = 3;
					EnqueueValue(thirdPriorityQueue, pcb[index].ProcessId, MAX_PROCESSES);
				}
				else
				{
					EnqueueValue(thirdPriorityQueue, pcb[index].ProcessId, MAX_PROCESSES);
				}
			}
		}
		else
		{
			
			int dispatchNanoSeconds = rand() % MAX_DISPATCH;
	
			*nanoSeconds += dispatchNanoSeconds;
			if (*nanoSeconds >= NANO_PER_SECOND)
			{
				*seconds += 1;
				*nanoSeconds -= NANO_PER_SECOND;
			}
			
			totalIdleNanoSeconds += dispatchNanoSeconds;
			
			if (totalProcessesCompleted >= TOTAL_PROCESS_LIMIT && !checkForProcesses())
			{
				printf("Test output %d:%d\n", *seconds, *nanoSeconds);
				stopLooping = 1;
			}
		}
	}

	// deallocate queues
	free(realTimeQueue);
	free(ioBlockedQueue);
	free(firstPriorityQueue);
	free(secondPriorityQueue);
	free(thirdPriorityQueue);

	displayStatistics();
}

int checkForProcesses()
{
	int processesRunning = 0;
	int index;
	for (index = 0; index < MAX_PROCESSES; ++index)
	{
		if (pcbOccupied[index] == 1)
		{
			processesRunning = 1;
			break;
		}
	}

	return processesRunning;
}

void spawnProcess()
{
	// Only spawn a total of 100 processes, if we have hit this limit, just don't spawn any more
	if (totalProcessesSpawned >= TOTAL_PROCESS_LIMIT)
		return;

	int index, found;
	for (index = 0, found = 0; index < MAX_PROCESSES && !found; ++index)
	{
		if (pcbOccupied[index] == 0)
		{
			found = 1;
			break;
		}
	}
	
	// If we can't find a spot, don't spawn
	if (!found)
		return;

	pid_t newChild = createChildProcess("./user", processName);

	// Initialize process control block for this process
	pcb[index].ProcessId = newChild;
	pcb[index].BlockedAtSeconds = 0;
	pcb[index].BlockedAtNanoSeconds = 0;
	pcb[index].LastScheduledSeconds = *seconds;
	pcb[index].LastScheduledNanoSeconds = *nanoSeconds;
	pcb[index].NanoSecondsWaiting = 0;
	pcb[index].NanoSecondsSleeping = 0;
	pcb[index].CreatedAtSeconds = *seconds;
	pcb[index].CreatedAtNanoSeconds = *nanoSeconds;

	if ((rand() % MAX_PERCENT) > 5)
		pcb[index].RealTime = 0;
	else
		pcb[index].RealTime = 1;

	pcbOccupied[index] = 1;

	// Queue based on what type of process we have
	if (pcb[index].RealTime)
	{
		pcb[index].QueueNumber = 0;
		EnqueueValue(realTimeQueue, pcb[index].ProcessId, MAX_PROCESSES);
	}
	else
	{
		pcb[index].QueueNumber = 1;
		EnqueueValue(firstPriorityQueue, pcb[index].ProcessId, MAX_PROCESSES);
	}

	if (ossLog != NULL && totalLinesWritten < MAX_LINES_WRITE)
	{
		fprintf(ossLog, "OSS: Generating process with PID %d and putting it in queue %d at time %d:%d\n", newChild, pcb[index].QueueNumber, *seconds, *nanoSeconds);
		++totalLinesWritten;
	}

	++totalProcessesSpawned;
}

int getProcessToDispatch(int* timeQuantum)
{
	pid_t processToDispatch = 0;
	int index = -1; // index in process control table of process to be dispatched

	processToDispatch = DequeueValue(realTimeQueue, MAX_PROCESSES);
	*timeQuantum = BASE_QUANTUM;	
	
	if (processToDispatch == 0)
	{
		processToDispatch = DequeueValue(firstPriorityQueue, MAX_PROCESSES);
		*timeQuantum = BASE_QUANTUM * 2 * pow(2, 0);
	}

	if (processToDispatch == 0)
	{
		processToDispatch = DequeueValue(secondPriorityQueue, MAX_PROCESSES);
		*timeQuantum = BASE_QUANTUM * 2 * pow(2, 1);
	}

	if (processToDispatch == 0)
	{
		processToDispatch = DequeueValue(thirdPriorityQueue, MAX_PROCESSES);
		*timeQuantum = BASE_QUANTUM * 2 * pow(2, 2);
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
	else if (index != -1)
	{
		if (ossLog != NULL && totalLinesWritten < MAX_LINES_WRITE)
		{
			fprintf(ossLog, "OSS: Dispatching process with PID %d from queue %d at time %d:%d\n", pcb[index].ProcessId,  pcb[index].QueueNumber, *seconds, *nanoSeconds);
			++totalLinesWritten;
		}

	}
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

// allocate all our IPC message queues in one spot, setting the global variables to the id
void allocateAllSharedMessageQueues()
{
	msgIdToChild = allocateMessageQueue(ID_MSG_TO, processName);
	msgIdToOss = allocateMessageQueue(ID_MSG_FROM, processName);	
}

// Deallocate the messages queues used by the process assuming they have actually been allocated
// (not equal to 0)
void deallocateAllSharedMessageQueues()
{
	if (msgIdToChild != 0)
		deallocateMessageQueue(msgIdToChild, processName);

	if (msgIdToOss != 0)
		deallocateMessageQueue(msgIdToOss, processName);
}

// Handle any interruption in this function, whether it is an interupttion from the command line or a timer interrup
void handleInterruption(int signo)
{
	if (signo == SIGALRM || signo == SIGINT)
	{
		if (ossLog != NULL)
			fclose(ossLog);

		deallocateAllSharedMessageQueues();
		deallocateAllSharedMemory();

		if (realTimeQueue != NULL)
			free(realTimeQueue);
		if (ioBlockedQueue != NULL)
			free(ioBlockedQueue);
		if (firstPriorityQueue != NULL)
			free(firstPriorityQueue);
		if (secondPriorityQueue != NULL)
			free(secondPriorityQueue);
		if (thirdPriorityQueue != NULL)
			free(thirdPriorityQueue);

		displayStatistics();	

		printf("oss exiting due to signal\n");
		printf("Number of completed processes: %d\n", totalProcessesCompleted);
		kill(0, SIGKILL);	
	}
}

// Check our arguments passed from the command line.  In this case, since we are only accepting the
// -h option from the command line, we only need to return 1 int which indicates if a the help 
// argument was passed.
int checkCommandArgs(int argc, char** argv)
{
	int argFlagHelp = 0;
	int c;
	while ((c = getopt(argc, argv, "h")) != -1)
	{
		switch (c)
		{
			case 'h':
				argFlagHelp = 1;
				break;	
			default:
				writeError("Unrecognized command line argument\n", processName);
				break;
		}
	}
	
	if (argFlagHelp == 1)
	{
		printf("oss (second iteration):\nWhen ran (using the option ./oss), the process oss will fork children and manage their running time using a message queues, which will be used to allot slices of time to child processes.  Statistics will also be returned.\nThere is only one command line option:\n-h Displays this help message\n");
	}
	
	return argFlagHelp;
}

// calculate and display statistics on run time
void displayStatistics()
{
	char statisticsMessage[1024];
	long averageNanoSecondsSleeping = totalSleepingNanoSeconds / totalProcessesCompleted;
	long averageNanoSecondsWaiting = totalWaitingNanoSeconds / totalProcessesCompleted;
	long averageNanoSecondsTurnaround = totalTurnaroundNanoSeconds / totalProcessesCompleted;

	long averageSecondsSleeping = averageNanoSecondsSleeping / NANO_PER_SECOND;
	if (averageSecondsSleeping < 0)
		averageSecondsSleeping = 0;
	else
		averageNanoSecondsSleeping = averageNanoSecondsSleeping - (averageSecondsSleeping * NANO_PER_SECOND);

	long averageSecondsWaiting = averageNanoSecondsWaiting / NANO_PER_SECOND;
	if (averageSecondsWaiting < 0)
		averageSecondsWaiting = 0;
	else
		averageNanoSecondsWaiting = averageNanoSecondsWaiting - (averageSecondsWaiting * NANO_PER_SECOND);

	long averageSecondsTurnaround = averageNanoSecondsTurnaround / NANO_PER_SECOND;
	if (averageNanoSecondsTurnaround  < 0)
		averageSecondsTurnaround = 0;
	else
		averageNanoSecondsTurnaround = averageNanoSecondsTurnaround - (averageSecondsTurnaround * NANO_PER_SECOND);

	long totalSecondsIdle = totalIdleNanoSeconds / NANO_PER_SECOND;
	if (totalSecondsIdle < 0)
		totalSecondsIdle = 0;
	else
		totalIdleNanoSeconds = totalIdleNanoSeconds - (totalSecondsIdle * NANO_PER_SECOND);

	snprintf(statisticsMessage, 1024, "OSS: Final statistics\nAverage sleeping time: Seconds %d NanoSeconds %d\nAverage seconds waiting: Seconds %d NanoSeconds %d\nAverage turnaroundtime: Seconds %d NanoSeconds %d\nTotal idle time: Seconds %d NanoSeconds %d\n", 
		averageSecondsSleeping, averageNanoSecondsSleeping,
		averageSecondsWaiting, averageNanoSecondsWaiting,
		averageSecondsTurnaround, averageNanoSecondsTurnaround,
		totalSecondsIdle, totalIdleNanoSeconds);
	
	printf("Final statistics:\n%s\n", statisticsMessage);

	if (ossLog != NULL)
        {
        	fprintf(ossLog, "%s", statisticsMessage);
        }
}
