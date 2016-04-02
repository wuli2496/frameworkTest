#ifndef __PROCESSPOOL_H__
#define __PROCESSPOOL_H__

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>

#include "process.h"

static int setnonblocking(int fd);
static void addfd(int epollfd, int fd);
static void addsig(int signo, void (handle)(int), bool restart = true);
static void sig_handler(int signo);
static void delfd(int epollfd, int fd);

static int sig_pipefd[2];



template <class T>
class ProcessPool
{
	public:
		static ProcessPool<T>* create(int listenfd, int process_number = 8);
		
		~ProcessPool();
		
		void run();
	
	private:
		ProcessPool(int listenfd, int process_number = 8);
		void run_child();
		void run_parent();
		void add_sig_pipe();
		
	private:
		static const int MAX_PROCESS_NUMBER = 16;
		static const int MAX_USER_PER_PROCESS = 65535;
		static const int MAX_EVENTS = 1000;

		Process* m_sub_process;	
		static ProcessPool<T>* m_instance;
		int m_process_number;
		int m_listenfd;
		int m_epollfd;
		int m_idx;
		bool m_stop;
		
};

template <class T>
ProcessPool<T>* ProcessPool<T>::m_instance = NULL;

template <class T>
ProcessPool<T>::ProcessPool(int listenfd, int process_number)
:m_listenfd(listenfd), m_process_number(process_number), m_stop(false), m_idx(-1)
{
	assert(process_number > 0 && process_number <= MAX_PROCESS_NUMBER);

	m_sub_process = new Process[process_number];
	assert(m_sub_process);


	for (int i = 0; i < process_number; i++)
	{
		int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_sub_process[i].m_pipefd);
		
		m_sub_process[i].m_pid = fork();
		assert(m_sub_process[i].m_pid >= 0);
		
		if(m_sub_process[i].m_pid > 0)
		{
			close(m_sub_process[i].m_pipefd[1]);
			continue;
		}
		else 
		{
			close(m_sub_process[i].m_pipefd[0]);
			m_idx = i;
			break;
		}	
	}
}


template<class T>
ProcessPool<T>::~ProcessPool()
{
	delete[] m_sub_process;
}

template<class T>
ProcessPool<T>* ProcessPool<T>::create(int listenfd, int process_number)
{
	if (NULL == m_instance)
	{
		m_instance = new ProcessPool<T>(listenfd, process_number);
	}
	
	return m_instance;
}

template<class T>
void ProcessPool<T>::run()
{
	pid_t pid = getpid();
	printf("pid:%d, m_idx:%d\n", pid, m_idx);
	if (m_idx != -1)
	{
		run_child();
		return;
	}

	run_parent();
}

template<class T>
void ProcessPool<T>::run_child()
{
	add_sig_pipe();

	int pipefd = m_sub_process[m_idx].m_pipefd[1];
	addfd(m_epollfd, pipefd);

	epoll_event events[MAX_EVENTS];
	int number = 0;
	int ret = 0;
	T* users = new T[MAX_USER_PER_PROCESS];
	
	while (!m_stop)
	{
		number = epoll_wait(m_epollfd, events, MAX_EVENTS, -1);
		if (number < 0 && errno == EINTR)
		{
			continue;
		}

		for (int i = 0; i < number; i++)
		{
			int sockfd = events[i].data.fd;
			if (sockfd == pipefd && (events[i].events & EPOLLIN))
			{
				int client;
				ret = recv(sockfd, (char*)&client, sizeof(client), 0);
				if ((ret < 0 && errno == EAGAIN) || 0 == ret) continue;
				else 
				{
					sockaddr_in client_addr;
					socklen_t client_addr_len = sizeof(sockaddr_in);
					int connfd = accept(m_listenfd, (sockaddr*)&client_addr, &client_addr_len);
					if (connfd < 0) 
					{
						printf("errno is %s\n", strerror(errno));
						continue;
					}
					else 
					{
						addfd(m_epollfd, connfd);
						users[connfd].init(m_epollfd, connfd, client_addr);
					}
				}
			}
			else if (sockfd == sig_pipefd[0] && (events[i].events & EPOLLIN))
			{
				char signals[1024];
				ret = recv(sockfd, signals, sizeof(signals), 0);
				if (ret <= 0) continue;

				for (int i = 0; i < ret; i++)
				{
					switch(signals[i])
					{
						case SIGCHLD:
						{
							pid_t pid;
							int stat;
							while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) continue;
							break;
						}
						case SIGTERM:
						case SIGINT:
							m_stop = true;
							break;
						default:
							break;
					}
				}
			}
			else if (events[i].events & EPOLLIN)
			{
				users[sockfd].process();
			}
			else continue;
		}
	}

	delete[] users;
	users = NULL;

	close(pipefd);
	close(m_epollfd);
	
}

