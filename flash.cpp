#include "flash.hh"
unsigned int find_lun_state (ssd_info * ssd, unsigned int channel, unsigned int lun){
	lun_info * the_lun = ssd->channel_head[channel]->lun_head[lun]; 
	if (the_lun->next_state_predict_time <= ssd->current_time) 
		return the_lun->next_state; 
	return the_lun->current_state; 
}
unsigned int find_plane_state(ssd_info * ssd , unsigned int channel, unsigned int lun, unsigned int plane){
	plane_info * the_plane = ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]; 
	if (the_plane->next_state_predict_time <= ssd->current_time) 
		return the_plane->next_state; 
	return the_plane->current_state; 	
}
unsigned int find_channel_state(ssd_info * ssd, unsigned int channel){
	channel_info * the_channel = ssd->channel_head[channel]; 
	if (the_channel->next_state_predict_time <= ssd->current_time) 
		return the_channel->next_state; 
	return the_channel->current_state; 	
}
void change_lun_state (ssd_info * ssd, unsigned int channel, unsigned int lun, unsigned int current_state, int64_t current_time, unsigned int next_state, int64_t next_time){
	// have been in the next state for some time 
	int state1 = ssd->channel_head[channel]->lun_head[lun]->current_state; 
	int state2 = ssd->channel_head[channel]->lun_head[lun]->next_state; 
	int64_t state1_time = ssd->channel_head[channel]->lun_head[lun]->current_time; 
	int64_t state2_time = ssd->channel_head[channel]->lun_head[lun]->next_state_predict_time; 

	ssd->channel_head[channel]->lun_head[lun]->state_time[state1] += ((state2_time - state1_time) > 0)? state2_time - state1_time: 0; 
	ssd->channel_head[channel]->lun_head[lun]->state_time[state2] += ((current_time - state2_time) > 0)? (current_time - state2_time): 0; 
	
	ssd->channel_head[channel]->lun_head[lun]->current_state=current_state;	
	ssd->channel_head[channel]->lun_head[lun]->current_time=current_time;
	
	ssd->channel_head[channel]->lun_head[lun]->next_state=next_state; 	
	ssd->channel_head[channel]->lun_head[lun]->next_state_predict_time=next_time;
}
void change_plane_state (ssd_info * ssd, unsigned int channel, unsigned int lun, unsigned int plane, unsigned int current_state, int64_t current_time, unsigned int next_state, int64_t next_time){
	int state1 = ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->current_state; 
	int state2 = ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->next_state; 
	int64_t state1_time = ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->current_time; 
	int64_t state2_time = ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->next_state_predict_time; 	
	ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->state_time[state1] += (state2_time - state1_time)>0? state2_time - state1_time : 0; 
	ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->state_time[state2] += (current_time - state2_time)>0? current_time - state2_time : 0;  
	ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->current_state=current_state;	
	ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->current_time=current_time;
	ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->next_state=next_state; 	
	ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->next_state_predict_time=next_time;

/*
	sub_request * rhead = ssd->channel_head[channel]->lun_head[lun]->rsubs_queue.queue_head; 
	while(rhead != NULL){
		if (rhead->location->plane == plane){
			if (current_state == PLANE_MODE_GC)
				change_subrequest_state (ssd, rhead, SR_MODE_GCC_S, current_time, SR_MODE_WAIT, next_time);
			else 
				change_subrequest_state (ssd, rhead, SR_MODE_IOC_S, current_time, SR_MODE_WAIT, next_time);
				 	
		}
		else {
			if (current_state == PLANE_MODE_GC) 
				change_subrequest_state(ssd, rhead, SR_MODE_GCC_O, current_time, SR_MODE_WAIT, next_time); 
			else
				change_subrequest_state(ssd, rhead, SR_MODE_IOC_O, current_time, SR_MODE_WAIT, next_time); 
		}
		rhead = rhead->next_node; 
	}	
	sub_request * whead = ssd->channel_head[channel]->lun_head[lun]->wsubs_queue.queue_head; 
	while(whead != NULL){
		if (whead->location->plane == plane){
			if (current_state == PLANE_MODE_GC)
				change_subrequest_state (ssd, whead, SR_MODE_GCC_S, current_time, SR_MODE_WAIT, next_time);
			else 
				change_subrequest_state (ssd, whead, SR_MODE_IOC_S, current_time, SR_MODE_WAIT, next_time);
				 	
		}
		else {
			if (current_state == PLANE_MODE_GC) 
				change_subrequest_state(ssd, whead, SR_MODE_GCC_O, current_time, SR_MODE_WAIT, next_time); 
			else
				change_subrequest_state(ssd, whead, SR_MODE_IOC_O, current_time, SR_MODE_WAIT, next_time); 
		}
		whead = whead->next_node; 
	}	
	sub_request * gchead = ssd->channel_head[channel]->lun_head[lun]->GCSubs.queue_head; 

	while(rhead != NULL){
		if (gchead->location->plane == plane){
			if (current_state == PLANE_MODE_GC)
				change_subrequest_state (ssd, gchead, SR_MODE_GCC_S, current_time, SR_MODE_WAIT, next_time);
			else 
				change_subrequest_state (ssd, gchead, SR_MODE_IOC_S, current_time, SR_MODE_WAIT, next_time);
				 	
		}
		else {
			if (current_state == PLANE_MODE_GC) 
				change_subrequest_state(ssd, gchead, SR_MODE_GCC_O, current_time, SR_MODE_WAIT, next_time); 
			else
				change_subrequest_state(ssd, gchead, SR_MODE_IOC_O, current_time, SR_MODE_WAIT, next_time); 
		}
		gchead = gchead->next_node; 
	}	
*/
}

