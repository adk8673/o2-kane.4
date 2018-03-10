#include<unistd.h>
#include<sys/types.h>
#include<sys/ipc.h>
#include<sys/shm.h>
#include<sys/msg.h>
#include"ErrorLogging.h"
#include"IPCUtilities.h"
#include"ProcessControlBlock.h"

#define MAX_PROCESSES 18
#define ID_SECONDS 1
#define ID_NANO_SECONDS 2
#define ID_PCB 3

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

// To save time, instead of passing argv[0] to each function, store it here
const char* processName = NULL;


// Function prototypes
void allocateAllSharedMemory();
void deallocateAllSharedMemory();

int main(int argc, char** argv)
{
	processName = argv[0];
	
	printf("Begin execution of oss\n");
	
	// Allocate and attach our shared memory
	allocateAllSharedMemory();
	
	*seconds = 0;
	*nanoSeconds = 0;

	
	

	// We're done, should be able to deallocate shared memory safely	
	deallocateAllSharedMemory();

	return 0;
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
