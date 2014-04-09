
#include <iostream>
#include <stdio.h>
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "backend.h"

extern char **environ;

const char usage [] = "%s host@port command args\n"
	"\n"
	" Command list:\n"
	" - queue queue-id/name output commandline:  Queues a job in queue-id/queue-name queue\n"
	"                                             using the current environment and the commandline provided\n"
	" - listjobs [queue-id|queue-name]:          Prints a list of jobs for a given queue (or all jobs if\n"
	"                                             no queue id/name is provided\n"
	" - listqueues:                              Prints the queues available in the server\n"
	" - addqueue name max_run:                   Adds a queue with name and maximum number of running jobs\n";
;

std::string getenv() {
	std::string renv;
	for (char **env = environ; *env; ++env) {
		renv += std::string(*env);
		if (env[1]) renv += "\n";
	}
	return renv;
}

std::string server_addr_port;

std::string uencode(const std::string & data) {
	CURL * curl = curl_easy_init();
	char * eurl = curl_easy_escape(curl, data.c_str(), data.size());
	std::string r = std::string(eurl);
	free(eurl);
	curl_easy_cleanup(curl);
	return r;
}

struct http_query { char * buffer; int received; };
static size_t curl_fwrite(void *buffer, size_t size, size_t nmemb, void *stream) {
	struct http_query * q = (struct http_query *)stream;

	q->buffer = (char*)realloc(q->buffer, size*nmemb + q->received + 32);
	char *bb = q->buffer;

	memcpy(&bb[q->received], buffer, size*nmemb);
	q->received += size*nmemb;

	return size*nmemb;
}

std::string curl_get(const char * url, const char * post) {
	struct http_query hq;
	memset(&hq,0,sizeof(hq));
	hq.buffer = (char*)malloc(32);

	CURL * curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_fwrite);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &hq);
	curl_easy_setopt(curl, CURLOPT_URL, url);

	if (post != 0) {
		curl_easy_setopt(curl, CURLOPT_POST, 1);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post);
	}

	CURLcode res = curl_easy_perform(curl);

	std::string ret;
	if(CURLE_OK == res)
		ret = std::string(hq.buffer, hq.received);

	curl_easy_cleanup(curl);
	
	free(hq.buffer);
	return ret;
}

std::string send_request(const std::string & command, const std::string & post = "") {
	std::string url = "http://" + server_addr_port + command;
	std::string resp = curl_get(url.c_str(), post.size() ? post.c_str() : 0);
	return resp;
}

int main(int argc, char ** argv) {
	fprintf(stderr,"Job-Scheduler client\n");
	
	if (argc < 3) {
		fprintf(stderr, usage, argv[0]);
		exit(1);
	}
	
	char * server_addr = argv[1];
	char * command     = argv[2];
	char * arg0        = (argc >= 4) ? argv[3] : NULL;
	char * arg1        = (argc >= 5) ? argv[4] : NULL;
	server_addr_port = std::string(server_addr);
	
	if (strcmp("queue", command) == 0) {
		if (arg0 && arg1) {
			std::string command;
			for (int i = 4; i < argc; i++)
				command += std::string(argv[i]) + ((i+1 < argc) ? "\n" : "");
			std::string env = getenv();
			std::string p = "command=" + uencode(command) + "&env=" + uencode(env) + "&output=" + uencode(arg1);
			std::string r = send_request("/queuejob/" + uencode(arg0), p);
			std::cout << r << std::endl;
		}
		else
			fprintf(stderr, "Need a queue ID/name to submit the job and an output file!\n");
	}
	else if (strcmp("listjobs", command) == 0) {
		std::string r = send_request(arg0 ? "/jobs/" + uencode(arg0) : "/jobs");
		std::cout << r << std::endl;
	}
	else if (strcmp("listqueues", command) == 0) {
		std::string r = send_request("/queues");
		std::cout << r << std::endl;
	}
	else if (strcmp("addqueue", command) == 0) {
		std::string p = "name=" + uencode(arg0) + "&max_run=" + std::string(arg1);
		std::string r = send_request("/newqueue", p);
		std::cout << r << std::endl;
	}
}


