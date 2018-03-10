#include<sys/ipc.h>
#include<sys/shm.h>
#include<sys/msg.h>

key_t getKey(int id)
{
        key_t key = -1;
        char cwd[1024];
        getcwd(cwd, sizeof(cwd));
        key = ftok(cwd, id);
        return key;
}

int allocateSharedMemory(int id, const char* processName)
{
	const int memFlags = (0777 | IPC_CREAT);	
	int shmid = 0;
	key_t key = getKey(id);
	if ((shmid = shmget(key, sizeof(int), memFlags)) == -1)
	{
		writeError("Failed to allocated shared memory for key", processName);
	}

	return shmid;
}	

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
void deallocateMessageQueue(int msgID, const char* processName)
{
	if(msgctl(msgID, IPC_RMID, NULL) == -1)
		writeError("Failed to deallocate message queue", processName);
}
