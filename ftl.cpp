 #include "ftl.hh"
#include <chrono>

void full_write_preconditioning(ssd_info * ssd, bool seq){
	unsigned int total_size = ssd->parameter->lun_num * ssd->parameter->plane_lun *
						ssd->parameter->block_plane * ssd->parameter->page_block;
	total_size = total_size * (1-ssd->parameter->overprovide);
	cerr << "full write for total size " << total_size << " page ";
	// add write to table
	int lpn = 0;
	int ppn = -1;
	local * location = new local(0,0,0,0,0);
	for (int i = 0; i <  total_size; i++){
		if (!seq){
			if ((invalid_old_page(ssd, lpn, location) != SUCCESS)){
				#if DEBUG
				cout << "fail in invalid old page" << endl;
				#endif 
			}
			ppn = get_new_ppn (ssd, lpn, location, true);
			if (ppn == -1) {
				cout << "fail in precondition " << endl;
			}
		}else {
			if ((invalid_old_page(ssd, lpn, location) == SUCCESS)){
				#if DEBUG 
				cout << "seqential should not see the old page " << endl;
				#endif 
			}
			ppn = get_new_ppn(ssd, lpn, NULL, true);
			if (ppn == -1)
				cout << "fail in precondition 2" << endl;
		}
		if (write_page(ssd, lpn, ppn) == FAIL)
			cout << "full write precondition fail in write_page " << endl;
		bool gc = check_need_gc(ssd, ppn);
		if (gc) {
			if (seq) cerr << "should not have GC in sequential preconditioning " << endl;
			local * location = new local(0,0,0);
			find_location(ssd, ppn, location);
			pre_process_gc(ssd, location);
			delete location;
 		}
		if(seq) {
			lpn++;
			lpn = lpn % total_size;
		}else
			lpn = rand() % total_size;
	}
	cerr << "is complete. erase count: " <<  ssd->stats->flash_erase_count
				<< ". move count: " << ssd->stats->gc_moved_page << endl;
	ssd->stats->flash_erase_count  = 0;
	ssd->stats->gc_moved_page = 0;
}

