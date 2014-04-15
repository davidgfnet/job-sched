
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

#ifdef ENABLE_MYSQL_BACKEND
static const char * mysql_init_query = 
	"DROP table IF EXISTS `jobs`;"
	"DROP table IF EXISTS `queues`;"
	""
	"CREATE TABLE IF NOT EXISTS `queues` ("
	"  `id` BIGINT(20) unsigned NOT NULL AUTO_INCREMENT,"
	"  `name` varchar(256) DEFAULT '',"
	"  `max_run` int(10) DEFAULT '0',"
	"  PRIMARY KEY (`id`)"
	") ENGINE=InnoDB DEFAULT CHARSET=utf8;"
	""
	"CREATE TABLE IF NOT EXISTS `jobs` ("
	"  `id` BIGINT(20) unsigned NOT NULL AUTO_INCREMENT,"
	"  `qid` BIGINT(20) unsigned NOT NULL,"
	"  `status` tinyint(255) unsigned NOT NULL DEFAULT '0',"
	"  `prio` tinyint(255) unsigned NOT NULL DEFAULT '255',"
	"  `commandline` varchar(8192) DEFAULT '',"
	"  `environment` TEXT DEFAULT '',"
	"  `output` varchar(4096) DEFAULT '',"
	"  `dateQueued` timestamp NULL DEFAULT CURRENT_TIMESTAMP,"
	"  `dateStarted` timestamp NULL DEFAULT NULL,"
	"  PRIMARY KEY (`id`),"
	"  FOREIGN KEY (`qid`) references queues(`id`)"
	") ENGINE=InnoDB DEFAULT CHARSET=utf8;"
	"";

#include <mysql/mysql.h>

std::string my_db_server;
std::string my_db_user;
std::string my_db_password;
std::string my_db_database;
MYSQL * mysql_connection = 0;

std::string my_escape(const std::string s) {
	char tempb[s.size()*2+2];
	mysql_real_escape_string(mysql_connection, tempb, s.c_str(), s.size());
	return std::string(tempb);
}

void my_connnect_reconnect() {
	if (mysql_connection)
		mysql_close(mysql_connection);
	
	mysql_connection = mysql_init(NULL);
	// Enable auto-reconnect, as some versions do not enable it by default
	my_bool reconnect = 1;
	mysql_options(mysql_connection, MYSQL_OPT_RECONNECT, &reconnect);
	if (!mysql_real_connect(mysql_connection, my_db_server.c_str(), my_db_user.c_str(), my_db_password.c_str(), my_db_database.c_str(), 0, NULL, CLIENT_MULTI_STATEMENTS)) {
		fprintf(stderr, "User %s Pass %s Host %s Database %s\n", my_db_user.c_str(), my_db_password.c_str(), my_db_server.c_str(), my_db_database.c_str());
		exit(1);
	}
	fprintf(stderr, "Connected to MYSQL!\n");
}

// This resets & setups the whole backend. Loosing all changes!
void my_backend_setup() {
	my_connnect_reconnect();
	if (mysql_query(mysql_connection, mysql_init_query)) {
		printf("MySQL error %s\n", mysql_error(mysql_connection));
	}
}

// Get queues
std::vector < t_job_queue > my_backend_get_queues() {
	std::vector < t_job_queue > ret;
	
	std::string query = "SELECT `id`,`name`,`max_run` FROM `queues`";
	mysql_query(mysql_connection, query.c_str());
	MYSQL_RES *result = mysql_store_result(mysql_connection);
	if (result) {
		MYSQL_ROW row;
		while (row = mysql_fetch_row(result)) {
			t_job_queue qu;
			qu.id = atoi(row[0]);
			qu.friendly_name = row[1];
			qu.max_running = atoi(row[2]);
			ret.push_back(qu);
		}
	}
	return ret;
}

// Get up to Max jobs for a queue
void my_get_job_summary(unsigned int * running, unsigned int * waiting, unsigned int * completed) {
	unsigned int res[3];
	for (int i = 0; i < 3; i++) {
		std::ostringstream query;
		query << "SELECT COUNT(*) FROM `jobs` WHERE `status`=" << i << ";";
		std::string sql = query.str();
	
		mysql_query(mysql_connection, sql.c_str());
		MYSQL_RES *result = mysql_store_result(mysql_connection);
		if (result) {
			MYSQL_ROW row = mysql_fetch_row(result);
			res[i] = atoi(row[0]);
		}
	}
	*waiting = res[0];
	*running = res[1];
	*completed = res[2];
}

