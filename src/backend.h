
void backend_setup();
std::vector < t_queued_job > get_jobs(unsigned long long qid, unsigned long long max, int status);
std::vector < t_queued_job > get_waiting_jobs(unsigned long long qid, unsigned long long max);
int parse_db_config(const std::string & conf);
void set_job_status(unsigned long long id, int status);
unsigned long long create_queue(std::string name, long max_jobs);
void db_load_startup();