ssd_info * distribute(ssd_info *ssd){

	if (ssd->dram != NULL)
		ssd->dram->current_time=ssd->current_time;

	request * req = ssd->request_tail; // The request we want to distribute
	if (req->size == 0) {
		cout << "size zero " << endl; 
		return ssd; 
	}
	unsigned lsn  = req->lsn;
	unsigned last_lpn  = (req->lsn+req->size-1)/ssd->parameter->subpage_page;
	unsigned first_lpn = req->lsn/ssd->parameter->subpage_page;
	unsigned lpn  = first_lpn;

	if (req->operation == READ)
	{
		while(lpn<=last_lpn)
		{
			sub_request * sub     = create_sub_request(ssd,lpn, req,req->operation, req->io_num);
			if (service_in_buffer(ssd, sub) != SUCCESS)
				printf("Error in servicing a request in buffer \n");

			lpn++;
		}
	}
	else if(req->operation==WRITE)
	{
		while (lpn<=last_lpn)
		{
			sub_request * sub=create_sub_request(ssd,lpn, req,req->operation, req->io_num);
			if (service_in_buffer(ssd, sub) != SUCCESS)
				printf("Error in servicing write requset in buffer! \n");
			lpn++;
		}
	} else {
		// FIXME for TRIM
	}
	return ssd;
}
STATE service_in_buffer(ssd_info * ssd, sub_request * sub){
	// check mapping table and find the request
	if (sub->operation == READ){
		buffer_entry * buf_ent = NULL;
		if (ssd->dram->buffer->buffer_capacity == 0) {
			sub->buf_entry = NULL;
			service_in_flash(ssd, sub);
			return SUCCESS;
		}

		if (ssd->dram->map->map_entry[sub->lpn].buf_ent != NULL){
			buf_ent = ssd->dram->map->map_entry[sub->lpn].buf_ent;
			ssd->dram->buffer->hit_read(buf_ent);
			sub->complete_time = ssd->current_time + 1000;
			sub->buf_entry= buf_ent;
			change_subrequest_state(ssd, sub,SR_MODE_ST_S,
				ssd->current_time,SR_MODE_COMPLETE,sub->complete_time);
		}else {
			service_in_flash(ssd, sub);
		}
	}
	else if (sub->operation == WRITE){
		if (ssd->dram->buffer->buffer_capacity == 0) {
			sub->buf_entry = NULL;
			service_in_flash(ssd, sub);
			return SUCCESS;
		}
		buffer_entry * buf_ent = NULL;
		if (ssd->dram->map->map_entry[sub->lpn].buf_ent == NULL) {
			// Add a new entry to buffer
			buf_ent = ssd->dram->buffer->add_head(sub->lpn);
			sub->buf_entry = NULL;

			if (buf_ent->outlier) {
				buf_ent->sub = sub;
			}else {
				ssd->dram->map->map_entry[sub->lpn].buf_ent = buf_ent;
				// Mark the request as complete
				sub->complete_time = ssd->current_time + 1000;
				// cout << "Delay buffer add (" << sub->lpn << "," << sub->location->plane << ")  " << sub->complete_time - sub->begin_time << endl;
				change_subrequest_state(ssd, sub,SR_MODE_ST_S,ssd->current_time,
							SR_MODE_COMPLETE,sub->complete_time);
				buf_ent->sub = NULL;
			}


			sub_request * evict_sub = create_sub_request(ssd, sub->lpn,NULL, WRITE, sub->io_num);
			evict_sub->buf_entry = buf_ent;
			service_in_flash(ssd, evict_sub);

		}else {	 // write to an entry in the buffer, just need to change the entry and make it dirty
			buf_ent = ssd->dram->map->map_entry[sub->lpn].buf_ent;
			ssd->dram->buffer->hit_write(buf_ent);
			sub->complete_time = ssd->current_time + 1000;
			// cout << "Delay buffer hit (" << sub->lpn << "," << sub->location->plane << ")  " << sub->complete_time - sub->begin_time << endl;
			change_subrequest_state(ssd, sub,SR_MODE_ST_S,
					ssd->current_time,SR_MODE_COMPLETE,sub->complete_time);
			sub->buf_entry = NULL;
		}

	}
	return SUCCESS;
}
void service_in_flash(ssd_info * ssd, sub_request * sub){
	if (sub->operation == READ) {
		if (ssd->channel_head[sub->location->channel]->
			lun_head[sub->location->lun]->
			rsubs_queue.find_subreq(sub)){
			// the request already exists
			sub->complete_time=ssd->current_time+1000;
			change_subrequest_state(ssd, sub,SR_MODE_ST_S,
					ssd->current_time,SR_MODE_COMPLETE,
					sub->complete_time);
			sub->state_current_time = sub->next_state_predict_time;
			ssd->stats->queue_read_count++;
		} else {
			ssd->channel_head[sub->location->channel]->
					lun_head[sub->location->lun]->
					rsubs_queue.push_tail(sub);
		}
	} else if (sub->operation == WRITE){
		if(invalid_old_page(ssd, sub) == FAIL) {
			#if DEBUG 
			cout << "error in invaliding the old page " << endl;
			#endif 
		}
		sub->ppn = get_new_ppn(ssd, sub->lpn, sub->location, true);

		if(sub->ppn == -1 || write_page(ssd, sub->lpn, sub->ppn) == FAIL) {
			cout << "there is problem "<< sub->ppn << endl;
			return;
		}
		find_location(ssd, sub->ppn, sub->location);
		ssd->channel_head[sub->location->channel]->lun_head[sub->location->lun]->
			wsubs_queue.push_tail(sub);

		Schedule_GC(ssd, sub);
	}
}

ssd_info *process( ssd_info *ssd)   {
	// use some random to stop always prioritizing a single channel (channel 0)
	int random = rand();
	for(int chan=0;chan<ssd->parameter->channel_number;chan++)
	{
		int c = ( chan + random ) % ssd->parameter->channel_number;
		unsigned int flag=0;
		if(find_channel_state(ssd, c) == CHANNEL_MODE_IDLE)
		{
			services_2_io(ssd, c, &flag);
			if(flag == 0)
				services_2_gc(ssd, c, &flag);
		}
	}
	return ssd;
}

