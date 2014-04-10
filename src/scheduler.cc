
#include <iostream>
#include <vector>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "common.h"
#include "backend.h"

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
		pid_t f = wait(&s);

		// Mark job as done
		pthread_mutex_lock(&queue_mutex);
		for (unsigned int i = 0; i < queues.size(); i++) {
			for (unsigned int j = 0; j < queues[i].running.size(); j++) {
				if (queues[i].running[j].pid == f) {
					queues[i].running[j].pfinished = 1;
					break;
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


