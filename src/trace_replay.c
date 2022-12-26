/*
cpu is critical for fast spdk applications, unlike traditional storage systems.
disk speed becomes faster -> cpu utilization goes higher, achieve this by increasing -x
MRC more frequent -> cpu utilization goes higher, achieve this by reducing -i
t: path to trace files
i: interval between two MRC construction in seconds
b: bucket size of reuse distance histogram, defines MRC resolution -> the smaller the higher
r: initial sample rate, default to 1, goes down gradually.
s: fixed size SHARDS, the size of all samples
x: the speed up of IO response time, 10 -> 10 times faster
*/
// #define _XOPEN_SOURCE   600
// #define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <dirent.h>
#include <glib.h>
#include <time.h>
#include <stdint.h>
#include "SHARDS.h"

void find_trace_files(char *trace_path, SHARDS **shards);

#define MAX_BUF_SIZE 512
#define BLK_SIZE 512

#define TIMESTAMP   0
#define RESPONSE_TIME   1
#define IO_TYPE 2
#define LUN 3
#define OFFSET  4
#define IO_SIZE   5

double last_mrc = 0.0;
unsigned int mrc_interval_sec = 600; // vary this
unsigned int bucket_size = 10;
unsigned int sample_size = 32000;
unsigned int speed_up = 1;
double initial_r = 1.0;

char trace_path[MAX_BUF_SIZE];

int main(int argc, char** argv)
{
    SHARDS *shards;
    int opt;

    memset(trace_path, '\0', MAX_BUF_SIZE);
    while ((opt = getopt(argc, argv, ":t:i:b:r:s:x:h")) != -1) {
        char *ptr;

        switch(opt) {
            case 't':
                memcpy(trace_path, optarg, strlen(optarg));
                break;
            case 'i':
                mrc_interval_sec = strtoul(optarg, &ptr, 10);
                break;
            case 'b':
                bucket_size = strtoul(optarg, &ptr, 10);
                break;
            case 'r':
                initial_r = strtod(optarg, &ptr);
                break;
            case 's':
                sample_size = strtoul(optarg, &ptr, 10);
                break;
            case 'x':
                speed_up = strtoul(optarg, &ptr, 10);
                break;
            case 'h':
                printf("-t, path to trace files\n");
                printf("-i, interval between two MRC construction in seconds\n");
                printf("-b, bucket size of reuse distance histogram, defines MRC resolution\n");
                printf("-r, initial sample rate\n");
                printf("-s, sample size of fixed-size SHARDS algorithm\n");
                printf("-x, the speed-up of IO response time\n");
                exit(0);
            case ':':
                printf("option needs a value\n"); 
                exit(-1);
            case '?':
                printf("unknown option: %c\n", optopt);
                break;
        }
    }

    if (trace_path[0] == '\0') {
        perror("trace file directory must be set");
        exit(-1);
    }

    shards = SHARDS_fixed_size_init_R(sample_size, initial_r, bucket_size, Uint64);
    find_trace_files(trace_path, &shards);
    SHARDS_free(shards);

    return 0;
}

void process_one_trace(char *trace_file, SHARDS **shards)
{
    char line[MAX_BUF_SIZE];
    FILE *file;

    printf("processing: %s\n", trace_file);
    file = fopen(trace_file, "r");
    while (fgets(line, sizeof(line), file)) {
        char *token =  strtok(line, ","), *ptr;
        uint64_t blk_start = 0, iosize = 0, blk_addr = 0;
        double timestamp = 0;
        unsigned int counter = 0, latency_usec = 0;

        while (token) {
            switch (counter++) {
                case TIMESTAMP:
                    GHashTable *mrc;
                    GList *keys, *first;

                    if (!strncmp(token, "Timestamp", strlen("Timestamp")))
                        break;

                    timestamp = strtod(token, &ptr);
                    if (timestamp - last_mrc < (double)mrc_interval_sec)
                        break;
                    
                    last_mrc = timestamp;
                    mrc = MRC_empty(*shards);
                    keys = g_hash_table_get_keys(mrc);
                    keys = g_list_sort(keys, (GCompareFunc) intcmp);
                    first = keys;
                    while (keys) {
                        // fprintf("%7d,%1.7f\n", *(int*)keys->data, 
                        //             *(double*)g_hash_table_lookup(mrc, keys->data));
                        g_hash_table_lookup(mrc, keys->data);
                        keys=keys->next;
                    }
                    g_list_free(first);
                    g_hash_table_destroy(mrc);
                    
                    break;
                case RESPONSE_TIME:
                    double latency_sec;

                    if (!strncmp(token, "Response", strlen("Response")))
                        break;

                    latency_sec = strtod(token, &ptr);
                    latency_usec = (unsigned int)(latency_sec * 1000 * 1000);
                    
                    break;
                case OFFSET: 
                    if (strncmp(token, "Offset", strlen("Offset")))
                        blk_start = strtoul(token, &ptr, 10);
                    
                    break;
                case IO_SIZE:
                    if (strncmp(token, "Size", strlen("Size")))
                        iosize = strtoul(token, &ptr, 10);
                    
                    break;
                default:
                    break;
            }
            token = strtok(NULL, ",");
        }
        assert(!(blk_start % BLK_SIZE) && !(iosize % BLK_SIZE));
        
        for (blk_addr = blk_start; blk_addr < blk_start + iosize; blk_addr += BLK_SIZE) {
            char *object = (char *)malloc(sizeof(uint64_t));

            assert(object);
            memcpy(object, &blk_addr, sizeof(uint64_t));
            SHARDS_feed_obj(*shards, object, sizeof(uint64_t));
        }
        usleep((unsigned int)((double)latency_usec / (double)speed_up));
    }
    fclose(file);
}

void find_trace_files(char *trace_path, SHARDS **shards)
{
    struct dirent **namelist;
    char trace_file[MAX_BUF_SIZE];
    int n, i;

    n = scandir(trace_path, &namelist, 0, alphasort);
    assert(n >= 0);

    for (i = 0; i < n; i++) {
        if (!strcmp(namelist[i]->d_name, ".") || !strcmp(namelist[i]->d_name, ".."))
            continue;
        memset(trace_file, '\0', MAX_BUF_SIZE);
        memcpy(trace_file, trace_path, strlen(trace_path));
        memcpy(trace_file + strlen(trace_path), "/\0", strlen("/\0"));
        memcpy(trace_file + strlen(trace_path) + 1,
                                namelist[i]->d_name, 
                                strlen(namelist[i]->d_name));
        process_one_trace(trace_file, shards);
        free(namelist[i]);
    }
    free(namelist);
}