void change_channel_state(ssd_info * ssd, unsigned int channel, unsigned int current_state, int64_t current_time, unsigned int next_state, int64_t next_time){	
	int state1 = ssd->channel_head[channel]->current_state; 
	int state2 = ssd->channel_head[channel]->next_state; 
	int64_t state1_time = ssd->channel_head[channel]->current_time; 
	int64_t state2_time = ssd->channel_head[channel]->next_state_predict_time; 
	
	ssd->channel_head[channel]->state_time[state1] += (state2_time - state1_time)>0? state2_time - state1_time : 0; 
	ssd->channel_head[channel]->state_time[state2] += (current_time - state2_time)>0? current_time - state2_time : 0;  
	
	int prev_state = ssd->channel_head[channel]->current_state;  
	ssd->channel_head[channel]->state_time[prev_state] += current_time - ssd->channel_head[channel]->current_time; 
	
	ssd->channel_head[channel]->current_state=current_state;	
	ssd->channel_head[channel]->current_time=current_time;
	
	ssd->channel_head[channel]->next_state=next_state; 	
	ssd->channel_head[channel]->next_state_predict_time=next_time; 
}

unsigned int find_subrequest_state(ssd_info * ssd, sub_request * sub){	
	if (sub->next_state_predict_time <= ssd->current_time)
		return sub->next_state; 
	return sub->current_state; 	
}
void change_subrequest_state(ssd_info * ssd, sub_request * sub, unsigned int current_state, int64_t current_time, unsigned int next_state , int64_t next_time){
/*	int state1 = sub->current_state; 
	int state2 = sub->next_state; 
	int64_t state1_time = sub->current_time; 
	int64_t state2_time = sub->next_state_predict_time; 
	
	sub->state_time[state1] += (state2_time - state1_time)>0? state2_time - state1_time : 0; 
	sub->state_time[state2] += (current_time - state2_time)>0? current_time - state2_time : 0;  
*/	
	sub->current_state = current_state; 
	sub->current_time = current_time; 
	sub->next_state = next_state; 
	sub->next_state_predict_time = next_time; 

}
