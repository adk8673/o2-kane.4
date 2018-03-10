#ifndef PROCESS_UTILITIES_H
#define PROCESS_UTILITIES_H

pid_t createChildProcess( const char* , const char* );
int makeargv( const char * , const char *delimiters, char ***);

#endif
