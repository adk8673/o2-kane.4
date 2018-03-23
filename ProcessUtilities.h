// ProcessUtilities.h
// CS 4760 Project 4
// Alex Kane 3/22/2018
// Declarations of process utility functions
#ifndef PROCESS_UTILITIES_H
#define PROCESS_UTILITIES_H

pid_t createChildProcess( const char* , const char* );
int makeargv( const char * , const char *delimiters, char ***);

#endif