// Get up to Max jobs for a queue
std::vector < t_queued_job > my_get_jobs(unsigned long long qid, unsigned long long max, int status) {
	std::ostringstream query;
	query << "SELECT `id`,`qid`,`status`,`commandline`,`output`,`environment`,UNIX_TIMESTAMP(`dateQueued`),UNIX_TIMESTAMP(`dateStarted`) FROM `jobs` ";

	if (status >= 0 && qid != ~0)
		query << " WHERE `status`=" << status << " AND `qid`=" << qid;
	else if (status >= 0 && qid == ~0)
		query << " WHERE `status`=" << status;
	else if (status < 0 && qid != ~0)
		query << " WHERE `qid`=" << qid;
	
	query << " ORDER BY `prio` DESC, `id` ASC LIMIT " << max;
	std::string sql = query.str();
		
	std::vector < t_queued_job > ret;
	mysql_query(mysql_connection, sql.c_str());
	MYSQL_RES *result = mysql_store_result(mysql_connection);
	if (result) {
		MYSQL_ROW row;
		while (row = mysql_fetch_row(result)) {
			t_queued_job t;
			t.id = atoi(row[0]);
			t.qid = atoi(row[1]);
			t.status = atoi(row[2]);
			t.commandline = std::string(row[3]);
			t.output = std::string(row[4]);
			t.environ = std::string(row[5]);
			t.dateq = atoi(row[6]);
			t.dater = row[7] ? atoi(row[7]) : 0;
			ret.push_back(t);
		}
	}
	
	return ret;
}

std::vector < t_queued_job > my_get_waiting_jobs(unsigned long long qid, unsigned long long max) {
	return my_get_jobs(qid, max, 0);
}

// Get up to Max jobs for a queue
void my_set_job_status(unsigned long long id, int status) {
	std::ostringstream query;
	query << "UPDATE `jobs` SET `status`=" << status;
	if (status == 1) // Mark run time
		query << ",`dateStarted`=now() ";
	query << " WHERE `id`=" << id;
	std::string sql = query.str();
		
	mysql_query(mysql_connection, sql.c_str());
}
bool my_backend_edit_queue(unsigned long long qid, int max_run) {
	if (max_run < 0) return true;

	std::ostringstream query;
	query << "UPDATE `queues` SET `max_run`=" << max_run;
	query << " WHERE `id`=" << qid;
	std::string sql = query.str();
		
	return (!mysql_query(mysql_connection, sql.c_str()));
}

// Create a new queue
unsigned long long my_backend_create_queue(std::string name, long max_jobs) {
	std::ostringstream query;
	query << "INSERT INTO `queues` (`name`,`max_run`) VALUES ('" << my_escape(name) << "','" << max_jobs << "');";
	std::string sql = query.str();
		
	if (!mysql_query(mysql_connection, sql.c_str()))
		return mysql_insert_id(mysql_connection);
	return ~0;
}

// Delete a queue
void my_backend_delete_queue(unsigned long long qid, bool truncate) {
	std::ostringstream query;
	query << "DELETE FROM `jobs` WHERE `qid`=" << qid << ";";
	if (!truncate)
		query << "DELETE FROM `queues` WHERE `id`=" << qid << ";";
	std::string sql = query.str();
		
	mysql_query(mysql_connection, sql.c_str());
}

// Insert a new job into queue
bool my_backend_create_job(unsigned long long qid, const std::string & cmdline, const std::string & env, const std::string &outf, int prio) {
	std::ostringstream query;
	query << "INSERT INTO `jobs` (`qid`,`prio`,`commandline`,`environment`,`output`) VALUES ";
	query << "('" << qid << "','" << prio << "','" << my_escape(cmdline) << "','" << my_escape(env);
	query << "','" << my_escape(outf) << "');";
	std::string sql = query.str();
		
	return (!mysql_query(mysql_connection, sql.c_str()));
}

void my_running_jobs_status(unsigned long long qid, int status) {
	std::ostringstream query;
	query << "UPDATE `jobs` SET `status`=" << status;
	query << " WHERE `status`=1 ";
	if (qid != ~0)
		query << " AND `qid`=" << qid;

	mysql_query(mysql_connection, query.str().c_str());
}

#define MYSQL_NUM_BACKEND 1
#else
#define MYSQL_NUM_BACKEND 0
#endif

#ifdef ENABLE_SQLITE_BACKEND
#include <sqlite3.h>

