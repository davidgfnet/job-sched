
void backend_setup();
std::vector < t_queued_job > get_jobs(unsigned long long qid, unsigned long long max, int status);
std::vector < t_queued_job > get_waiting_jobs(unsigned long long qid, unsigned long long max);
int parse_db_config(const std::string & conf);
void set_job_status(unsigned long long id, int status);
void db_load_startup();

// Backend basic operations
unsigned long long create_queue(std::string name, long max_jobs);
bool create_job(unsigned long long qid, const std::string & cmdline, const std::string & env, const std::string &outf, int prio);
void delete_queue_backend(unsigned long long qid);
void get_job_summary(unsigned int * running, unsigned int * waiting, unsigned int * completed);

