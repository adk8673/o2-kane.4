#include<unistd.h>
#include<errno.h>
#include<sys/types.h>
#include"ProcessUtilities.h"

// Create a child process by forking a child and executing the target process
// using command line arguements passed
pid_t createChildProcess(const char* targetProgram, const char* processName)
{
	pid_t forkedPid = fork();

	// see if fork is child or parent
	// also check for errors in forking
	if (forkedPid == 0)
	{
		// If we get here, we are inside of child process, begin parsing and our target program
		// and args into a command line
		char** args;
		int execResults;
		int numItems  = makeargv(targetProgram, " ", &args);
		execResults = execvp(args[0], args);
		if (execResults == -1)
		{
			printf("%d\n", errno);
			writeError("Failed to exec targetProcess", processName);
		}
	}
	else if (forkedPid < 0)
	{
		writeError("Failed to fork process", processName);
	}

	return forkedPid;	
}

// Function taken from Robbins textbook
int makeargv(const char *s, const char *delimiters, char ***argvp)
{
	int error;
	int i;
	int numtokens;
	const char *snew;
	char* t;

	if ((s == NULL) || (delimiters == NULL) || (argvp == NULL))
	{
		errno = EINVAL;
		return -1;
	}
	
	*argvp = NULL;
	snew = s + strspn(s, delimiters);
	if ((t = malloc(strlen(snew) + 1 )) == NULL)
		return -1;
	strcpy(t, snew);
	numtokens = 0;
	if (strtok(t, delimiters) != NULL)
		for (numtokens = 1; strtok(NULL, delimiters) != NULL; numtokens++) ;

	if ((*argvp = malloc((numtokens + 1) * sizeof(char *))) == NULL)
	{
		error = errno;
		free(t);
		errno = error;
		return -1;
	}
	
	if (numtokens == 0)
		free(t);
	else 
	{
		strcpy(t, snew);
		**argvp = strtok(t, delimiters);
		for (i = 1; i < numtokens; i++)
			*((*argvp) + i) = strtok(NULL, delimiters);
	}

	*((*argvp) +  numtokens) = NULL;
	return numtokens;
}
