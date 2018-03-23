// IPCUtilities.h
// CS 4760 Project 4
// Alex Kane 3/22/2018
// IPC Wrapper functions declarations
#ifndef IPCUTILITIES_H
#define IPCUTILITIES_H

// This is mostly used internally to parse the passed id into a key value that should be reproducible
// in other executables in the same directory, which can then be used to get IPC resources
key_t getKey(int);

// Allocate a shared memory segment using IPC_CREAT using the passed ID to get a key value and return the shmid
// or -1 if unsucessful
int allocateSharedMemory(int, int, const char*);
	
// Allocate a shared queue object and return the msgID or -1 if it fails, using the passed ID value to get a key
int allocateMessageQueue(int, const char*);

// Get the shmid of an existing memory queue created with the passed ID
int getExistingMessageQueue(int, const char*);

// Deallocate a shared message queue with the passe msgid
void deallocateMessageQueue(int, const char*);

// Get a pointer to the existing memory segment with the given ID
void* getExistingSharedMemory(int, const char*);

// Deallocate the shared memory segment witht he given ID
void deallocateSharedMemory(int, const char*);

#endif
