
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

// Backend

#ifdef ENABLE_MYSQL_BACKEND

#include <mysql/mysql.h>

std::string db_server;
std::string db_user;
std::string db_password;
std::string db_database;

MYSQL * mysql_connection = 0;

std::string db_escape(const std::string s) {
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
	if (!mysql_real_connect(mysql_connection, db_server.c_str(), db_user.c_str(), db_password.c_str(), db_database.c_str(), 0, NULL, CLIENT_MULTI_STATEMENTS)) {
		fprintf(stderr, "User %s Pass %s Host %s Database %s\n", db_user.c_str(), db_password.c_str(), db_server.c_str(), db_database.c_str());
		exit(1);
	}
	fprintf(stderr, "Connected to MYSQL!\n");
}

// This resets & setups the whole backend. Loosing all changes!
void backend_setup() {
	static const char * query = 
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


	my_connnect_reconnect();
	if (mysql_query(mysql_connection, query)) {
		printf("MySQL error %s\n", mysql_error(mysql_connection));
	}
}

// Get queues
std::vector < t_job_queue > get_queues() {
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
void get_job_summary(unsigned int * running, unsigned int * waiting, unsigned int * completed) {
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
	*running = res[0];
	*waiting = res[1];
	*completed = res[2];
}

// Get up to Max jobs for a queue
std::vector < t_queued_job > get_jobs(unsigned long long qid, unsigned long long max, int status) {
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

std::vector < t_queued_job > get_waiting_jobs(unsigned long long qid, unsigned long long max) {
	return get_jobs(qid, max, 0);
}

// Get up to Max jobs for a queue
void set_job_status(unsigned long long id, int status) {
	std::ostringstream query;
	query << "UPDATE `jobs` SET `status`=" << status;
	if (status == 1) // Mark run time
		query << ",`dateStarted`=now() ";
	query << " WHERE `id`=" << id;
	std::string sql = query.str();
		
	mysql_query(mysql_connection, sql.c_str());
}

// Create a new queue
unsigned long long create_queue(std::string name, long max_jobs) {
	std::ostringstream query;
	query << "INSERT INTO `queues` (`name`,`max_run`) VALUES ('" << db_escape(name) << "','" << max_jobs << "');";
	std::string sql = query.str();
		
	if (!mysql_query(mysql_connection, sql.c_str()))
		return mysql_insert_id(mysql_connection);
	return ~0;
}

// Delete a queue
void delete_queue_backend(unsigned long long qid) {
	std::ostringstream query;
	query << "DELETE FROM `jobs` WHERE `qid`=" << qid << ";";
	query << "DELETE FROM `queues` WHERE `id`=" << qid << ";";
	std::string sql = query.str();
		
	mysql_query(mysql_connection, sql.c_str());
}

// Insert a new job into queue
bool create_job(unsigned long long qid, const std::string & cmdline, const std::string & env, const std::string &outf, int prio) {
	std::ostringstream query;
	query << "INSERT INTO `jobs` (`qid`,`prio`,`commandline`,`environment`,`output`) VALUES ";
	query << "('" << qid << "','" << prio << "','" << db_escape(cmdline) << "','" << db_escape(env);
	query << "','" << db_escape(outf) << "');";
	std::string sql = query.str();
		
	return (!mysql_query(mysql_connection, sql.c_str()));
}

#endif

int parse_db_config(const std::string & conf) {
	std::vector <std::string> pieces = Tokenize(conf,":");
	if (pieces.size() != 2) return 0;
	std::vector <std::string> params = Tokenize(pieces[1],",");
	if (params.size() != 4) return 0;
	
	if (pieces[0] == "mysql") {
		db_server   = params[0];
		db_database = params[1];
		db_user     = params[2];
		db_password = params[3];
	}
	
	my_connnect_reconnect();
}

void db_load_startup() {
	std::vector < t_job_queue > qu = get_queues();
	pthread_mutex_lock(&queue_mutex);
	for (unsigned int i = 0; i < qu.size(); i++) {
		queues.push_back(qu[i]);
	}
	std::cerr << "Loaded " << queues.size() << " queues" << std::endl;
	pthread_mutex_unlock(&queue_mutex);
}


