#include<unistd.h>
#include<stdlib.h>
#include<stdio.h>
#include<sys/types.h>
#include<sys/ipc.h>
#include<sys/shm.h>
#include<sys/msg.h>
#include<time.h>
#include"ErrorLogging.h"
#include"IPCUtilities.h"
#include"ProcessControlBlock.h"

#define MAX_PROCESSES 18
#define ID_SECONDS 1
#define ID_NANO_SECONDS 2
#define ID_PCB 3
#define ID_MSG_TO 4
#define ID_MSG_FROM 5
#define MAX_PERCENT 100
#define IO_SECONDS 2
#define IO_NANO_SECONDS 100000

// Global variable definitions
// Probably not the cleanest to have these global - but I'm not sure of a better way
// since we may need to deallocate these outside of main if we get a signal causing 
// us to cancel

// Pointer to shared global process control block
// will be stored in shared memory
struct ProcessControlBlock* pcb = NULL;

// Pointer to shared global seconds integer
int* seconds = NULL;

// Pointer to shared memory global nanoseconds integer
int* nanoSeconds = NULL;

// MsgID of queue to send messages to child
int msgIdToChild = 0;

// MsgID of queue to send responses back to oss
int msgIdToOss = 0;

// To save time, instead of passing argv[0] to each function, store it here
const char* processName = NULL;

typedef struct {
	long mtype;
	char mtext[50];
} mymsg_t;

// Function declarations
void attachToSharedMemory();
void getMessageQueues();
void dettachSharedMemory();
void executeChild();

int main(int argc, char** argv)
{
	srand(time(NULL) * getpid());
	processName = argv[0];

		
	// Get access to our shared memory
	attachToSharedMemory();
	
	// Get ids of our message queues
	getMessageQueues();

	printf("Child process %d has been initialized\n", getpid());
	executeChild();

	// Deattach from our shared memory
	dettachSharedMemory();
	return 0;
}

void executeChild()
{
	pid_t thisPid = getpid();

	// Child can't execute unless it has received a message from oss
	mymsg_t fromMsg, returnMsg;
	int bytesRead;
	
	int index;
	for (index = 0; index < MAX_PROCESSES; ++index)
	{
		if(pcb[index].ProcessId == thisPid)
			break;
	}

	int finished = 0;	
	while (!finished)
	{
		if ( (bytesRead = msgrcv(msgIdToChild, &fromMsg, sizeof(fromMsg), thisPid, 0)) == -1 )
			writeError("Failed to read message from oss to child\n", processName);
		
		printf("Child with pid %d is running\n", getpid());

		int timeQuantum = atoi(fromMsg.mtext);
		
		// first, check to see if this process should finish
		if ((rand() % MAX_PERCENT) < 30)
		{
			int usedQuantum = rand() % timeQuantum;
			snprintf(returnMsg.mtext, 50, "%d", usedQuantum);
			finished = 1;
		}
		// then, see if this user process was blocked on IO
		else if ((rand() % MAX_PERCENT) < 10)
		{
			int usedQuantum = (rand() % timeQuantum) * -1;
			snprintf(returnMsg.mtext, 50, "%d", usedQuantum);

			// Need to determine when the event will occur
			pcb[index].IOBlocked = 1;

			pcb[index].BlockedSeconds = *seconds + IO_SECONDS;
			pcb[index].BlockedNanoSeconds = *nanoSeconds + IO_NANO_SECONDS;
		}
		else
		{
			strcpy(returnMsg.mtext, "0");
		}	
		
		returnMsg.mtype = 1;
		if( msgsnd(msgIdToOss, &returnMsg, sizeof(returnMsg), 0) == -1 )
			writeError("Failed to send message to parent\n", processName);
	}		
}

void attachToSharedMemory()
{
	if((seconds = (int*)getExistingSharedMemory(ID_SECONDS, processName)) == -1)
		writeError("Failed to attach child to seconds shared memory\n", processName);

	if ((nanoSeconds = (int*)getExistingSharedMemory(ID_NANO_SECONDS, processName)) == -1)
		writeError("Failed to attach child to nano seconds shared memory\n", processName);
	
	if ((pcb = (struct ProcessControlBlock*)getExistingSharedMemory(ID_PCB, processName)) == -1)
		writeError("Failed to attach child to process table shared memory\n", processName);
}

void getMessageQueues()
{
	msgIdToChild = getExistingMessageQueue(ID_MSG_TO, processName);
	msgIdToOss = getExistingMessageQueue(ID_MSG_FROM, processName);
}

void dettachSharedMemory()
{
	if ( shmdt(seconds) == -1 )
		writeError("Failed to dettach seconds from child\n", processName);
	if ( shmdt(nanoSeconds) == -1 )
		writeError("Failed to dettach nano seconds from child\n", processName);
	if ( shmdt(pcb) == -1 )
		writeError("Failed to dettach process contro ltable from child\n", processName);
}
