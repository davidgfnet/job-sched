
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <vector>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "common.h"

// Iterates for each job and reads their stdout
void * output_thread(void * args) {
	int * stop = (int*)args;
	while (! (*stop)) {
		// Setup poll structure for jobs
		std::vector <struct pollfd> pfd;
		int need_write = 0;
		do {
			pthread_mutex_lock(&queue_mutex);
			for (unsigned int i = 0; i < queues.size(); i++) {
				for (unsigned int j = 0; j < queues[i].running.size(); j++) {
					if (queues[i].running[j].pstdout >= 0) {
						struct pollfd p;
						p.fd = queues[i].running[j].pstdout;
						p.events = POLLIN;
						pfd.push_back(p);
					}
					if (queues[i].running[j].pfile >= 0 && queues[i].running[j].dbuffer > 0)
						need_write = 1;
				}
			}
			pthread_mutex_unlock(&queue_mutex);
			
			if (pfd.size() == 0)
				sleep(2);
			else
				poll(&pfd[0], pfd.size(), 10000);
			
		} while (pfd.size() == 0 && !need_write);

		// Try to read/write from/to each open FD
		pthread_mutex_lock(&queue_mutex);
		for (unsigned int i = 0; i < queues.size(); i++) {
			for (unsigned int j = 0; j < queues[i].running.size(); j++) {
				t_job * job = &queues[i].running[j];
				if (job->pstdout >= 0 and job->dbuffer < sizeof(job->buffer)) {
					int r = read(job->pstdout, &job->buffer[job->dbuffer], sizeof(job->buffer)-job->dbuffer);
					if (r > 0){
						job->dbuffer += r;
						printf("Read!!\n");
					}
					else if (r == 0 || !IOTRY_AGAIN(r)) {
						close(job->pstdout);
						job->pstdout = -1;
					}
				}
				if (job->pfile >= 0 and job->dbuffer > 0) {
					int r = write(job->pfile, job->buffer, job->dbuffer);
					if (r > 0) {
						memmove(job->buffer, &job->buffer[r], job->dbuffer-r);
						job->dbuffer -= r;
					}
					else {
						close(job->pfile);
						job->pfile = -1;
					}
				}
				if (job->pstdout < 0 && job->dbuffer == 0) {
					close(job->pfile);
					job->pfile = -1;
				}
			}
		}
		pthread_mutex_unlock(&queue_mutex);
	}

}

