
#ifndef _JOBSCHED_BACKEND__H_
#define _JOBSCHED_BACKEND__H_

// General operations
void db_load_startup();

// Global ba op
int parse_db_config(const std::string & conf);

// Function types
typedef void (*t_backend_setup)();
typedef unsigned long long (*t_backend_create_queue)(std::string, long);
typedef bool (*t_backend_create_job)(unsigned long long, const std::string &, const std::string &, const std::string &, int );
typedef void (*t_backend_delete_queue)(unsigned long long, bool);
typedef void (*t_get_job_summary)(unsigned int *, unsigned int *, unsigned int *);
typedef void (*t_set_job_status)(unsigned long long, int);
typedef std::vector < t_queued_job > (*t_get_jobs)(unsigned long long, unsigned long long, int);
typedef std::vector < t_queued_job > (*t_get_waiting_jobs)(unsigned long long, unsigned long long);
typedef std::vector < t_job_queue > (*t_backend_get_queues)();

// Backend basic operations
void backend_setup();
unsigned long long backend_create_queue(std::string name, long max_jobs);
bool backend_create_job(unsigned long long qid, const std::string & cmdline, const std::string & env, const std::string &outf, int prio);
void backend_delete_queue(unsigned long long qid, bool truncate);
void get_job_summary(unsigned int * running, unsigned int * waiting, unsigned int * completed);
void set_job_status(unsigned long long id, int status);
std::vector < t_queued_job > get_jobs(unsigned long long qid, unsigned long long max, int status);
std::vector < t_queued_job > get_waiting_jobs(unsigned long long qid, unsigned long long max);
std::vector < t_job_queue > backend_get_queues();

#endif

