// QueueUtilities.c
// CS 4760 Project 4
// Alex Kane 3/22/2018
// Utility functions for carrying out queue operations on an array
#include<unistd.h>
#include<sys/types.h>
#include"QueueUtilities.h"

void InitializeQueue(pid_t* queue, int size)
{
	int i;
	for (i = 0; i < size; ++i)
		queue[i] = 0;
}

pid_t DequeueValue(pid_t* queue, int size)
{
	pid_t firstProcess = queue[0];

	if (firstProcess != 0)
	{
		// copy each value forward one spot
		int i;
		for (i = 1; i < size; ++i)
			queue[i - 1] = queue[i];

		// after copying every value up, last entry needs to be manually set
		queue[size - 1] = 0;
	}

	return firstProcess;
}

void EnqueueValue(pid_t* queue, pid_t pid, int size)
{
	int i = 0;
	for (i = 0; i < size && queue[i] != 0; ++i) ;

	queue[i] = pid;
}