void services_2_io(ssd_info * ssd, unsigned int channel, unsigned int * channel_busy_flag){
	int64_t read_transfer_time = 7 * ssd->parameter->time_characteristics.tWC +
			(ssd->parameter->subpage_page * ssd->parameter->subpage_capacity) *
			ssd->parameter->time_characteristics.tRC;
	int64_t read_time = ssd->parameter->time_characteristics.tR;
	int64_t write_transfer_time = 7 * ssd->parameter->time_characteristics.tWC +
			(ssd->parameter->subpage_page * ssd->parameter->subpage_capacity) *
			ssd->parameter->time_characteristics.tWC;
	int64_t write_time = ssd->parameter->time_characteristics.tPROG;
	int64_t channel_busy_time = 0;
	int64_t lun_busy_time = 0;
	sub_request ** subs;
	unsigned int subs_count = 0;
	unsigned int max_subs_count = ssd->parameter->plane_lun;
	subs = new sub_request *[max_subs_count];
	unsigned int lun;
	int random = rand();
	for (unsigned int c = 0; c < ssd->channel_head[channel]->lun_num; c++){
		for (int i = 0; i < max_subs_count; i++) subs[i] = NULL;
		subs_count = 0;
		lun = (c + random ) % ssd->channel_head[channel]->lun_num;
		if (find_lun_state(ssd , channel, lun) == LUN_MODE_IDLE &&
			ssd->channel_head[channel]->lun_head[lun]->GCMode == false){
			int operation = -1;
			subs_count = find_lun_io_requests(ssd, channel, lun, subs, &operation);
			if ( subs_count  == 0 ) continue;

			// Process collected sub requests
			switch (operation){
				case READ:
					if (subs_count > 1) ssd->stats->read_multiplane_count++;
					channel_busy_time = subs_count * read_transfer_time;
					lun_busy_time = channel_busy_time + read_time;
					break;
				case WRITE:
					if (subs_count > 1) ssd->stats->write_multiplane_count++;
					channel_busy_time = subs_count * write_transfer_time;
					lun_busy_time = channel_busy_time + write_time;
					break;
				default:
					cout << "Error: wrong operation (cannot be erase) " << endl;
			}

			for (int i = 0; i < max_subs_count; i++){
				if (subs[i] == NULL) continue;
				subs[i]->complete_time = ssd->current_time + lun_busy_time;
				change_subrequest_state (ssd, subs[i],
						SR_MODE_ST_S, ssd->current_time,
						SR_MODE_COMPLETE , subs[i]->complete_time);
				change_plane_state(ssd, subs[i]->location->channel,  
						subs[i]->location->lun, subs[i]->location->plane,
						PLANE_MODE_IO, ssd->current_time,
						PLANE_MODE_IDLE, subs[i]->complete_time);

				if (subs[i]->trigger_gc) {
					ssd->channel_head[channel]->lun_head[lun]->GCMode = true;
				}
				if (subs[i]->buf_entry != NULL){
					write_cleanup(ssd, subs[i]);

					delete subs[i];
				}
			}

			if (subs_count > 0){
				change_lun_state (ssd, channel, lun, LUN_MODE_IO,
						ssd->current_time, LUN_MODE_IDLE,
						ssd->current_time + lun_busy_time);
				change_channel_state(ssd, channel, CHANNEL_MODE_IO,
						ssd->current_time, CHANNEL_MODE_IDLE,
						ssd->current_time + channel_busy_time);
				*channel_busy_flag = 1;
				break;
			}
		}
	}
	delete [] subs;

}

void write_cleanup(ssd_info * ssd, sub_request * sub){
	// remove the entry from the buffer since the request is done
	buffer_entry * buf_ent = sub->buf_entry;
	if (buf_ent == NULL) return;

	if (buf_ent->gc)
		ssd->dram->gc_buffer->remove_entry(buf_ent);
	else
		ssd->dram->buffer->remove_entry(buf_ent);

	int lpn = buf_ent->lpn;
	ssd->dram->map->map_entry[lpn].buf_ent = NULL;

	int64_t complete_time = sub->complete_time;

	if (buf_ent->outlier){
		if (buf_ent->sub != sub) {
			buf_ent->sub->complete_time = complete_time;

			change_subrequest_state (ssd, buf_ent->sub,
				SR_MODE_ST_S, ssd->current_time,
				SR_MODE_COMPLETE, complete_time + 1000);
		}
		buf_ent->sub->buf_entry = NULL;

		delete buf_ent;
	}else {
		if (buf_ent->sub != NULL)
			cout << "there is a problem here " << endl;

		// By removing one subrequest from buffer, we might put another sub request in the buffer
		buffer_entry * outlier_entry = NULL;
		if (buf_ent->gc)
			outlier_entry = ssd->dram->gc_buffer->move_outlier();
		else
			outlier_entry = ssd->dram->buffer->move_outlier();

		delete buf_ent;

		if (outlier_entry != NULL) {
			sub_request * outlier_sub = outlier_entry->sub;
			if (outlier_sub == NULL)
				cout << "outlier sub cannot be null" << endl;
			outlier_entry->sub = NULL;
			ssd->dram->map->map_entry[outlier_sub->lpn].buf_ent = outlier_entry;

			outlier_sub->complete_time = complete_time + 1000;

			change_subrequest_state(ssd, outlier_sub,
				SR_MODE_ST_S, ssd->current_time,
				SR_MODE_COMPLETE, complete_time+1000);
			outlier_sub->buf_entry = NULL;

		}
	}
}


