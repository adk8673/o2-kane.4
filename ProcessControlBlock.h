// ProcessControlBlock.h
// CS 4760 Project 4
// Alex Kane 3/22/2018
// Declaration of ProcessControlBlock struct which is used to store information about
// child processes of oss.  Declared so that it can be included in both user and oss
// easily
#ifndef PROCESS_CONTROL_BLOCK_H
#define PROCESS_CONTROL_BLOCK_H

struct ProcessControlBlock {
	// Id of process - will actually be the hoare server's unix pid
	pid_t ProcessId;
	
	// 1 If process is realtime and must be scheduled round robin, 0 if not
	int RealTime;

	// Seconds process was created at
	int CreatedAtSeconds;

	// NanoSeconds process was created at
	int CreatedAtNanoSeconds;
		
	// 1 IF blocked on IO
	int IOBlocked;

	// Blocked At seconds
	int BlockedAtSeconds;
	
	// Bloecked At Nanoseconds
	int BlockedAtNanoSeconds;

	// Seconds to be blocked until
	int BlockedSeconds;

	// Nanoseconds to be blocked until
	int BlockedNanoSeconds;

	// Queue number
	int QueueNumber;

	// Last scheduled seconds
	int LastScheduledSeconds;

	// Last scheduled nanoseconds
	int LastScheduledNanoSeconds;

	// Total nano seconds waiting
	long NanoSecondsWaiting;

	// Total time sleeping
	long NanoSecondsSleeping;
};

#endif
