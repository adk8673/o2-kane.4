// PeriodicTimer.c
// CS 4760 Project 4
// Alex Kane 3/22/2018
// Function from robbins textbook which will send SIGALRM to process 
// from which it is called after a set time
#include<signal.h>
#include<time.h>
#include"PeriodicTimer.h"

#define BILLION 1000000000L

// function taken from Robbins textbook
int setPeriodic(double sec)
{
	time_t timerid;
	struct itimerspec value;

	if (timer_create(CLOCK_REALTIME, NULL, &timerid) == -1)
		return -1;

	value.it_interval.tv_sec = (long)sec;
	value.it_interval.tv_nsec = (sec- value.it_interval.tv_sec) * BILLION;
	if (value.it_interval.tv_nsec >= BILLION)
	{
		value.it_interval.tv_sec++;
		value.it_interval.tv_nsec -= BILLION;
	}	
	
	value.it_value = value.it_interval;
	return timer_settime(timerid, 0, &value, NULL);
}