int find_lun_io_requests(ssd_info * ssd, unsigned int channel,
				unsigned int lun, sub_request ** subs, int * operation){
	int max_subs_count = ssd->parameter->plane_lun;
	unsigned int page_offset = -1;
	int subs_count = 0;
	(*operation) = -1;
	for (int i = 0; i < max_subs_count; i++){
		if (subs[i] == NULL) continue;
		subs_count++;
		(*operation) = subs[i]->operation;
		page_offset = subs[i]->location->page;
	}
	if ((*operation) == ERASE ) return subs_count;

	for (unsigned plane = 0; plane < ssd->parameter->plane_lun; plane++){
		if (subs[plane] != NULL ) continue;
		if ((*operation) == -1 || (*operation) == READ ) {
			subs[plane] = ssd->channel_head[channel]->lun_head[lun]->
						rsubs_queue.target_request(plane, -1, page_offset);
			if (subs[plane] != NULL) {
				ssd->channel_head[channel]->lun_head[lun]->rsubs_queue.remove_node(subs[plane]);
				page_offset = subs[plane]->location->page;
				subs_count++;
				(*operation) = subs[plane]->operation;
			}
		}
	}

	if ((*operation) == READ) return subs_count;


	for (unsigned plane = 0; plane < ssd->parameter->plane_lun; plane++){
		if (subs[plane] != NULL ) continue;

		if ((*operation) == -1 || (*operation) == WRITE ){
			subs[plane] = ssd->channel_head[channel]->lun_head[lun]->
						wsubs_queue.target_request(plane, -1, page_offset);
			if (subs[plane] != NULL) {
				ssd->channel_head[channel]->lun_head[lun]->wsubs_queue.remove_node(subs[plane]);
				page_offset = subs[plane]->location->page;
				subs_count++;
				(*operation) = subs[plane]->operation;
			}
		}
	}

	return subs_count;
}

