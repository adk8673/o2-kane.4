#include<unistd.h>
#include<sys/ipc.h>
#include<sys/shm.h>
#include<sys/msg.h>
#include<sys/stat.h>
#include"ErrorLogging.h"

// This is mostly used internally to parse the passed id into a key value that should be reproducible
// in other executables in the same directory, which can then be used to get IPC resources
key_t getKey(int id)
{
        key_t key = -1;
        char cwd[1024];
        getcwd(cwd, sizeof(cwd));
        key = ftok(cwd, id);
        return key;
}


// Allocate a shared memory segment using IPC_CREAT using the passed ID to get a key value and return the shmid
// or -1 if unsucessful
int allocateSharedMemory(int id, int sizeInBytes, const char* processName)
{
	const int memFlags = (0777 | IPC_CREAT);	
	int shmid = 0;
	key_t key = getKey(id);
	if ((shmid = shmget(key, sizeInBytes, memFlags)) == -1)
	{
		writeError("Failed to allocated shared memory for key", processName);
	}

	return shmid;
}	

// Allocate a shared queue object and return the msgID or -1 if it fails, using the passed ID value to get a key
int allocateMessageQueue(int id, const char* processName)
{
	const int msgFlags = (S_IRUSR | S_IWUSR  | IPC_CREAT);
	int msgid = 0;
	key_t key = getKey(id);
	if ((msgid = msgget(key, msgFlags)) == -1)
	{
		writeError("Failed to allocate message queue for key", processName);
	}

	return msgid;
}

// Get the shmid of an existing memory queue created with the passed ID
int getExistingMessageQueue(int id, const char* processName)
{
	const int msgFlags = (S_IRUSR | S_IWUSR);
	int msgid = 0;
	key_t key = getKey(id);
	if ((msgid = msgget(key, msgFlags)) == -1)
	{
		writeError("Failed to get existing message queue for key", processName);
	}
	
	return msgid;
}

// Deallocate a shared message queue with the passe msgid
void deallocateMessageQueue(int msgID, const char* processName)
{
	if(msgctl(msgID, IPC_RMID, NULL) == -1)
		writeError("Failed to deallocate message queue", processName);
}

// Get a pointer to the existing memory segment with the given ID
void* getExistingSharedMemory(int id, const char* processName)
{
	key_t key = getKey(id);
	void* pResult = NULL;
	int shmid = -1;
	if ((shmid = shmget(key, 0, 0777)) == -1)
	{
		writeError("Failed to get SHMID of existing memory", processName);
	}
	else
	{
		pResult = shmat(shmid, 0, 0);
		if ((int)pResult == -1)
		{
			pResult = NULL;
			writeError("Failed to map existing shared memory to local address space", processName);
		}
	}
	
	return pResult;
}

// Deallocate the shared memory segment witht he given ID
void deallocateSharedMemory(int shmid, const char* processName)
{
	if (shmctl(shmid, IPC_RMID, NULL) == -1)
		writeError("Failed to deallocated shared memory for key", processName);
}
