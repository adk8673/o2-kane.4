#ifndef QUEUE_UTILITIES_H
#define QUEUE_UTILITIES_H

void InitializeQueue( pid_t *, int );
pid_t DequeueValue(pid_t *, int );
void EnqueueValue(pid_t *, pid_t , int );

#endif