static const char * sqlite_init_query = 
	"DROP table IF EXISTS `jobs`;"
	"DROP table IF EXISTS `queues`;"
	""
	"CREATE TABLE IF NOT EXISTS `queues` ("
	"  `id` INTEGER PRIMARY KEY,"
	"  `name` varchar(256) DEFAULT '',"
	"  `max_run` int(10) DEFAULT '0'"
	");"
	""
	"CREATE TABLE IF NOT EXISTS `jobs` ("
	"  `id` INTEGER PRIMARY KEY,"
	"  `qid` INTEGER,"
	"  `status` tinyint(255) NOT NULL DEFAULT '0',"
	"  `prio` tinyint(255) NOT NULL DEFAULT '100',"
	"  `commandline` varchar(8192) DEFAULT '',"
	"  `environment` TEXT DEFAULT '',"
	"  `output` varchar(4096) DEFAULT '',"
	"  `dateQueued` timestamp default (strftime('%s', 'now')),"
	"  `dateStarted` timestamp NULL DEFAULT NULL,"
	"  FOREIGN KEY (`qid`) references queues(`id`)"
	");"
	"";

std::string sqlite_db_file;
sqlite3 * sqlite_db = 0;

void sqlite_connnect_reconnect() {
	if (sqlite_db)
		sqlite3_close(sqlite_db);
	
	if (sqlite3_open(sqlite_db_file.c_str(), &sqlite_db)) {
		std::cerr << "Could not opern database " << sqlite_db_file << std::endl;
		exit(1);
	}
	sqlite3_exec(sqlite_db, "PRAGMA foreign_keys = ON;", 0,0,0);
	std::cerr << "SQLite ready!" << std::endl;
}

// This resets & setups the whole backend. Loosing all changes!
void sqlite_backend_setup() {
	sqlite_connnect_reconnect();
	char *errmsg;
	if (SQLITE_OK != sqlite3_exec(sqlite_db, sqlite_init_query, 0, 0, &errmsg)) {
		std::cerr << "SQLite error at initializing the database!" << std::endl;
		std::cerr << errmsg << std::endl;
		sqlite3_free(errmsg);
	}
}

inline std::string byte2hex(unsigned char c) {
	std::string ret;
	unsigned char b1 = c&0xF;
	unsigned char b2 = (c>>4)&0xF;
	if (b2 < 10) ret += (char)('0' + b2);
	else ret += (char)('A' + b2 - 10);

	if (b1 < 10) ret += (char)('0' + b1);
	else ret += (char)('A' + b1 - 10);

	return ret;
}
std::string sql_escape(const std::string s) {
	std::string ret;
	for (unsigned int i = 0; i < s.size(); i++) {
		ret += byte2hex(s[i]);
	}
	return "X'" + ret + "'";
}


// Get queues
static int sqlite_backend_get_queues_callback(void *arg, int argc, char **argv, char **azColName) {
	std::vector < t_job_queue > * ret = (std::vector < t_job_queue >*)arg;
	t_job_queue qu;
	qu.id = atoi(argv[0]);
	qu.friendly_name = argv[1];
	qu.max_running = atoi(argv[2]);
	(*ret).push_back(qu);
	return 0;
}
std::vector < t_job_queue > sqlite_backend_get_queues() {
	std::vector < t_job_queue > ret;
	const char * query = "SELECT `id`,`name`,`max_run` FROM `queues`";
	sqlite3_exec(sqlite_db, query, sqlite_backend_get_queues_callback, &ret, 0);
	return ret;
}

// Get up to Max jobs for a queue
static int sqlite_get_job_summary_callback(void *arg, int argc, char **argv, char **azColName) {
	int * ret = (int*)arg;
	*ret = atoi(argv[0]);
	return 0;
}
void sqlite_get_job_summary(unsigned int * running, unsigned int * waiting, unsigned int * completed) {
	unsigned int res[3];
	for (int i = 0; i < 3; i++) {
		std::ostringstream query;
		query << "SELECT COUNT(*) FROM `jobs` WHERE `status`=" << i << ";";
	
		sqlite3_exec(sqlite_db, query.str().c_str(), sqlite_get_job_summary_callback, &res[i], 0);
	}
	*waiting = res[0];
	*running = res[1];
	*completed = res[2];
}

