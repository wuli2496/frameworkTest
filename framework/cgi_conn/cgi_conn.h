#ifndef __CGI_CONN_H__
#define __CGI__CONN_H__

class cgi_conn
{
	public:
		cgi_conn(){}
		~cgi_conn(){}
		
		void init(int epollfd, int sock, sockaddr_in addr)
		{
			m_epollfd = epollfd;
			m_sockfd = sock;
			m_addr = addr;
			memset(m_buff, 0, sizeof(m_buff));
			m_read_idx = 0;
		}

		void process()
		{
			int idx = -1;
			int ret = -1;

			while (true)
			{
				idx = m_read_idx;
				ret = recv(m_sockfd, m_buff + idx, BUFFER_SIZE - 1 - idx, 0);
				if (ret < 0) 
				{
					if (errno != EAGAIN)
					{
						delfd(m_epollfd, m_sockfd);
					}
					break;
				}
				else if (0 == ret)
				{
					break;
				}
				else 
				{
					m_read_idx += ret;
					printf("user content:%s\n", m_buff);
					for (; idx < m_read_idx; idx++)
					{
						if (idx >= 1 && '\r' == m_buff[idx - 1] && '\n' == m_buff[idx])
						break;
					}

					if (idx == m_read_idx) continue;
					m_buff[idx - 1] = 0;
					char *filename = m_buff;
					if (access(filename, F_OK) == -1) 
					{
						delfd(m_epollfd, m_sockfd);
						break;
					}

					ret = fork();
					if (ret < 0)
					{
						delfd(m_epollfd, m_sockfd);
						break;
					}
					else if (ret > 0)
					{
						delfd(m_epollfd, m_sockfd);
						break;
					}
					else 
					{
						close(STDOUT_FILENO);
						dup(m_sockfd);
						execl(m_buff, m_buff, (char*)0);
						exit(0);
					}
				}
			}
		}
		
	private:
		static const int BUFFER_SIZE = 1024;
		static int m_epollfd;
		int m_sockfd;
		sockaddr_in m_addr;
		char m_buff[BUFFER_SIZE];	
		int m_read_idx;
};

int cgi_conn::m_epollfd = -1;

#endif
