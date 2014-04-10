
#include <algorithm>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <vector>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sstream>
#include "common.h"
#include "backend.h"

const char header_200[] = "HTTP/1.1 200 OK\r\nContent-Length: %d\r\nContent-Type: text/xml\r\nConnection: close\r\n\r\n";
const char header_404[] = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n";

enum CStatus { sReceiving, sResponding, sError };

struct t_request {
	int fd;
	CStatus status;
	
	short flags;
	long start_time;
	std::string request;
	std::string response;
};
std::vector <t_request> reqs;

bool valid_header(std::string req);
std::string get_response(const std::string & req);

// Server for requests dispatch
void serve_queries(int port) {
	
	int listenfd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in servaddr;
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(port);
	int yes = 1;
	setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
	if (bind(listenfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
		printf("Error binding the port.\n",errno); perror("bind"); exit(1);
	}

	setNonblocking(listenfd);
	if(listen(listenfd,5) < 0) {
		printf("Error listening on the port\n"); perror("listen"); exit(1);
	}
	
	while (!global_stop) {
		// Check incoming connections
		int fd = accept(listenfd, NULL, NULL);
		if (fd >= 0) {
			setNonblocking(fd);
			
			t_request req;
			req.fd = fd;
			req.status = sReceiving;
			req.start_time = time(0);
			reqs.push_back(req);
		}
		
		for (unsigned int i = 0; i < reqs.size(); i++) {
			if (reqs[i].fd >= 0) {
				if (reqs[i].status == sReceiving) {
					char temp[4096];
					int r = read(reqs[i].fd, temp, sizeof(temp));
					if (r > 0) {
						reqs[i].request += std::string(temp, r);
						if (valid_header(reqs[i].request)) {
							reqs[i].status = sResponding;
							reqs[i].response = get_response(reqs[i].request);
						}
					}
					else if (r == 0 || !IOTRY_AGAIN(r)) {
						reqs[i].status = sError;
					}
				}
				if (reqs[i].status == sResponding) {
					int r = write(reqs[i].fd, reqs[i].response.c_str(), reqs[i].response.size());
					if (r > 0) {
						reqs[i].response = reqs[i].response.substr(r);
					}
					if (!IOTRY_AGAIN(r) || reqs[i].response.size() == 0) {
						reqs[i].status = sError;
					}
				}
				if (reqs[i].status == sReceiving)
					reqs[i].flags = POLLIN;
				if (reqs[i].status == sResponding)
					reqs[i].flags = POLLOUT | POLLERR;
				if (reqs[i].status == sError) {
					close(reqs[i].fd);
					reqs[i].fd = -1;
				}
			}
		}
		
		// Setup poll structure for jobs
		std::vector <struct pollfd> pfd;
		pfd.push_back(mkp(listenfd, POLLIN));

		for (unsigned int i = 0; i < reqs.size(); i++)
			if (reqs[i].fd >= 0)
				pfd.push_back(mkp(reqs[i].fd, reqs[i].flags));
		
		poll(&pfd[0], pfd.size(), 10000);
	}
}

bool valid_header(std::string req) {
	std::transform(req.begin(), req.end(), req.begin(), ::tolower);
	
	bool valid = (req.find("\r\n\r\n") != std::string::npos);
	valid = valid and (req.substr(0,4) == "get " || req.substr(0,5) == "post ");
	
	// Check if it has body data
	if (req.find("content-length:") != std::string::npos) {
		// Parse body length and make sure we have the whole request
		size_t p = req.find("content-length:") + 15;
		while (req.size() < p and req[p] == ' ')
			p++;
		int body_size = atoi(&req[p]);
		
		size_t head_size = req.find("\r\n\r\n");
		size_t total_size = head_size + 4 + body_size;
		
		valid = valid and (total_size <= req.size());
	}
	
	return valid;
}

std::string get_header(int res) {
	if (res < 0)
		return std::string(header_404);
	char t[4096];
	sprintf(t, header_200, res);
	return std::string(t);
}

std::string post_parse(const std::string & body, const std::string & param) {
	std::vector <std::string> params = Tokenize(body,"&");
	for (unsigned int i = 0; i < params.size(); i++) {
		std::vector <std::string> key_val = Tokenize(params[i],"=");
		if (key_val.size() != 2) continue;
		
		if (key_val[0] == param)
			return key_val[1];
	}
	return "";
}

// Requests:
// /queues
// /jobs
// /jobs/queueid

std::string get_response(const std::string & req) {
	std::vector <std::string> lines = Tokenize(req,"\r\n");
	
	std::vector <std::string> l0 = Tokenize(lines[0]," ");
	if (l0.size() < 3) return "";
	
	std::string body = req.substr(req.find("\r\n\r\n")+4);
	
	std::string method = l0[0];
	std::string path   = urldecode(l0[1]);

	std::ostringstream os;

	printf("Requested %s\n", path.c_str());	
	if (path == "/queues") {
		// Return all the queues, with their characteristics
		os << "<queues>";
		pthread_mutex_lock(&queue_mutex);
		for (unsigned int i = 0; i < queues.size(); i++) {
			os << "<queue id='" << queues[i].id << "' name='" << queues[i].friendly_name <<
				"' max_run='" << queues[i].max_running << "' run='" << queues[i].running.size() <<
				"' />";
		}
		pthread_mutex_unlock(&queue_mutex);
		os << "</queues>";
	}
	else if (path.substr(0,9) == "/queuejob") {
		unsigned long long qid = ~0;
		if (path.size() > 10 and path[9] == '/')
			qid = atoi(path.substr(10).c_str());
		
		std::string env     = urldecode(post_parse(body, "env"));
		std::string command = urldecode(post_parse(body, "command"));
		std::string outputf = urldecode(post_parse(body, "output"));
		int prio            = atoi(post_parse(body, "prio").c_str());
		
		if (create_job(qid, command, env, outputf, prio))
			os << "<ok />";
		else
			os << "<err />";
		
	}
	else if (path.substr(0,5) == "/jobs") {
		unsigned long long qid = ~0;
		if (path.size() > 6 and path[5] == '/')
			qid = atoi(path.substr(6).c_str());
		
		// Get jobs for a given queue (or all queues)
		os << "<jobs>";
		pthread_mutex_lock(&queue_mutex);
		for (unsigned int i = 0; i < queues.size(); i++) {
			if (qid != ~0 && queues[i].id != qid) continue;
			
			os << "<queue id='" << queues[i].id << "' name='" << queues[i].friendly_name << "'>";
			std::vector < t_queued_job > jobs = get_jobs(queues[i].id, ~0, -1);
			for (unsigned int j = 0; j < jobs.size(); j++) {
				os << "<job id='" << jobs[j].id << "' ";
				os << "status='" << jobs[j].status << "' ";
				os << "dateq='" << jobs[j].dateq << "' ";
				os << "dater='" << jobs[j].dater << "' ";
				os << "commandline='" << jobs[j].commandline << "' ";
				os << "output='" << jobs[j].output << "' />";
			}
			os << "</queue>";
		}
		pthread_mutex_unlock(&queue_mutex);
		os << "</jobs>";
	}
	else if (path == "/newqueue") {
		// Data is in the post body
		std::string queue_name = urldecode(post_parse(body, "name"));
		int queue_mrun = atoi(post_parse(body, "max_run").c_str());
		unsigned long long qid = create_queue(queue_name, queue_mrun);
		if (qid != ~0) {
			pthread_mutex_lock(&queue_mutex);
			t_job_queue qu;
			qu.id = qid;
			qu.friendly_name = queue_name;
			qu.max_running = queue_mrun;
			queues.push_back(qu);

			pthread_mutex_unlock(&queue_mutex);
			os << "<ok />";
		}
		else
			os << "<err />";
	}

	std::string rbody = os.str();
	if (rbody == "")
		return get_header(-1) + rbody;
	else
		return get_header(rbody.size()) + rbody;
}



