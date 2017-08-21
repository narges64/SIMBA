#ifndef FTL_H
#define FTL_H 100000

#include "common.hh"
#include "garbage_collection.hh"
#include "flash.hh"

ssd_info *process( ssd_info *);
ssd_info *distribute(ssd_info *); 
sub_request * create_sub_request( ssd_info * ssd, int lpn, request * req,unsigned int operation, int io_num);
void services_2_gc(ssd_info * ssd, unsigned int channel, unsigned int * channel_busy_flag);
void services_2_io(ssd_info * ssd, unsigned int channel, unsigned int * channel_busy_flag);
int find_lun_io_requests(ssd_info * ssd, unsigned int channel, unsigned int lun, sub_request ** subs, int * operation);
int find_lun_gc_requests(ssd_info * ssd, unsigned int channel, unsigned int lun, sub_request ** subs, int * operation);
void find_location(ssd_info *ssd,int ppn, local * location);
int find_ppn(ssd_info * ssd, const local * location);
void full_sequential_write(ssd_info * ssd);
int get_new_ppn(ssd_info *ssd, int lpn, const local * location, bool hot);
STATE allocate_plane( ssd_info * ssd , local * location);
uint64_t set_entry_state(ssd_info *ssd, int lsn,unsigned int size);
STATE  allocate_page_in_plane( ssd_info *ssd, local * location, bool hot);
int get_target_lun(ssd_info * ssd);
int get_target_plane(ssd_info * ssd, unsigned int channel, unsigned int lun);
bool check_need_gc(ssd_info * ssd, int ppn);
void full_write_preconditioning(ssd_info * ssd, bool seq);
STATE invalid_old_page(ssd_info * ssd, sub_request * sub ); // const int lpn);
STATE invalid_old_page(ssd_info * ssd, const int lpn, local * location);
STATE write_page(ssd_info * ssd, const int lpn, const int  ppn);
int get_active_block(ssd_info * ssd, local * location);
int get_cold_active_block(ssd_info * ssd, local * location);
STATE service_in_buffer(ssd_info * ssd, sub_request * sub);
void service_in_flash(ssd_info * ssd, sub_request * sub);
STATE update_map_entry(ssd_info * ssd, int lpn, int ppn);
STATE update_physical_page(ssd_info * ssd, const int ppn, const int lpn);
void write_cleanup(ssd_info * ssd, sub_request * sub); 
#endif
