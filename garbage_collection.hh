#ifndef GARBAGE_COLLECTION_H
#define GARBAGE_COLLECTION_H 10000
#include <sys/types.h>
#include "ssd.hh"
#include "ftl.hh"
#include "common.hh"
STATE find_victim_block( ssd_info *ssd,local * location); 
STATE greedy_algorithm(ssd_info * ssd, local * location);
STATE fifo_algorithm(ssd_info * ssd, local * location);
STATE windowed_algorithm(ssd_info * ssd, local * location);
STATE RGA_algorithm(ssd_info * ssd, local * location);
STATE RANDOM_algorithm(ssd_info * ssd, local * location);
STATE RANDOM_p_algorithm(ssd_info * ssd, local * location);
STATE RANDOM_pp_algorithm(ssd_info * ssd, local * location);

unsigned int best_cost( ssd_info * ssd,  plane_info * the_plane, int active_block , int cold_active_block);
int64_t compute_moving_cost(ssd_info * ssd, const local * location, const local * twin_location, int approach); 
int erase_block(ssd_info * ssd,const local * location);
bool update_priority(ssd_info * ssd, unsigned int channel, unsigned int lun); 
STATE move_page(ssd_info * ssd, const local * location, gc_operation * gc_node); 
STATE add_gc_node(ssd_info * ssd, gc_operation * gc_node); 
int plane_emergency_state(ssd_info * ssd, const local * location); 
void update_subreq_state(ssd_info * ssd, const local * location, int64_t next_state_time); 
sub_request * create_gc_sub_request( ssd_info * ssd, const local * location, int operation, gc_operation * gc_node); 
bool Schedule_GC(ssd_info * ssd, sub_request * sub); 
void pre_process_gc(ssd_info * ssd, const local * location); 
STATE delete_gc_node(ssd_info * ssd, gc_operation * gc_node); 
#endif
