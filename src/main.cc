
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "backend.h"

const char usage [] = "%s [-p port] [-db dbinfo] [-s] [-a]\n"
	"\n"
	" -s:       Reset backend daabase (clears all jobs & queues!!!)\n"
	" -a:       Binds server address to any IP (listens on external IPs)\n"
	"\n"
	"dbinfo:\n"
	" - mysql:  mysql:host,database,user,password\n"
	" - sqlite: sqlite:host,database,user,password\n";

int main(int argc, char ** argv) {
	fprintf(stderr,"Job-Scheduler daemon\n");
	
	if (argc < 2) {
		fprintf(stderr, usage, argv[0]);
		exit(1);
	}
	
	bool anyip = false;
	for (int i = 1; i < argc; i++) {
		if (strcmp("-p", argv[i]) == 0) {
			i++;
		}
		else if (strcmp("-db", argv[i]) == 0) {
			if (i+1 >= argc || !parse_db_config(std::string(argv[i+1]))) {
				fprintf(stderr, "Cannot parse db config\n");
			}
			i++;
		}
		else if (strcmp("-s", argv[i]) == 0) {
			backend_setup();
			fprintf(stderr, "Backend reset!\n");
			exit(0);
		}
		else if (strcmp("-a", argv[i]) == 0)
			anyip = true;
	}
	
	// Load initial val.
	db_load_startup();

	// Start the scheduler and output collector threads	
	int stop = 0;
	pthread_t sched, output;
	pthread_create (&sched,  NULL, &scheduler_thread, &stop);
	pthread_create (&output, NULL, &output_thread,    &stop);
	
	// Start a server to receive queries
	serve_queries(8080, anyip);
}


