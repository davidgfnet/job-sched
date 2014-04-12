
CC=gcc
CXX=g++

OPTIONS=-DENABLE_MYSQL_BACKEND -DENABLE_SQLITE_BACKEND

CFLAGS=-ggdb $(OPTIONS)

OBJS=scheduler.o jobs.o output.o common.o main.o backend.o server.o
LIBS=-lpthread `mysql_config --libs` -lsqlite3

OBJS_CLIENT=common.o client.o

all:	$(OBJS) $(OBJS_CLIENT)
	$(CXX) -o job-sched-daemon $(OBJS) $(LIBS)
	$(CXX) -o job-sched $(OBJS_CLIENT) -lcurl

%.o:	%.cc
	$(CXX) $(CFLAGS) -o $@ -c $<
	
clean:
	rm -f job-sched $(OBJS) $(OBJS_CLIENT)

