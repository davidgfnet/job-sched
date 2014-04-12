
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

const char header_200[] = "HTTP/1.1 200 OK\r\nContent-Length: %d\r\nContent-Type: %s\r\nConnection: close\r\n\r\n";
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
void serve_queries(int port, bool anyip) {
	
	int listenfd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in servaddr;
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(anyip ? INADDR_ANY: INADDR_LOOPBACK);
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
					if ((!IOTRY_AGAIN(r) && r < 0) || reqs[i].response.size() == 0) {
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

std::string get_mime(std::string type) {
	if (type == "css") return "text/css";
	if (type == "html") return "text/html";
	if (type == "js") return "text/javascript";
	return "application/octet-stream";
}

std::string get_ext(std::string path) {
	std::string ret = path;
	while (ret.find(".") != std::string::npos) {
		ret = ret.substr(ret.find(".")+1);
	}
	return ret;
}

std::string get_header(int res, std::string path) {
	if (res < 0)
		return std::string(header_404);
	std::string mtype = (path.substr(0,5) == "/web/" ?get_mime(get_ext(path)) : "application/json");
	char t[4096];
	sprintf(t, header_200, res, mtype.c_str());
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

std::string get_file(std::string path) {
	path = fullpath(path);
	FILE * fd = fopen(path.c_str(), "rb");
	if (!fd) return "";

	std::string ret;
	int r;
	do {
		char temp[4096];
		r = fread(temp,1,sizeof(temp),fd);
		if (r > 0)
			ret += std::string(temp, r);
	} while (r > 0);
	fclose(fd);
	return ret;
}

std::string json_escape(const std::string & s) {
	std::string ret;
	for (unsigned int i = 0; i < s.size(); i++) {
		if (s[i] == '\\')
			ret = ret + "\\\\";
		else if (s[i] == '\n')
			ret = ret + "\\n";
		else if (s[i] == '\r')
			ret = ret + "\\r";
		else if (s[i] == '\t')
			ret = ret + "\\t";
		else
			ret = ret + s[i];
	}
	return ret;
}

unsigned long long get_qid(std::string path) {
	path = urldecode(path);
	if (path.size() == 0) return ~0;
	// Queue ID
	if (path[0] >= '0' && path[0] <= '9')
		return atol(path.c_str());
	// Queue name
	pthread_mutex_lock(&queue_mutex);
	for (unsigned int i = 0; i < queues.size(); i++) {
		if (queues[i].friendly_name == path) {
			pthread_mutex_unlock(&queue_mutex);
			return queues[i].id;
		}
	}
	pthread_mutex_unlock(&queue_mutex);
	return ~0;
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
		os << "{ \"code\": \"ok\", \"result\": [ ";
		pthread_mutex_lock(&queue_mutex);
		for (unsigned int i = 0; i < queues.size(); i++) {
			if (i != 0) os << ",";
			os << "{ \"id\":\"" << queues[i].id << "\", \"name\":\"" << queues[i].friendly_name <<
				"\", \"max_run\":\"" << queues[i].max_running << "\", \"run\":\"" << queues[i].running.size() <<
				"\" }";
		}
		pthread_mutex_unlock(&queue_mutex);
		os << "] }";
	}
	else if (path.substr(0,9) == "/queuejob") {
		unsigned long long qid = ~0;
		if (path.size() > 10 and path[9] == '/')
			qid = get_qid(path.substr(10));
		
		std::string env     = urldecode(post_parse(body, "env"));
		std::string command = urldecode(post_parse(body, "command"));
		std::string outputf = urldecode(post_parse(body, "output"));
		int prio            = atoi(post_parse(body, "prio").c_str());
		
		if (qid != ~0 && backend_create_job(qid, command, env, outputf, prio))
			os << "{ \"code\": \"ok\"}";
		else
			os << "{ \"code\": \"error\"}";
	}
	else if (path.substr(0,5) == "/jobs") { // Form /jobs/queueid-name/status
		std::vector <std::string > fields = Tokenize(path.substr(5),"/");
		unsigned long long qid = (fields.size() > 1 && fields[1] != "") ? get_qid(fields[1]) : ~0;
		std::string st = fields.size() > 2 ? fields[2] : "";
		int status = (st == "wait" ? 0 :
						st == "run" ? 1 :
						st == "comp" ? 2 : -1);
		
		// Get jobs for a given queue (or all queues)
		os << "{ \"code\": \"ok\", \"result\": [ ";
		pthread_mutex_lock(&queue_mutex);
		for (unsigned int i = 0; i < queues.size(); i++) {
			if (qid != ~0 && queues[i].id != qid) continue;
			if (qid == ~0 && i != 0) os << ",";

			os << "{ \"qid\": \"" << queues[i].id << "\", \"name\": \"" << queues[i].friendly_name << "\", \"jobs\": [";
			std::vector < t_queued_job > jobs = get_jobs(queues[i].id, ~0, status);
			for (unsigned int j = 0; j < jobs.size(); j++) {
				if (j != 0) os << ",";
				os << "{ \"id\":\"" << jobs[j].id << "\", ";
				os << "\"status\":\"" << jobs[j].status << "\", ";
				os << "\"dateq\":\"" << jobs[j].dateq << "\", ";
				os << "\"dater\":\"" << jobs[j].dater << "\", ";
				os << "\"commandline\":\"" << json_escape(jobs[j].commandline) << "\", ";
				os << "\"output\":\"" << jobs[j].output << "\" }";
			}
			os << "]}";
		}
		pthread_mutex_unlock(&queue_mutex);
		os << "]}";
	}
	else if (path == "/summary") {
		pthread_mutex_lock(&queue_mutex);
		unsigned int max_jobs = 0;
		unsigned int nq = queues.size();
		for (unsigned int i = 0; i < queues.size(); i++)
			max_jobs += queues[i].max_running;
		pthread_mutex_unlock(&queue_mutex);
		
		unsigned int running, waiting, completed;
		get_job_summary(&running, &waiting, &completed);
		
		os << "{ \"code\": \"ok\", \"jobs\": ";
		os << "{ \"running\": \"" << running << "\", ";
		os << "  \"waiting\": \"" << waiting << "\", ";
		os << "  \"completed\": \"" << completed << "\" }, ";
		os << " \"queues\" : { ";
		os << "  \"total\": \"" << nq << "\", ";
		os << "  \"maxjobs\": \"" << max_jobs << "\" } } ";
	}
	else if (path == "/newqueue") {
		// Data is in the post body
		std::string queue_name = urldecode(post_parse(body, "name"));
		int queue_mrun = atoi(post_parse(body, "max_run").c_str());
		unsigned long long qid = backend_create_queue(queue_name, queue_mrun);
		if (qid != ~0) {
			pthread_mutex_lock(&queue_mutex);
			t_job_queue qu;
			qu.id = qid;
			qu.friendly_name = queue_name;
			qu.max_running = queue_mrun;
			queues.push_back(qu);

			pthread_mutex_unlock(&queue_mutex);
			os << "{ \"code\": \"ok\"}";
		}
		else
			os << "{ \"code\": \"error\"}";
	}
	else if (path.substr(0,10) == "/delqueue/" ||
			path.substr(0,10) == "/trnqueue/") {
		// Data is in the post body
		unsigned long long qid = ~0;
		if (path.size() > 10)
			qid = get_qid(path.substr(10));

		bool only_truncate = path.substr(0,10) == "/trnqueue/";

		if (qid != ~0 && delete_queue(qid, only_truncate))
			os << "{ \"code\": \"ok\"}";
		else
			os << "{ \"code\": \"error\"}";
	}
	else if (path.substr(0,5) == "/web/") {
		os << get_file(path.substr(1));
	}

	std::string rbody = os.str();
	if (rbody == "")
		return get_header(-1,path) + rbody;
	else
		return get_header(rbody.size(),path) + rbody;
}



