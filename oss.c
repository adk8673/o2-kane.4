#include<unistd.h>
#include<sys/types.h>
#include<sys/ipc.h>
#include<sys/shm.h>
#include<sys/msg.h>
#include"ErrorLogging.h"
#include"IPCUtilities.h"
#include"ProcessControlBlock.h"
#include"ProcessUtilities.h"

#define MAX_PROCESSES 18
#define ID_SECONDS 1
#define ID_NANO_SECONDS 2
#define ID_PCB 3
#define ID_MSG_TO 4
#define ID_MSG_FROM 5

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

typedef struct {
	long mtype;
	char mtext[300];
} mymsg_t;

// Function prototypes
void allocateAllSharedMemory();
void deallocateAllSharedMemory();
void allocateAllSharedMessageQueues();
void deallocateAllSharedMessageQueues();
void excuteOss();

int main(int argc, char** argv)
{
	processName = argv[0];
	
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

	// Main logic	
	executeOss();

	int status;
	pid_t childpid;
	while((childpid = wait(&status)) > 0);
	
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
	pid_t newChild = createChildProcess("./user", processName);
	pid_t secondChild = createChildProcess("./user", processName);

	mymsg_t toChildMsg;
	toChildMsg.mtype = newChild;
	char test[500] = "TEST";
	strcpy(toChildMsg.mtext, test);
	int bytesRead;

	if (msgsnd(msgIdToChild, &toChildMsg, sizeof(toChildMsg), 0) == -1)
		writeError("Failed when sending message to child\n", processName);

printf("Sent message to child\n");

	mymsg_t fromChildMsg;
	if ((bytesRead = msgrcv(msgIdToOss, &fromChildMsg, sizeof(fromChildMsg), 1, 0)) == -1)
		writeError("Failed when receiving message message from child\n", processName);

	toChildMsg.mtype = secondChild;
	test[500] = "TEST2";
	strcpy(toChildMsg.mtext, test);

	if (msgsnd(msgIdToChild, &toChildMsg, sizeof(toChildMsg), 0) == -1)
		writeError("Failed when sending message to child\n", processName);
	printf("Sent message to child\n");

	if ((bytesRead = msgrcv(msgIdToOss, &fromChildMsg, sizeof(fromChildMsg), 1, 0)) == -1)
		writeError("Failed when receiving message message from child\n", processName);

	printf("Read from child mtype: %d  mtext: %s\n", fromChildMsg.mtype, fromChildMsg.mtext);
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

