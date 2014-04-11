
#include <iostream>
#include <vector>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "common.h"
#include "backend.h"

pid_t wait_timeout(int * s) {
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

/*
 * Thread responsible for waiting for jobs to finish
 * and to spawn new jobs if any empty slot.
**/
void * scheduler_thread(void * args) {
	int online_jobs = 0;
	int * stop = (int*)args;
	while (! (*stop)) {
		// Check and add spawn new jobs
		do {
			pthread_mutex_lock(&queue_mutex);
			for (unsigned int i = 0; i < queues.size(); i++) {
				while (queues[i].running.size() < queues[i].max_running) {
					t_job j;
					if (!get_next_job(queues[i].id, &j))
						break; // No more jobs for this queue

					spawn_job(&j);
					queues[i].running.push_back(j);
					online_jobs++;
					
					set_job_status(j.id, 1);
				}
			}
			pthread_mutex_unlock(&queue_mutex);
			
			if (online_jobs == 0)
				sleep(2);
			
		} while (online_jobs == 0);

		// Wait for a job to finish
		int s;
		pid_t f = wait_timeout(&s);

		// Mark job as done
		pthread_mutex_lock(&queue_mutex);
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
					online_jobs--;
				}
			}
		}
		pthread_mutex_unlock(&queue_mutex);
	}
}

// Destroy a queue: kill all running programs, close all FD and remove it!
bool delete_queue(unsigned long long qid) {
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
		delete_queue_backend(queues[qidx].id);
		queues.erase(queues.begin() + qidx);
	}
	pthread_mutex_unlock(&queue_mutex);
	return true;
}