template<class T>
void ProcessPool<T>::run_parent()
{
	add_sig_pipe();
	addfd(m_epollfd, m_listenfd);

	int sub_process_counter = 0;
	epoll_event events[MAX_EVENTS];
	int number = 0;
	int ret = -1;
	int new_conn = 1;
	
	while (!m_stop)
	{
		number = epoll_wait(m_epollfd, events, MAX_EVENTS, -1);
		if (number < 0 && errno != EINTR)
		{
			printf("epoll failure\n");
			break;
		}

		printf("number:%d\n", number);
		pid_t pid = getpid();
		printf("pid:%d, m_idx:%d\n", pid, m_idx);
		for (int i = 0; i < number; i++)
		{
			int sockfd = events[i].data.fd;
			if (sockfd == m_listenfd)
			{
				int c = sub_process_counter;
				do 
				{
					if (m_sub_process[c].m_pid != -1) break;

					c = (c + 1) % m_process_number;
				}while (c != sub_process_counter);

				if (-1 == m_sub_process[c].m_pid)
				{
					m_stop = true;
					break;
				}

				sub_process_counter = (c + 1) % m_process_number;
				send(m_sub_process[c].m_pipefd[0], (char*)&new_conn, sizeof(new_conn), 0);
				printf("send to child %d\n", c);
			}
			else if (sockfd == sig_pipefd[0])
			{
				char signals[1024];
				ret = recv(sig_pipefd[0], signals, sizeof(signals), 0);
				if (ret <= 0) continue;

				for (int i = 0; i < ret; i++)
				{
					switch(signals[i])
					{
						case SIGCHLD:
						{
							pid_t pid;
							int stat;

							while ((pid = waitpid(-1, &stat, WNOHANG)) > 0)
							{
								for (int i = 0; i < m_process_number; i++)
								{
									if (m_sub_process[i].m_pid == pid)
									{
										printf("child %i join\n", i);
										m_sub_process[i].m_pid = -1;
										close(m_sub_process[i].m_pipefd[0]);
									}
								}
							}

							m_stop = true;
							for (int i = 0; i < m_process_number; i++)
							{
								if (m_sub_process[i].m_pid != -1) m_stop = false;
							}
							break;
						}
						case SIGTERM:
						case SIGINT:
						{
							printf("kill all child\n");
							for (int i = 0; i < m_process_number; i++)
							{
								if (m_sub_process[i].m_pid != -1)
								{
									kill(m_sub_process[i].m_pid, SIGTERM);
								}
							}
							break;
						}
						default:
							break;
					}
				}
			}
			else continue;
		}
	}

	close(m_epollfd);
}


template<class T>
void ProcessPool<T>::add_sig_pipe()
{
	m_epollfd = epoll_create(5);
	assert(m_epollfd != -1);

	int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipefd);
	assert(ret != -1);

	setnonblocking(sig_pipefd[1]);
	addfd(m_epollfd, sig_pipefd[0]);

	addsig(SIGCHLD, sig_handler);
	addsig(SIGTERM, sig_handler);
	addsig(SIGINT, sig_handler);
	addsig(SIGPIPE, SIG_IGN);
	
}

int setnonblocking(int fd)
{
	int old_option = fcntl(fd, F_GETFL);
	int new_option = old_option | O_NONBLOCK;
	fcntl(fd, F_SETFL, new_option);
	
	return old_option;
}


void addfd(int epollfd, int fd)
{
	epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLET;

	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
	setnonblocking(fd);
}

void addsig(int signo, void (handle)(int), bool restart)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handle;

	if (restart)
	{
		sa.sa_flags |= SA_RESTART;
	}
	
	sigfillset(&sa.sa_mask);
	assert(sigaction(signo, &sa, NULL) != -1);
}

void sig_handler(int signo)
{
	int msg = signo;
	
	send(sig_pipefd[1], (char*)&msg, sizeof(msg), 0);
}

void delfd(int epollfd, int fd)
{
	epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
	close(fd);
}

#endif
