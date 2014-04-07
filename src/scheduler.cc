
#include <iostream>
#include <vector>
#include <sys/types.h>
#include <sys/wait.h>

struct t_job {
	int id;
	pid_t pid;
};
struct t_job_queue {
	int id;
	int max_running;
	std::vector < t_job > running;
};
std::vector <t_job_queue> queues;

/*
 * Thread responsible for waiting for jobs to finish
 * and to spawn new jobs if any empty slot.
**/
void * scheduler_thread(void * args) {
	
	while (!stop) {
		// Check and add spawn new jobs
		for (unsigned int i = 0; i < queues.size(); i++) {
			while (queue[i].running.size() < queue[i].max_running) {
				t_job j;
				j.id = get_next_job(queue[i].id);

				if (j.id < 0) break; // No more jobs for this queue

				j.pid = spawn_job(j.id);
				queue[i].running.push_back(j);
			}
		}

		// Wait for a job to finish
		int s;
		pid_t f = wait(&s);

		// Mark job as done
		for (unsigned int i = 0; i < queues.size(); i++) {
			for (unsigned int j = 0; i < queues[i].running.size(); i++) {
				if (queues[i].running[j].pid == f) {
					job_finished(queues[i].running[j].id);
					queues[i].running.erase(queues[i].running.begin() + j);
					break;
				}
			}
		}
	}
}


