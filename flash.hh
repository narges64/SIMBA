#include "common.hh"
unsigned int find_plane_state(ssd_info * ssd , unsigned int channel, unsigned int lun, unsigned int plane); 
unsigned int find_lun_state (ssd_info * ssd, unsigned int channel, unsigned int lun);
unsigned int find_channel_state(ssd_info * ssd, unsigned int channel);  
void change_plane_state (ssd_info * ssd, unsigned int channel, unsigned int lun, unsigned int plane, unsigned int current_state, int64_t current_time, unsigned int next_state, int64_t next_time); 
void change_lun_state (ssd_info * ssd, unsigned int channel, unsigned int lun, unsigned int current_state, int64_t current_time, unsigned int next_state, int64_t next_time);
void change_channel_state(ssd_info * ssd, unsigned int channel, unsigned int current_state, int64_t current_time, unsigned int next_state, int64_t next_time); 

unsigned int find_subrequest_state(ssd_info * ssd, sub_request * sub); 
void change_subrequest_state(ssd_info * ssd, sub_request * sub, unsigned int current_state, int64_t current_time, unsigned int next_state , int64_t next_time);