void services_2_gc(ssd_info * ssd, unsigned int channel,unsigned int * channel_busy_flag) {
	int64_t read_transfer_time = 7 * ssd->parameter->time_characteristics.tWC +
					(ssd->parameter->subpage_page * ssd->parameter->subpage_capacity) *
					ssd->parameter->time_characteristics.tRC;
	int64_t read_time =  ssd->parameter->time_characteristics.tR;
	int64_t write_transfer_time = 7 * ssd->parameter->time_characteristics.tWC +
					(ssd->parameter->subpage_page * ssd->parameter->subpage_capacity) *
					ssd->parameter->time_characteristics.tWC;
	int64_t write_time =  ssd->parameter->gc_time_ratio * ssd->parameter->time_characteristics.tPROG;
	int64_t erase_transfer_time = 5 * ssd->parameter->time_characteristics.tWC;
	int64_t erase_time = ssd->parameter->time_characteristics.tBERS;

	sub_request ** subs = new sub_request * [ssd->parameter->plane_lun];

	int subs_count = 0;
	int random = rand() % ssd->channel_head[channel]->lun_num;
	unsigned int lun = 0;
	for (unsigned int c = 0; c < ssd->channel_head[channel]->lun_num; c++){
		// select the order of luns randomly
		lun = (c + random) % ssd->channel_head[channel]->lun_num;

		if (find_lun_state(ssd , channel, lun) != LUN_MODE_IDLE ) continue;
			// ssd->channel_head[channel]->lun_head[lun]->GCMode != true) continue;
		// collect sub_requests for different planes
		for (int i = 0; i < ssd->parameter->plane_lun; i++) subs[i] = NULL;
		subs_count = 0;

		int64_t lun_busy_time = 0;
		int64_t channel_busy_time = 0;

		int operation = -1;
		subs_count = find_lun_gc_requests(ssd, channel, lun, subs, &operation);
		if (subs_count == 0) continue;


		// find the total latency to service all sub-requests
		switch(operation){
			case READ:
               			if (subs_count > 1) ssd->stats->read_multiplane_count++;
				channel_busy_time =  read_transfer_time * subs_count;
				lun_busy_time = channel_busy_time + read_time;
				break;
			case WRITE:
                		if (subs_count > 1) ssd->stats->write_multiplane_count++;
				channel_busy_time = write_transfer_time * subs_count;
				lun_busy_time = channel_busy_time + write_time;
				break;
			case ERASE:
                		if (subs_count > 1) ssd->stats->erase_multiplane_count++;
				channel_busy_time =  erase_transfer_time * subs_count;
				lun_busy_time =  channel_busy_time + erase_time;
				break;
			default:
				cout << "Error in the operation: " << operation << endl;
		}
	
		// service all sub_requests
		for (int i = 0; i < ssd->parameter->plane_lun; i++){
			if (subs[i] == NULL) continue;

			subs[i]->complete_time = ssd->current_time + lun_busy_time;
			change_subrequest_state(ssd, subs[i],
				SR_MODE_ST_S, ssd->current_time, SR_MODE_COMPLETE ,
				ssd->current_time + lun_busy_time);

			change_plane_state (ssd, subs[i]->location->channel,
				subs[i]->location->lun, subs[i]->location->plane,
				PLANE_MODE_GC, ssd->current_time, PLANE_MODE_IDLE,
				ssd->current_time + lun_busy_time);

			if (operation == ERASE){
				delete_gc_node(ssd, subs[i]->gc_node);
				ssd->channel_head[subs[i]->location->channel]->lun_head[subs[i]->location->lun]->GCMode = false;
			}

			if (subs[i]->buf_entry != NULL){
				write_cleanup(ssd, subs[i]);
			}

			delete subs[i];
		}

		// make channel, and lun busy
		if (subs_count != 0) {
			change_lun_state (ssd, channel, lun, LUN_MODE_GC, ssd->current_time ,
					LUN_MODE_IDLE, ssd->current_time +   lun_busy_time);
			change_channel_state(ssd, channel, CHANNEL_MODE_GC, ssd->current_time ,
					CHANNEL_MODE_IDLE, ssd->current_time + channel_busy_time);
			*channel_busy_flag = 1;
			break;
		}


	}
	delete [] subs;
}

int find_lun_gc_requests(ssd_info * ssd, unsigned int channel, unsigned int lun,
						sub_request ** subs, int * operation) {
	int max_subs_count = ssd->parameter->plane_lun;
	unsigned int page_offset = -1;
	int subs_count = 0;
	(*operation) = -1;
	static int k = 0; 
	for (int i = 0; i < max_subs_count; i++){
		if (subs[i] != NULL) {
			subs_count++;
			(*operation) = subs[i]->operation;
			page_offset = subs[i]->location->page;
		}
	}

	if (ssd->channel_head[channel]->lun_head[lun]->GCSubs.is_empty()) return subs_count;
	for (int i = 0; i < max_subs_count; i++){
		if (subs[i] != NULL) continue;
		sub_request * temp = ssd->channel_head[channel]->lun_head[lun]->
					GCSubs.target_request(i, -1, page_offset);
		if (temp == NULL ||
			((*operation) != -1 && temp->operation != (*operation)))
			continue;  // can be improved
		ssd->channel_head[channel]->lun_head[lun]->GCSubs.remove_node(temp);
		if (find_subrequest_state(ssd, temp) == SR_MODE_COMPLETE) {
			i--;
			continue;
		}
		if (temp->operation == ERASE || temp->operation == READ) {
			subs[i] = temp;
			(*operation) = subs[i]->operation;
			subs_count++;
			continue;
		}
		// If it's write

		if ((temp->buf_entry != NULL) || (ssd->dram->gc_buffer->buffer_capacity== 0)){
			subs[i] = temp;
			(*operation) = subs[i]->operation;
			subs_count++;
			continue;
		}

		int lpn = temp->lpn;
		buffer_entry * buf_ent = ssd->dram->map->map_entry[lpn].buf_ent;
		if (buf_ent != NULL) {
			if (buf_ent->gc) 
				ssd->dram->gc_buffer->hit_write(buf_ent); 
			else 
				ssd->dram->buffer->hit_write(buf_ent);  
			i--;
			continue;
		}else {
			buf_ent = ssd->dram->gc_buffer->add_head(lpn);
			temp->buf_entry = buf_ent;
			buf_ent->gc = true;

			if (!buf_ent->outlier) {
				ssd->dram->map->map_entry[lpn].buf_ent= buf_ent;
				ssd->channel_head[channel]->lun_head[lun]->GCSubs.push_tail(temp);
				i--;
				continue;
			}else {
				buf_ent->sub = temp;
				subs[i] = temp;
				(*operation) = subs[i]->operation;
				subs_count++;
			}

		}

		if (ssd->parameter->plane_level_tech != GCGC)
			return subs_count; // NOT FOR GCGC
	}

	return subs_count;
}


