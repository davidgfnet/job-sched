
#include <poll.h>
#include <iostream>
#include <vector>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>

#define IOTRY_AGAIN(ret) (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))

static struct pollfd mkp(int fd, short e) {
	struct pollfd p;
	p.fd = fd;
	p.events = e;
	return p;
}

static std::string realpath(const std::string & path) {
	char * r = realpath(path.c_str(),0);
	std::string res(r);
	free(r);
	return res;
}

// Converts the path from rel to abs in case it's a rel path
static std::string fullpath(const std::string & path) {
	if (path.size() > 0 and path[0] != '/') {
		char temp[8*1024];
		getcwd(temp, sizeof(temp));
		std::string cwd = std::string(temp);
		cwd = realpath(cwd);
		
		return cwd + "/" + path;
	}
	return path;
}


struct t_job {
	unsigned long long id;    // Job ID
	pid_t pid;                // Spawned process ID
	int pstdout, pfile;       // Pipe from process and to output file
	std::string commandline;  // Commandline to execute
	std::string output;       // File to output the process stdout
	std::string environ;      // Environment variables
	int dbuffer;              // Intermediate buffer size
	char buffer[16*1024];     // Intermediate buffer
	char pfinished;           // Process finished, waiting for final IO
};
struct t_queued_job {
	unsigned long long id;    // Job ID
	unsigned long long qid;   // Queue ID
	std::string commandline;  // Commandline to execute
	std::string output;       // File to output the process stdout
	std::string environ;      // Environment variables
	int status;               // Job status
	unsigned long long dateq; // Date queued
	unsigned long long dater; // Date running
};

struct t_job_queue {
	unsigned long long id;
	std::string friendly_name;
	int max_running;
	std::vector < t_job > running;
};

int setNonblocking(int fd);
std::vector <std::string> Tokenize(const std::string & str, const std::string & delimiters);

extern pthread_mutex_t queue_mutex;
extern std::vector <t_job_queue> queues;;
extern int global_stop;

void * scheduler_thread(void * args);
void * output_thread(void * args);
int get_next_job (unsigned long long qid, t_job * j);
void spawn_job (t_job * j);
void kill_job(t_job * j);
void job_finished(t_job * j);
bool delete_queue(unsigned long long qid, bool truncate);
void * scheduler_thread(void * args);
void * output_thread(void * args);
void serve_queries();
void serve_queries(int port, bool anyip);
std::string urldecode (std::string url);

extern volatile int force_scheduling;
bool edit_queue(unsigned long long qid, int max_run);


