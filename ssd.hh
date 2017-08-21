#include <fstream>
#include <iostream>
#include <stdlib.h>
#include <cstdlib>
#include <cmath>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <limits>
#include <sys/types.h>
#include "ftl.hh"



ssd_info *simulate(ssd_info *);
ssd_info *buffer_management(ssd_info *);
request * read_request_from_file(ssd_info * ssd, int64_t nearest_event_time);
int get_requests_consolidation(ssd_info *ssd);
//int select_trace_file(ssd_info * ssd);
int64_t find_nearest_event(ssd_info *);
unsigned int lpn2ppn(ssd_info * ,unsigned int lsn);
unsigned int transfer_size(ssd_info *,unsigned int,request *);
void trace_output(ssd_info* );
void print_statistics(ssd_info *, int);
void free_all_node(ssd_info *);
void add_to_request_queue(ssd_info * ssd, request * req);
void close_files(ssd_info * ssd) ;
void collect_gc_statistics(ssd_info * ssd, int app);
void restart_trace_files(ssd_info * ssd);
void collect_statistics(ssd_info * ssd, request * req);
void remove_request(ssd_info * ssd, request ** req, request ** pre_node);
void print_epoch_statistics(ssd_info * ssd, int app);
double gc_standard_deviation (ssd_info * ssd, int channel, int lun, int plane);
void generate_trace_file(ssd_info * ssd, char * name); 
