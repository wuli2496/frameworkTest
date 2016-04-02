#ifndef __PROCESS_H__
#define __PROCESS_H__

#include <sys/types.h>

class Process
{
	public:
		Process();
		
	public:
		pid_t m_pid;
		int m_pipefd[2];	
};

#endif