// Get up to Max jobs for a queue
static int sqlite_get_jobs_callback(void *arg, int argc, char **argv, char **azColName) {
	std::vector < t_queued_job > * ret = (std::vector < t_queued_job > *)arg;
	t_queued_job t;
	t.id = atoi(argv[0]);
	t.qid = atoi(argv[1]);
	t.status = atoi(argv[2]);
	t.commandline = std::string(argv[3]);
	t.output = std::string(argv[4]);
	t.environ = std::string(argv[5]);
	t.dateq = argv[6] ? atoi(argv[6]) : 0;
	t.dater = argv[7] ? atoi(argv[7]) : 0;
	(*ret).push_back(t);
	return 0;
}
std::vector < t_queued_job > sqlite_get_jobs(unsigned long long qid, unsigned long long max, int status) {
	std::ostringstream query;
	query << "SELECT `id`,`qid`,`status`,`commandline`,`output`,`environment`,`dateQueued`,`dateStarted` FROM `jobs` ";

	if (status >= 0 && qid != ~0)
		query << " WHERE `status`=" << status << " AND `qid`=" << qid;
	else if (status >= 0 && qid == ~0)
		query << " WHERE `status`=" << status;
	else if (status < 0 && qid != ~0)
		query << " WHERE `qid`=" << qid;
	
	query << " ORDER BY `prio` DESC, `id` ASC";
	if (max != ~0) query << " LIMIT " << max;
	std::vector < t_queued_job > ret;
	sqlite3_exec(sqlite_db, query.str().c_str(), sqlite_get_jobs_callback, &ret, 0);
	return ret;
}

std::vector < t_queued_job > sqlite_get_waiting_jobs(unsigned long long qid, unsigned long long max) {
	return sqlite_get_jobs(qid, max, 0);
}


// Get up to Max jobs for a queue
void sqlite_set_job_status(unsigned long long id, int status) {
	std::ostringstream query;
	query << "UPDATE `jobs` SET `status`=" << status;
	if (status == 1) // Mark run time
		query << ",`dateStarted`=strftime('%s', 'now') ";
	query << " WHERE `id`=" << id;

	sqlite3_exec(sqlite_db, query.str().c_str(), 0, 0, 0);
}

bool sqlite_backend_edit_queue(unsigned long long qid, int max_run) {
	if (max_run < 0) return true;

	std::ostringstream query;
	query << "UPDATE `queues` SET `max_run`=" << max_run;
	query << " WHERE `id`=" << qid;
		
	return (SQLITE_OK == sqlite3_exec(sqlite_db, query.str().c_str(), 0, 0, 0));
}


// Create a new queue
unsigned long long sqlite_backend_create_queue(std::string name, long max_jobs) {
	std::ostringstream query;
	query << "INSERT INTO `queues` (`name`,`max_run`) VALUES (" << sql_escape(name) << ",'" << max_jobs << "');";
		
	if (SQLITE_OK == sqlite3_exec(sqlite_db, query.str().c_str(), 0, 0, 0))
		return sqlite3_last_insert_rowid(sqlite_db);
	return ~0;
}

// Delete a queue
void sqlite_backend_delete_queue(unsigned long long qid, bool truncate) {
	std::ostringstream query;
	query << "DELETE FROM `jobs` WHERE `qid`=" << qid << ";";
	if (!truncate)
		query << "DELETE FROM `queues` WHERE `id`=" << qid << ";";
	sqlite3_exec(sqlite_db, query.str().c_str(), 0, 0, 0);
}

// Insert a new job into queue
bool sqlite_backend_create_job(unsigned long long qid, const std::string & cmdline, const std::string & env, const std::string &outf, int prio) {
	std::ostringstream query;
	query << "INSERT INTO `jobs` (`qid`,`prio`,`commandline`,`environment`,`output`) VALUES ";
	query << "('" << qid << "','" << prio << "'," << sql_escape(cmdline) << "," << sql_escape(env);
	query << "," << sql_escape(outf) << ");";
	return (SQLITE_OK == sqlite3_exec(sqlite_db, query.str().c_str(), 0, 0, 0));
}

void sqlite_running_jobs_status(unsigned long long qid, int status) {
	std::ostringstream query;
	query << "UPDATE `jobs` SET `status`=" << status;
	query << " WHERE `status`=1 ";
	if (qid != ~0)
		query << " AND `qid`=" << qid;

	sqlite3_exec(sqlite_db, query.str().c_str(), 0, 0, 0);
}


#define SQLITE_NUM_BACKEND 1
#else
#define SQLITE_NUM_BACKEND 0
#endif