// =============================================================================

sub_request * create_sub_request( ssd_info * ssd, int lpn,
				 request * req,unsigned int operation, int io_num = -1){

	sub_request * sub = new sub_request(ssd->current_time, lpn,
				ssd->subrequest_sequence_number++, operation);

	if(req!=NULL)
	{
		sub->next_subs = req->subs;
		req->subs = sub;

		sub->app_id = req->app_id;
		sub->io_num = req->io_num;

		sub->state_time[SR_MODE_WAIT] = ssd->current_time - req->time;
		sub->state_current_time = ssd->current_time;
	}
	else
	{
		sub->io_num = io_num;
		sub->state_time[SR_MODE_WAIT] = 0;
		sub->state_current_time = ssd->current_time;
	}

	if (operation == READ)
	{
		sub->ppn = ssd->dram->map->map_entry[lpn].pn;
		if (sub->ppn >= 0) {
			find_location(ssd,sub->ppn, sub->location);
			if (ssd->dram->map->map_entry[lpn].state)
				sub->state = 1;
			else
				sub->state = 0;
		}else {
			sub->state = 0;
		}
	}
	else if(operation == WRITE)
	{
		sub->ppn = -1; // ssd->dram->map->map_entry[lpn].pn;
		sub->state=0;
	}
	else
	{
		delete sub;
		printf("\n FIXME TRIM command \n");
		return NULL;
	}

	return sub;
}

STATE write_page(ssd_info * ssd, const int lpn, const int  ppn){
	if(update_map_entry(ssd, lpn, ppn) == FAIL) return FAIL;
	if (update_physical_page(ssd, ppn, lpn) == FAIL) return FAIL;
	return SUCCESS;
}
STATE update_map_entry(ssd_info * ssd, int lpn, int ppn){
	bool full_page= true;
	if (ppn == -1) {
		ssd->dram->map->map_entry[lpn].pn = -1;
		ssd->dram->map->map_entry[lpn].state = false;
		ssd->dram->map->map_entry[lpn].buf_ent = NULL;
	} else {
		ssd->dram->map->map_entry[lpn].pn=ppn;
		ssd->dram->map->map_entry[lpn].state = true;
	}

	return SUCCESS;
}
STATE update_physical_page(ssd_info * ssd, const int ppn, const int lpn){
	local * location = new local(0,0,0);
	find_location(ssd, ppn, location);

	int c = location->channel;
	int l = location->lun;
	int p = location->plane;
	int b = location->block;
	int pg = location->page;

	blk_info * block = ssd->channel_head[c]->lun_head[l]->plane_head[p]->blk_head[b];

	if (lpn != -1) {
		if (block->last_write_page > pg) {
			cout << "update physical page, writing backward " << block->last_write_page  << "  "  << pg<< endl;
			return FAIL;
		}
		ssd->channel_head[c]->lun_head[l]->plane_head[p]->blk_head[b]->page_head[pg]->lpn=lpn;
		ssd->channel_head[c]->lun_head[l]->plane_head[p]->blk_head[b]->page_head[pg]->valid_state=true;

		ssd->channel_head[c]->lun_head[l]->plane_head[p]->free_page--;
		ssd->stats->flash_prog_count++;
		ssd->channel_head[c]->program_count++;
		ssd->channel_head[c]->lun_head[l]->program_count++;
		ssd->channel_head[c]->lun_head[l]->plane_head[p]->program_count++;
	}else {

		if (block->page_head[pg]->lpn == -1 || block->page_head[pg]->valid_state == false) {
			cout << "invalidating has problem, the page is already invalid or free " << endl;
			return FAIL;
		}

		block->page_head[pg]->lpn=-1;
		if (block->page_head[pg]->valid_state){
			block->invalid_page_num++;
		}
		block->page_head[pg]->valid_state=false;
		ssd->channel_head[c]->lun_head[l]->plane_head[p]->invalid_page++;

	}
	delete location;
	return SUCCESS;
}
uint64_t set_entry_state(ssd_info *ssd, int lsn,unsigned int size){
	uint64_t temp,state,move;

	temp=~(0xffffffffffffffff<<size);
	move=lsn%ssd->parameter->subpage_page;
	state=temp<<move;

	return state;
}

