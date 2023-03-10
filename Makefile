CC=gcc
CFLAGS= -Wall -I./include `pkg-config --cflags --libs glib-2.0`
#VERSION=-std=c11
ODIR=obj
AR=ar
ARFLGAS=rcs

dynamic: lib/libSHARDS.so
	
lib/libSHARDS.so: obj/SHARDS.o obj/shard_utils.o
	$(CC)  -g -fPIC  $(VERSION)  -Wextra -pedantic $^ -shared $(CFLAGS) -o lib/libSHARDS.so

static: lib/libSHARDS.a
	
lib/libSHARDS.a: obj/SHARDS.o obj/shard_utils.o
	$(AR) $(ARFLAGS) lib/libSHARDS.a $^ 

# The -I./include from the CFLAGS is not really needed in thi step, as the header file for libSHARDS.so was already included en the shards_test.o .
# However it won't hurt the compiling process, which is why we leave that flag in the CFLAGS variable and use it here.

shards_test: obj/shards_test.o lib/libSHARDS.a
	$(CC) -g  $^ $(CFLAGS) $(VERSION) -o $@

trace_replay: obj/trace_replay.o lib/libSHARDS.a
	$(CC) -g  $^ $(CFLAGS) $(VERSION) -o $@

shards_test2: obj/shards_test.o lib/libSHARDS.so
	$(CC) -g $^ $(CFLAGS) $(VERSION) -o $@

obj/shards_test.o: src/shards_test.c
	$(CC) -g -c  src/shards_test.c $(CFLAGS)  $(VERSION) -o $@

obj/trace_replay.o: src/trace_replay.c
	$(CC) -g -c  src/trace_replay.c $(CFLAGS)  $(VERSION) -o $@

obj/SHARDS.o : src/SHARDS.c
	$(CC) -g -fPIC -c  $(CFLAGS) $(LFLAGS)  src/SHARDS.c -o $@

obj/shard_utils.o: src/shards_utils.c
	$(CC) -g  -fPIC -c $(CFLAGS) $(LFLAGS)  src/shards_utils.c -o $@



