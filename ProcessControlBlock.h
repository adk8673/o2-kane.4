#ifndef PROCESS_CONTROL_BLOCK_H
#define PROCESS_CONTROL_BLOCK_H

struct ProcessControlBlock {
	// Id of process - will actually be the hoare server's unix pid
	pid_t ProcessId;
	
	// 1 If process is realtime and must be scheduled round robin, 0 if not
	int RealTime;
};
#endif