STATE invalid_old_page(ssd_info * ssd, const int lpn, local * location){
	if (ssd->dram->map->map_entry[lpn].state == false) {
		if (ssd->current_time > 0){
			#if DEBUG 
			cout << "error in invalid old page error 1 " << endl;
			#endif 
		}
		return FAIL;
	}
	int old_ppn = ssd->dram->map->map_entry[lpn].pn;
	find_location(ssd, old_ppn, location);
	if( update_physical_page(ssd, old_ppn, -1) == FAIL) {
			cout << "could not update physical page " << endl;
			return FAIL;
	}
	if (update_map_entry(ssd, lpn, -1) == FAIL ) {
		cout << "could not update mapping entry " << endl;
		return FAIL;
	}
	return SUCCESS;
}

STATE invalid_old_page(ssd_info * ssd, sub_request * sub){ // const int lpn){
	int lpn = sub->lpn;
	if (ssd->dram->map->map_entry[lpn].state == false){
		#if DUBG 
		cout << "error 1 " << sub->lpn  << endl; 	
		#endif 
		 return FAIL;
	}
	int old_ppn = ssd->dram->map->map_entry[lpn].pn;
	find_location(ssd, old_ppn, sub->location);
	if( update_map_entry(ssd, lpn, -1) == FAIL) {
		cout << "error 2" << endl; 
		return FAIL;
	}
	if (update_physical_page(ssd, old_ppn, -1)  == FAIL) {
		cout << "error 3 " << endl; 
		return FAIL;
	} 
	return SUCCESS;
}

int get_new_ppn(ssd_info *ssd, int lpn, const local * location, bool hot){ // hot specify if we need a page in hot or cold active block 
	local * new_location;
	if (location != NULL) {
		new_location = new local(location->channel, location->lun, location->plane);
	}else {
		new_location = new local(0,0,0);
		allocate_plane(ssd, new_location);
	}
	if (allocate_page_in_plane(ssd, new_location, hot) != SUCCESS){
		delete new_location;
		return -1;
	}
	int ppn = find_ppn(ssd, new_location);
	delete new_location;
	return ppn;
}
int get_target_lun(ssd_info * ssd){
	int target_lun = ssd->lun_token;
	ssd->lun_token = (ssd->lun_token + 1) % ssd->parameter->lun_num;
	return target_lun;
}
int get_target_plane(ssd_info * ssd, int channel, int lun) {
	int target_plane = ssd->channel_head[channel]->lun_head[lun]->plane_token;
	ssd->channel_head[channel]->lun_head[lun]->plane_token =
			(ssd->channel_head[channel]->lun_head[lun]->plane_token + 1) % ssd->parameter->plane_lun;
	return target_plane;
}
STATE allocate_plane( ssd_info * ssd , local * location){
	sub_request * update=NULL;
	unsigned int channel_num=0,lun_num=0,plane_num=0;
	if (location == NULL) location = new local(0,0,0);
	channel_num = ssd->parameter->channel_number;
	lun_num=ssd->parameter->lun_channel[0];
	plane_num=ssd->parameter->plane_lun;

	unsigned int target_lun = get_target_lun(ssd);
	location->channel=target_lun % channel_num;
	location->lun= (target_lun / channel_num) % ssd->parameter->lun_channel[location->channel];
	location->plane= get_target_plane(ssd, location->channel, location->lun);
	return SUCCESS;
}
int get_active_block(ssd_info *ssd, local * location){
	int channel = location->channel;
	int lun = location->lun;
	int plane = location->plane;

	int active_block = ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->active_block;
	int free_page_num = ssd->channel_head[channel]->lun_head[lun]->
					plane_head[plane]->blk_head[active_block]->free_page_num;
	int cold_active_block = ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->cold_active_block; 
	int count=0;
	while(((free_page_num==0))&&(count<ssd->parameter->block_plane))
	{
		active_block=(active_block+1)%ssd->parameter->block_plane;
		if (active_block == cold_active_block) 
			active_block=(active_block+1)%ssd->parameter->block_plane;
		free_page_num=ssd->channel_head[channel]->lun_head[lun]->
					plane_head[plane]->blk_head[active_block]->free_page_num;
		count++;
	}
	if (count == ssd->parameter->block_plane) {
		cout << "could not find active_block " << endl;
		return -1;
	}
	ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->active_block=active_block;

	return active_block;
}

