// ErrorLogging.c
// CS 4760 Project 4
// Alex Kane 3/22/2018
// Error logging function(s)

#include"ErrorLogging.h"

// Using perror, write information about errors to stderr
void writeError(const char* errorMessage, const char* processName)
{
	char message[1024];
	
	snprintf(message, sizeof(message), "%s: Error: %s", processName, errorMessage);
	
	perror(message);
}
