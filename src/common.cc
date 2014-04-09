
#include "common.h"
#include <fcntl.h>
#include <string.h>

std::vector <std::string> Tokenize(const std::string & str, const std::string & delimiters) {
	std::vector <std::string> tokens;
	std::string::size_type pos = str.find(delimiters);
	int start_pos = 0;

	while (std::string::npos != pos) {
		tokens.push_back(str.substr(start_pos, pos-start_pos));
		start_pos = pos + delimiters.size();
		pos     = str.find(delimiters,start_pos);
	}
	tokens.push_back(str.substr(start_pos));
	return tokens;
}

int setNonblocking(int fd) {
	int flags;
	/* If they have O_NONBLOCK, use the Posix way to do it */
#if defined(O_NONBLOCK)
	/* Fixme: O_NONBLOCK is defined but broken on SunOS 4.1.x and AIX 3.2.5. */
	if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
		flags = 0;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
	/* Otherwise, use the old way of doing it */
	flags = 1;
	return ioctl(fd, FIOBIO, &flags);
#endif
}

char hex2char(const char * i) {
	char c1, c2;
	if      (i[0] >= '0' && i[0] <= '9') c1 = i[0]-'0';
	else if (i[0] >= 'a' && i[0] <= 'f') c1 = i[0]-'a'+10;
	else                                 c1 = i[0]-'A'+10;
		
	if      (i[1] >= '0' && i[1] <= '9') c2 = i[1]-'0';
	else if (i[1] >= 'a' && i[1] <= 'f') c2 = i[1]-'a'+10;
	else                                 c2 = i[1]-'A'+10;
	
	return c1*16+c2;
}
int ishexpair(const char * i) {
	if (!(	(i[0] >= '0' && i[0] <= '9') ||
		(i[0] >= 'a' && i[0] <= 'f') ||
		(i[0] >= 'A' && i[0] <= 'F') ))
		return 0;
	if (!(	(i[1] >= '0' && i[1] <= '9') ||
		(i[1] >= 'a' && i[1] <= 'f') ||
		(i[1] >= 'A' && i[1] <= 'F') ))
		return 0;
	return 1;
}



void urldecode (char * dest, const char *url) {
	int s = 0, d = 0;
	int url_len = strlen (url) + 1;

	while (s < url_len) {
		char c = url[s++];

		if (c == '%' && s + 2 < url_len) {
			if (ishexpair(&url[s]))
				dest[d++] = hex2char(&url[s]);
			else {
				dest[d++] = c;
				dest[d++] = url[s+0];
				dest[d++] = url[s+1];
			}
			s += 2;
		}
		else if (c == '+') {
			dest[d++] = ' ';
		}
		else {
			dest[d++] = c;
		}
	}
}

std::string urldecode (std::string url) {
	char temp[url.size()+1];
	urldecode(temp,url.c_str());
	return std::string(temp);
}


pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
std::vector <t_job_queue> queues;
int global_stop = 0;


