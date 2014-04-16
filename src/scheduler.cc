
#include <iostream>
#include <vector>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "common.h"
#include "backend.h"

volatile int force_scheduling = 0;  // Set this to one in order to force scheduler to run

pid_t wait_timeout_(int * s) {
	sigset_t mask;
	sigset_t orig_mask;
	struct timespec timeout;
	pid_t pid;
 
	sigemptyset (&mask);
	sigaddset (&mask, SIGCHLD);
	sigprocmask(SIG_BLOCK, &mask, &orig_mask);
 
	timeout.tv_sec = 5;
	timeout.tv_nsec = 0;
 
	while (1) {
		if (sigtimedwait(&mask, NULL, &timeout) < 0) {
			if (errno == EINTR) /* Interrupted by a signal other than SIGCHLD. */
				continue;
			else
				return -1;
		}
		break;
	}

	return wait(s);
}

pid_t wait_timeout(int * s) {
	// Wait until we get a child
	// Or if we are forced to stop (a queue or job added)
	int n = 0;
	while (!force_scheduling && n++ < 5) {
		pid_t p = waitpid(-1, s, WNOHANG);
		if (p > 0) return p;  // NOHANG returns zero :(
		sleep(3);
	}
	return -1;
}


/*
 * Thread responsible for waiting for jobs to finish
 * and to spawn new jobs if any empty slot.
**/
void * scheduler_thread(void * args) {
	int * stop = (int*)args;
	while (! (*stop)) {
		// Check and add spawn new jobs
		int online_jobs;
		do {
			pthread_mutex_lock(&queue_mutex);
			online_jobs = 0;
			for (unsigned int i = 0; i < queues.size(); i++) {
				while (queues[i].running.size() < queues[i].max_running) {
					t_job j;
					if (!get_next_job(queues[i].id, &j))
						break; // No more jobs for this queue

					spawn_job(&j);
					queues[i].running.push_back(j);
					
					set_job_status(j.id, 1);
				}
				online_jobs += queues[i].running.size();
			}
			pthread_mutex_unlock(&queue_mutex);
			
			// If no work, just wait ...
			while (online_jobs == 0 && !force_scheduling)
				sleep(3);
			
		} while (online_jobs == 0);

		// Wait for a job to finish
		int s;
		pid_t f = wait_timeout(&s);

		// Mark job as done
		pthread_mutex_lock(&queue_mutex);
		force_scheduling = 0;
		if (f >= 0) {
			for (unsigned int i = 0; i < queues.size(); i++) {
				for (unsigned int j = 0; j < queues[i].running.size(); j++) {
					if (queues[i].running[j].pid == f) {
						queues[i].running[j].pfinished = 1;
						break;
					}
				}
			}
		}
		// Remove jobs as soon as process & IO finishes
		for (unsigned int i = 0; i < queues.size(); i++) {
			for (unsigned int j = 0; j < queues[i].running.size(); j++) {
				if (queues[i].running[j].pfinished && 
					queues[i].running[j].pfile < 0 && 
					queues[i].running[j].pstdout < 0) {
					
					job_finished(&queues[i].running[j]);
					queues[i].running.erase(queues[i].running.begin() + j);
				}
			}
		}
		pthread_mutex_unlock(&queue_mutex);
	}
}

// Destroy a queue: kill all running programs, close all FD and remove it!
bool delete_queue(unsigned long long qid, bool truncate) {
	pthread_mutex_lock(&queue_mutex);
	unsigned int qidx;
	for (qidx = 0; qidx < queues.size(); qidx++) {
		if (queues[qidx].id == qid)
			break;
	}
	if (qidx < queues.size()) {
		for (unsigned int i = 0; i < queues[qidx].running.size(); i++) {
			kill_job(&queues[qidx].running[i]);
			if (queues[qidx].running[i].pfile >= 0)
				close(queues[qidx].running[i].pfile);
			if (queues[qidx].running[i].pstdout >= 0)
				close(queues[qidx].running[i].pstdout);
		}
		backend_delete_queue(queues[qidx].id, truncate);
		if (!truncate)
			queues.erase(queues.begin() + qidx);
	}
	pthread_mutex_unlock(&queue_mutex);
	return true;
}

bool edit_queue(unsigned long long qid, int max_run) {
	pthread_mutex_lock(&queue_mutex);
	bool r = backend_edit_queue(qid, max_run);
	force_scheduling = 1;
	pthread_mutex_unlock(&queue_mutex);
	return r;
}