int get_cold_active_block(ssd_info *ssd, local * location){
	int channel = location->channel;
	int lun = location->lun;
	int plane = location->plane;

	int cold_active_block = ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->cold_active_block;
	int free_page_num = ssd->channel_head[channel]->lun_head[lun]->
					plane_head[plane]->blk_head[cold_active_block]->free_page_num;
	int active_block = ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->active_block; 
	int count=0;
	while(((free_page_num==0))&&(count<ssd->parameter->block_plane))
	{
		cold_active_block=(cold_active_block+1)%ssd->parameter->block_plane;
		if (cold_active_block == active_block) 
			cold_active_block=(cold_active_block+1)%ssd->parameter->block_plane;
		free_page_num=ssd->channel_head[channel]->lun_head[lun]->
					plane_head[plane]->blk_head[cold_active_block]->free_page_num;
		count++;
	}
	if (count == ssd->parameter->block_plane) {
		cout << "could not find cold active_block " << endl;
		return -1;
	}
	ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->cold_active_block=cold_active_block;

	return cold_active_block;
}

STATE allocate_page_in_plane( ssd_info *ssd, local * location, bool hot){
	int channel = location->channel;
	int lun = location->lun;
	int plane = location->plane;
	int active_block= -1; 
	if (hot) 
		active_block = get_active_block(ssd, location);
	else 
		active_block = get_cold_active_block(ssd, location); 

	if (active_block == -1) return FAIL;
	int active_page = ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->
					blk_head[active_block]->last_write_page + 1;

	ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->blk_head[active_block]->last_write_page++;
	ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->blk_head[active_block]->
					last_write_time = ssd->current_time;
	ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->blk_head[active_block]->free_page_num--;

	location->block = active_block;
	location->page = active_page;


	return SUCCESS;
}
void find_location(ssd_info * ssd, int ppn, local * location){
	int pn,ppn_value=ppn;
	int page_plane=0,page_lun=0,page_channel=0;

	pn = ppn;

	page_plane=ssd->parameter->page_block*ssd->parameter->block_plane;
	page_lun=page_plane*ssd->parameter->plane_lun;

	int page = ppn % (page_lun * ssd->parameter->lun_num);
	int channel = 0;
	while (true){
		page_channel = page_lun * ssd->parameter->lun_channel[location->channel];
		page = page - page_channel;
		if (page < 0){
			page = page + page_channel;
			break;
		}else {
			channel++;
		}
	}
	int lun = page/page_lun;
	int plane = (page%page_lun)/page_plane;
	int block = ((page%page_lun)%page_plane)/ssd->parameter->page_block;
	int page_ = (((page%page_lun)%page_plane)%ssd->parameter->page_block)%ssd->parameter->page_block;

	if (location == NULL) {
		location = new local(channel, lun, plane, block, page_);
	}else {
		location->channel = channel;
		location->lun     = lun;
		location->plane   = plane;
		location->block   = block;
		location->page    = page_;
	}
}
int find_ppn(ssd_info * ssd,const local * location){
	unsigned int channel = location->channel;
	unsigned int lun = location->lun;
	unsigned int plane = location->plane;
	unsigned int block = location->block;
	unsigned int page = location->page;

	int ppn=0;

	int page_plane=0,page_lun=0;
	int page_channel[100];

	page_plane=ssd->parameter->page_block*ssd->parameter->block_plane;
	page_lun=page_plane*ssd->parameter->plane_lun;

	unsigned int i=0;
	while(i<ssd->parameter->channel_number)
	{
		page_channel[i]=ssd->parameter->lun_channel[i]*page_lun;
		i++;
	}


	i=0;
	while(i<channel)
	{
		ppn=ppn+page_channel[i];
		i++;
	}
	ppn=ppn+page_lun*lun+page_plane*plane+block*ssd->parameter->page_block+page;

	return ppn;
}

bool check_need_gc(ssd_info * ssd, int ppn){
	local * location = new local(0,0,0);
	find_location(ssd, ppn, location);

	int free_page = ssd->channel_head[location->channel]->
				lun_head[location->lun]->plane_head[location->plane]->free_page;
	int all_page = ssd->parameter->page_block*ssd->parameter->block_plane;

	if (free_page > (all_page * ssd->parameter->gc_down_threshold)){
		delete location;
		return false;
	}
	delete location;
	return true;
}
