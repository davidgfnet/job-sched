
#include <iostream>
#include <vector>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "common.h"
#include "backend.h"

// Return the next job for a queue with qid
int get_next_job (unsigned long long qid, t_job * j) {
	std::vector < t_queued_job > jobs = get_waiting_jobs(qid, 1);
	if (jobs.size() == 0) return 0;
	
	j->id = jobs[0].id;
	j->pstdout = -1;
	j->pfile   = -1;
	j->dbuffer = 0;
	j->pfinished = 0;
	
	j->commandline = jobs[0].commandline;
	j->output = "";
	
	return 1;
}

void spawn_job (t_job * j) {
	// Create pipes for stdout
	int pipes[2];
	pipe(pipes);
	
	// Create a nex process
	pid_t pid = fork();
	if (pid) {
		// Save the fd and return
		j->pid = pid;
		close(pipes[1]);
		j->pstdout = pipes[0];
		setNonblocking(j->pstdout);
		
		// Open file for stdout logging
		j->pfile = open(j->output.c_str(), O_WRONLY | O_TRUNC);
	}else{
		// Move stdout to our pipe
		close(pipes[0]);
		dup2(1, pipes[1]);
		dup2(2, pipes[1]);
		
		// Execute the command
		std::vector <std::string> args = Tokenize(j->commandline, " ");
		const char * path = args[0].c_str();
		const char * ar[args.size()];
		for (unsigned int i = 0; i < args.size()-1; i++)
			ar[i] = args[i+1].c_str();
		ar[args.size()-1] = 0;
		
		execvp (path, (char* const*)ar);
	}
}

void job_finished(t_job * j) {
	set_job_status(j->id, 2);
}