struct t_ba_callbacks {
	t_backend_setup backend_setup;
	t_backend_create_queue backend_create_queue;
	t_backend_edit_queue backend_edit_queue;
	t_backend_create_job backend_create_job;
	t_backend_delete_queue backend_delete_queue;
	t_get_job_summary get_job_summary;
	t_set_job_status set_job_status;
	t_get_jobs get_jobs;
	t_get_waiting_jobs get_waiting_jobs;
	t_backend_get_queues backend_get_queues;
	t_running_jobs_status backend_running_jobs_status;
};
unsigned int cba = -1;

#define NUM_BACKENDS (MYSQL_NUM_BACKEND+SQLITE_NUM_BACKEND)

t_ba_callbacks callbacks[NUM_BACKENDS] = {
	#ifdef ENABLE_MYSQL_BACKEND
	{
		my_backend_setup,
		my_backend_create_queue,
		my_backend_edit_queue,
		my_backend_create_job,
		my_backend_delete_queue,
		my_get_job_summary,
		my_set_job_status,
		my_get_jobs,
		my_get_waiting_jobs,
		my_backend_get_queues,
		my_running_jobs_status
	},
	#endif
	#ifdef ENABLE_SQLITE_BACKEND
	{
		sqlite_backend_setup,
		sqlite_backend_create_queue,
		sqlite_backend_edit_queue,
		sqlite_backend_create_job,
		sqlite_backend_delete_queue,
		sqlite_get_job_summary,
		sqlite_set_job_status,
		sqlite_get_jobs,
		sqlite_get_waiting_jobs,
		sqlite_backend_get_queues,
		sqlite_running_jobs_status
	},
	#endif
};

void backend_setup() {
	callbacks[cba].backend_setup();
}
unsigned long long backend_create_queue(std::string name, long max_jobs) {
	return callbacks[cba].backend_create_queue(name,max_jobs);
}
bool backend_create_job(unsigned long long qid, const std::string & cmdline, const std::string & env, const std::string &outf, int prio) {
	return callbacks[cba].backend_create_job(qid,cmdline,env,outf,prio);
}
void backend_delete_queue(unsigned long long qid, bool t) {
	callbacks[cba].backend_delete_queue(qid, t);
}
void get_job_summary(unsigned int * running, unsigned int * waiting, unsigned int * completed) {
	callbacks[cba].get_job_summary(running,waiting,completed);
}
void set_job_status(unsigned long long id, int status) {
	callbacks[cba].set_job_status(id,status);
}
std::vector < t_queued_job > get_jobs(unsigned long long qid, unsigned long long max, int status) {
	return callbacks[cba].get_jobs(qid,max,status);
}
std::vector < t_queued_job > get_waiting_jobs(unsigned long long qid, unsigned long long max) {
	return callbacks[cba].get_waiting_jobs(qid,max);
}
std::vector < t_job_queue > backend_get_queues() {
	return callbacks[cba].backend_get_queues();
}
bool backend_edit_queue(unsigned long long qid, int max_run) {
	return callbacks[cba].backend_edit_queue(qid,max_run);
}
void backend_running_jobs_status(unsigned long long qid, int status) {
	callbacks[cba].backend_running_jobs_status(qid,status);
}


int parse_db_config(const std::string & conf) {
	std::vector <std::string> pieces = Tokenize(conf,":");
	if (pieces.size() != 2) return 0;
	
	#ifdef ENABLE_MYSQL_BACKEND
	if (pieces[0] == "mysql") {
		std::vector <std::string> params = Tokenize(pieces[1],",");
		if (params.size() != 4) return 0;

		my_db_server   = params[0];
		my_db_database = params[1];
		my_db_user     = params[2];
		my_db_password = params[3];
		cba = 0;
		my_connnect_reconnect();
	}
	#endif
	#ifdef ENABLE_SQLITE_BACKEND
	if (pieces[0] == "sqlite") {
		sqlite_db_file = pieces[1];
		cba = MYSQL_NUM_BACKEND;
		sqlite_connnect_reconnect();
	}
	#endif

	if (cba < 0) {
		fprintf(stderr,"No backend available, aborting!\n");
		exit(1);
	}
}

// Load existing stuff into local data
void db_load_startup() {
	std::vector < t_job_queue > qu = backend_get_queues();
	pthread_mutex_lock(&queue_mutex);
	for (unsigned int i = 0; i < qu.size(); i++) {
		queues.push_back(qu[i]);
	}
	std::cerr << "Loaded " << queues.size() << " queues" << std::endl;
	// Set running jobs to waiting (all queues)
	backend_running_jobs_status(~0, 0);
	pthread_mutex_unlock(&queue_mutex);
}


