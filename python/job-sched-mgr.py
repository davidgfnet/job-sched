#!/usr/bin/python3

import argparse
import json
import urllib.request
import urllib.parse
import os

# Pretty table formatter :D
class ALIGN:
	LEFT, RIGHT = '-', ''
class Column(list):
	def __init__(self, name, data, align=ALIGN.RIGHT):
		list.__init__(self, data)
		self.name = name
		self.width = max(len(x) for x in data + [name])
		self.format = ' %%%s%ds ' % (align, self.width)
class Table:
	def __init__(self, *columns):
		self.columns = columns
		self.length = max(len(x) for x in columns)
	def get_row(self, i=None):
		for x in self.columns:
			if i is None: yield x.format % x.name
			else:         yield x.format % x[i]
	def get_line(self):
		for x in self.columns:
			yield '-' * (x.width + 2)
	def join_n_wrap(self, char, elements):
		return ' ' + char + char.join(elements) + char
	def get_rows(self):
		yield self.join_n_wrap('+', self.get_line())
		yield self.join_n_wrap('|', self.get_row(None))
		yield self.join_n_wrap('+', self.get_line())
		for i in range(0, self.length):
			yield self.join_n_wrap('|', self.get_row(i))
		yield self.join_n_wrap('+', self.get_line())
	def __str__(self):
		return '\n'.join(self.get_rows())


global_server_addr_port = "localhost:8080"

def n2s(n):
	if type(n) is str: n = int(n)
	ss = {0:'Waiting', 1:'Running', 2:'Completed'}
	return ss[n]

def brm(l):	return ' '.join(l.split('\n'))

def URLRequest(url, params):
	try:
		if len(params) > 0:
			data = urllib.parse.urlencode(params).encode('ascii')
			return urllib.request.urlopen(url,data).read()
		else:
			return urllib.request.urlopen(url).read()
	except:
		raise

def send_command(command, params = {}):
	data = URLRequest("http://" + global_server_addr_port + command, params)
	return json.loads(data.decode('utf-8'))

def command_listqueue(args):
	res = send_command("/queues")
	#check_code(res)
	print( Table( 
			Column("Queue ID",        [x['id']      for x in res['result']]),
			Column("Queue name",      [x['name']    for x in res['result']]),
			Column("Max active jobs", [x['max_run'] for x in res['result']]),
			Column("Active jobs",     [x['run']     for x in res['result']])
	))

def command_listjobs(args):
	if (args.queue_id):
		res = send_command("/jobs/" + urllib.parse.quote(args.queue_id))
	else:
		res = send_command("/jobs")
	#check_code(res)
	print( Table( 
			Column("Job ID",          [y['id']                for x in res['result'] for y in x['jobs']]),
			Column("Queue ID",        [x['qid']               for x in res['result'] for y in x['jobs']]),
			Column("Job status",      [n2s(y['status'])       for x in res['result'] for y in x['jobs']]),
			Column("Commandline",     [brm(y['commandline'])  for x in res['result'] for y in x['jobs']]),
	))

def command_delq(args, trunc):
	c = "trnqueue" if trunc else "delqueue"
	res = send_command("/" + c + "/" + urllib.parse.quote(args.queue_id))
	if res['code'] == 'ok':
		if trunc:
			print("Queue successfully truncated")
		else:
			print("Queue successfully deleted")
	else:
		print("The command could not be completed")

def command_trucqueue(args):
	command_delq(args, True)
def command_delqueue(args):
	command_delq(args, False)

def command_queuejob(args):
	env = '\n'.join([var+"="+os.environ[var] for var in os.environ])
	params = { 
		"command" : '\n'.join(args.command), 
		"env" : env,
		"output" : os.path.abspath(args.output_file),
		"prio": "100"
	}
	res = send_command("/queuejob/" + urllib.parse.quote(args.queue_id), params)
	if res['code'] == "ok":
		print("Job successfully queued!")
	else:
		print("Error queuing job!")

parser = argparse.ArgumentParser(description='Manage job-scheduler server')

# Optional server addr argument
parser.add_argument('-s', dest='server_address', action='store',
                   default="localhost:8080",
                   help='Address and Port for the server, i.e. 1.2.3.4:91')

# Required command and arguments
subparsers = parser.add_subparsers(help='commands')

# A listqueue command
list_parser = subparsers.add_parser('listqueues', help='List available queues on the server')
list_parser.set_defaults(func=command_listqueue)

# A listjobs command
list_parser = subparsers.add_parser('listjobs', help='List available jobs on the server')
list_parser.add_argument('queue_id', action='store', default=None, help='Queue ID to list', nargs='?')
list_parser.set_defaults(func=command_listjobs)

# A delqueue command
list_parser = subparsers.add_parser('delqueue', help='Delete queue and all its jobs from the server')
list_parser.add_argument('queue_id', action='store', default=None, help='Queue ID to list')
list_parser.set_defaults(func=command_delqueue)

# A truncatequeue command
list_parser = subparsers.add_parser('truncqueue', help='Delete all jobs in the specified queue (does not delete the queue itself)')
list_parser.add_argument('queue_id', action='store', default=None, help='Queue ID to list')
list_parser.set_defaults(func=command_trucqueue)

# A deljob command
list_parser = subparsers.add_parser('deljob', help='Delete jobs from the server')
list_parser.add_argument('job_id', action='store', default=None, help='Job ID to delete', nargs='+')

# A queuejob command
list_parser = subparsers.add_parser('queuejob', help='Queue a job in the server')
list_parser.add_argument('queue_id',    action='store', default=None, help='Queue where the job should be queued')
list_parser.add_argument('output_file', action='store', default=None, help='Output file to store stdout for the process')
list_parser.add_argument('command',     action='store', default=None, help='Command with arguments to be executed', nargs='+')
list_parser.set_defaults(func=command_queuejob)

args = parser.parse_args()
args.func(args)


