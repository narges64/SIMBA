#include "garbage_collection.hh"
#include <chrono>
using namespace std::chrono; 

void gc_for_plane(ssd_info * ssd, gc_operation * gc_node){
	unsigned int page_move_count = 0;
	local * location = gc_node->location;
	if (find_victim_block(ssd, location) != SUCCESS) {
		printf("Error: invalid block selected for gc \n");
		return;
	}
	int last_write = ssd->channel_head[location->channel]->lun_head[location->lun]->plane_head[location->plane]->blk_head[location->block]->last_write_page; 
	for(int i=0;i<=last_write;i++)
	{
		if(ssd->channel_head[location->channel]->lun_head[location->lun]->plane_head[location->plane]->blk_head[location->block]->page_head[i]->valid_state == true)
		{
			location->page=i;
			if (move_page(ssd, location, gc_node) == FAIL){
				cout << "Error: problem while moving valid pages in GC " << endl;
				return;
			}
			page_move_count++;
		}
	}
	ssd->stats->gc_moved_page += page_move_count;
	
	sub_request * erase_subreq = create_gc_sub_request( ssd, location, ERASE, gc_node);
	ssd->channel_head[location->channel]->lun_head[location->lun]->GCSubs.push_tail(erase_subreq);
	erase_block(ssd,location);
}

STATE find_victim_block(ssd_info * ssd, local * location){
	high_resolution_clock::time_point t1 = high_resolution_clock::now();
	switch (ssd->parameter->gc_algorithm) {
		case GREEDY:
				greedy_algorithm(ssd, location);

				break;
		case FIFO:
				fifo_algorithm(ssd, location);

				break;
		case WINDOWED:
				windowed_algorithm(ssd, location);

				break;
		case RGA:
				RGA_algorithm(ssd, location); // select the best one based on valid number

				break;
		case RANDOM:
				RANDOM_algorithm(ssd, location);

				break;
		case RANDOMP:
				RANDOM_p_algorithm(ssd, location);

				break;
		case RANDOMPP:
				RANDOM_pp_algorithm(ssd, location);

				break;

		default:
		{
			printf("Wrong garbage collection in parameters %d\n", ssd->parameter->gc_algorithm);
			return ERROR;
		}

	}

  	high_resolution_clock::time_point t2 = high_resolution_clock::now();
  	double dif = duration_cast<nanoseconds>( t2 - t1 ).count();
  	// printf ("Elasped time is %lf nanoseconds.\n", dif );

	if (location->block == -1){
		printf("Error: Problem in finding the victim block, time: %lld    gc alg %d \n", ssd->current_time , ssd->parameter->gc_algorithm );
		return FAIL;

	}

	if (ssd->channel_head[location->channel]->lun_head[location->lun]->	
								plane_head[location->plane]->blk_head[location->block]->free_page_num >= ssd->parameter->page_block){
		cout << "Error: too much free page in selected victim \n";
		
		cout << ssd->channel_head[location->channel]->lun_head[location->lun]->plane_head[location->plane]->blk_head[location->block]->free_page_num << endl;
		return FAIL;
	}

	return SUCCESS;
}
sub_request * create_gc_sub_request( ssd_info * ssd,const local * location, int operation, gc_operation * gc_node){

	if (gc_node != NULL)	{
		if (location->channel != gc_node->location->channel || location->lun != gc_node->location->lun){
			cout << "Error in location and gc_node incompatible! " << endl;
			return NULL;
		}
	}

	sub_request * sub = new sub_request(ssd->current_time, -1, ssd->subrequest_sequence_number++,operation);

	sub->gc_node = gc_node;
	sub->begin_time=ssd->current_time;

	if (operation != ERASE) {
		sub->lpn = ssd->channel_head[location->channel]->lun_head[location->lun]->plane_head[location->plane]->blk_head[location->block]->page_head[location->page]->lpn;
		if (ssd->dram->map->map_entry[sub->lpn].state){
			sub->state = 1;
			if (sub->lpn == -1) 
				cout << "Error in state of physical page " << endl; 
		}else{
			sub->state = 0;
			if (sub->lpn != -1) 
				cout << "Error in state of invalid physical page " << endl; 
		}
	}

	if (operation == READ)
	{
		if (sub->location != NULL) delete sub->location;
		sub->location = new local(location->channel, location->lun, location->plane,location->block, location->page);
		sub->ppn = ssd->dram->map->map_entry[sub->lpn].pn;
	}
	else if(operation == WRITE)
	{
		if (sub->location != NULL) delete sub->location;
	}
	else if (operation == ERASE)
	{
		if (sub->location != NULL) delete sub->location;
		sub->location = new local(location->channel, location->lun, location->plane, location->block, 0);
		sub->ppn = -1;
	}else {
		delete sub;
		printf("\nERROR ! Unexpected command.\n");
		return NULL;
	}

	return sub;
}
STATE move_page(ssd_info * ssd,  const local * location, gc_operation * gc_node){

	sub_request * rsub = create_gc_sub_request(ssd, location, READ, gc_node);
	ssd->channel_head[rsub->location->channel]->lun_head[rsub->location->lun]->GCSubs.push_tail(rsub);


	sub_request * wsub = create_gc_sub_request(ssd, location, WRITE, gc_node);
	wsub->location = new local(location->channel, location->lun, location->plane);
	invalid_old_page(ssd, wsub);
	wsub->ppn = get_new_ppn(ssd, wsub->lpn, wsub->location, false); // false---> cold block 
	if( wsub->ppn == -1 || write_page(ssd, wsub->lpn, wsub->ppn) == FAIL) {
		cout << "error in move page " << endl; 
		return FAIL; 
	}
		
	find_location(ssd, wsub->ppn, wsub->location);	

	buffer_entry * buf_ent = ssd->dram->map->map_entry[wsub->lpn].buf_ent; 
	if (buf_ent == NULL){
		ssd->channel_head[wsub->location->channel]->lun_head[wsub->location->lun]->GCSubs.push_tail(wsub);
	} else {
		if (!buf_ent->gc) 
			ssd->dram->buffer->hit_write(buf_ent); 
		else 
			ssd->dram->gc_buffer->hit_write(buf_ent); 	
		delete wsub; 
	}	

	return SUCCESS;
}

int erase_block(ssd_info * ssd,const local * location){

	
	unsigned int channel = location->channel;
	unsigned int lun = location->lun;
	unsigned int plane = location->plane;
	unsigned int block = location->block;

//	int initial_free = ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->blk_head[block]->free_page_num;
	ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->blk_head[block]->free_page_num=ssd->parameter->page_block;
	ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->blk_head[block]->invalid_page_num=0;
	ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->blk_head[block]->last_write_page=-1;
	ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->blk_head[block]->erase_count++;

	for (int i=0;i<ssd->parameter->page_block;i++)
	{
		ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->blk_head[block]->page_head[i]->valid_state=false;
		ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->blk_head[block]->page_head[i]->lpn=-1;
	}

	ssd->stats->flash_erase_count++;
	ssd->channel_head[channel]->erase_count++;
	ssd->channel_head[channel]->lun_head[lun]->erase_count++;
	ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->erase_count++;
	ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->free_page += ssd->parameter->page_block;
	ssd->channel_head[channel]->lun_head[lun]->plane_head[plane]->invalid_page -= ssd->parameter->page_block;

//	cout << "erase block " << channel << " "<< lun << " "<< plane << " "<< block << endl;
	return SUCCESS;

}


bool Schedule_GC(ssd_info * ssd, sub_request * sub){
		
	local * location = sub->location; 
	unsigned int free_page = ssd->channel_head[location->channel]->lun_head[location->lun]->plane_head[location->plane]->free_page;
	unsigned int invalid_page = ssd->channel_head[location->channel]->lun_head[location->lun]->plane_head[location->plane]->invalid_page;
	unsigned int all_page = ssd->parameter->page_block*ssd->parameter->block_plane;
	gc_operation * gc_node = NULL;



	if (free_page > (all_page * ssd->parameter->gc_down_threshold)){
		return false;
	}
	gc_node = new gc_operation(location, ssd->gc_sequence_number++);

	if (add_gc_node(ssd, gc_node) != SUCCESS){
		delete gc_node;
		return false;
	}

	sub->trigger_gc = true; 	
	
	//int invalid_00 = ssd->channel_head[0]->lun_head[0]->plane_head[location->plane]->invalid_page;
	//int invalid_01 = ssd->channel_head[0]->lun_head[1]->plane_head[location->plane]->invalid_page;
	//int invalid_10 = ssd->channel_head[1]->lun_head[0]->plane_head[location->plane]->invalid_page;
	//int invalid_11 = ssd->channel_head[1]->lun_head[1]->plane_head[location->plane]->invalid_page;
	//cout << "GC starts for planes " << location->channel << location->lun << " with (" << invalid_00 << "," << invalid_01 << "," << invalid_10 << "," << invalid_11 << ")\n";
	gc_for_plane(ssd, gc_node);

	return true;
}

void pre_process_gc(ssd_info * ssd, const local * location){

	int pre_process_move = 0;
	local * gc_location = new local(location->channel, location->lun, location->plane);
	if (find_victim_block(ssd, gc_location) != SUCCESS) {
		printf("Error: invalid block selected for gc \n");
		delete gc_location;
		return;
	}
	//location->print();
	for(unsigned int i=0;i<ssd->parameter->page_block;i++)
	{
		if(ssd->channel_head[gc_location->channel]->lun_head[gc_location->lun]->plane_head[gc_location->plane]->blk_head[gc_location->block]->page_head[i]->valid_state == true)
		{
			gc_location->page=i;

			int lpn = ssd->channel_head[gc_location->channel]->lun_head[gc_location->lun]->plane_head[gc_location->plane]->blk_head[gc_location->block]->page_head[gc_location->page]->lpn;
			local * location = new local(0,0,0,0,0); 
			invalid_old_page(ssd, lpn, location);
			int ppn = get_new_ppn(ssd, lpn, location, false); // false indicate we want cold active block 
			if (ppn ==-1 || write_page(ssd, lpn, ppn) == FAIL) {
				cout << "error in pre-process " << endl; 
				return; 
			}
				
			pre_process_move++;
		}
	}
	erase_block(ssd, gc_location);
	delete gc_location;
	ssd->stats->gc_moved_page += pre_process_move;
}

STATE add_gc_node(ssd_info * ssd, gc_operation * gc_node){
	if (gc_node == NULL) return FAIL;
	gc_operation * gc = ssd->channel_head[gc_node->location->channel]->lun_head[gc_node->location->lun]->plane_head[gc_node->location->plane]->scheduled_gc;

	if (gc == NULL){
		ssd->channel_head[gc_node->location->channel]->lun_head[gc_node->location->lun]->plane_head[gc_node->location->plane]->scheduled_gc = gc_node;
	}else {
		gc->next_node = gc_node; 
	}
	return SUCCESS;
}
STATE delete_gc_node(ssd_info * ssd, gc_operation * gc_node){
	if (gc_node == NULL) return FAIL;
	if (ssd->channel_head[gc_node->location->channel]->lun_head[gc_node->location->lun]->plane_head[gc_node->location->plane]->scheduled_gc == NULL) return FAIL;
	ssd->channel_head[gc_node->location->channel]->lun_head[gc_node->location->lun]->plane_head[gc_node->location->plane]->scheduled_gc = gc_node->next_node;
	delete gc_node;
	return SUCCESS;
}
// =============== GC ALGORITHMS ================================
unsigned int best_cost(ssd_info * ssd, plane_info * the_plane, int active_block, int cold_active_block){
	int max_invalid = -1; // ssd->parameter->page_block; 
	int selected_block = -1; 
	for (int i = 0; i < ssd->parameter->block_plane; i++){
		if ((i == active_block) || (i == cold_active_block)) continue; 
		int free_count = the_plane->blk_head[i]->free_page_num; 
		int invalid_count = the_plane->blk_head[i]->invalid_page_num; 
		
		if (free_count > 0) continue; 
		if (invalid_count > max_invalid) {
			max_invalid = invalid_count; 
			selected_block = i; 
		} 
	}	
	return selected_block; 
}
STATE greedy_algorithm(ssd_info * ssd,  local * location){
	int active_block = get_active_block(ssd,location);
	int cold_active_block = get_cold_active_block(ssd, location); 
	location->block = best_cost(ssd, ssd->channel_head[location->channel]->lun_head[location->lun]->plane_head[location->plane], active_block, cold_active_block);
	return SUCCESS;
}
STATE fifo_algorithm(ssd_info * ssd,  local * location){
	int block = -1;
	int64_t min_time = 10000000000000;
	plane_info * the_plane = ssd->channel_head[location->channel]->lun_head[location->lun]->plane_head[location->plane]; 
	int active_block = get_active_block(ssd,location);
	int cold_active_block = get_cold_active_block(ssd, location); 
	for (int i = 0; i < ssd->parameter->block_plane; i++){
		if ((i == active_block) || (i == cold_active_block)) continue;
		if(the_plane->blk_head[i]->free_page_num > 0) continue; 
		if(the_plane->blk_head[i]->invalid_page_num == 0) continue; 

		if (the_plane->blk_head[i]->last_write_time <= min_time){
			min_time = the_plane->blk_head[i]->last_write_time;
			block = i;
		}
	}
	location->block = block;
	return SUCCESS;
}
STATE windowed_algorithm(ssd_info * ssd, local * location){
/*
	unsigned int window_size = 100; // FIXME make it parameter

	int * blocks = new int[window_size];
	for (int i = 0; i < window_size; i++)
		blocks[i] = -1;


	plane_info *the_plane = ssd->channel_head[location->channel]->lun_head[location->lun]->plane_head[location->plane];

	int active_block = get_active_block(ssd,location);
	int cold_active_block = get_cold_active_block(ssd, location); 
	for (int i = 0; i < ssd->parameter->block_plane; i++){
		if ((i == active_block) || (i == cold_active_block)) continue;
		if(the_plane->blk_head[i]->free_page_num > 0) continue; 
		
		int64_t time_i = the_plane->blk_head[i]->last_write_time;
		for (int j = 0; j < window_size; j++){
			if (blocks[j] == -1) {blocks[j] = i; break;}
			
			int64_t time_j = the_plane->blk_head[blocks[j]]->last_write_time;

			if (time_j > time_i){

				for (int k = window_size - 2; k >= j; k--){
					blocks[k+1] = blocks[k];
				}
				blocks[j] = i;
				break;
			}

		}
	}

	int temp = window_size - 1;
	while (blocks[temp] == -1) temp--;

	unsigned int block = -1;

	block= best_cost(ssd, the_plane, active_block, cold_active_block);
	delete blocks;
*/
	location->block = 1; // block;

	return SUCCESS;
}
STATE RGA_algorithm(ssd_info * ssd, local * location){
	unsigned int window_size = 200; // FIXME
	unsigned int total_size = ssd->parameter->block_plane;

	int * blocks = new int[total_size];
	for (int i = 0; i < total_size; i++){
		blocks[i] = i;
	}

	int active_block = get_active_block(ssd, location);
	int cold_active_block = get_cold_active_block(ssd, location); 
	plane_info * p = ssd->channel_head[location->channel]->lun_head[location->lun]->plane_head[location->plane];

	blocks[active_block] = blocks[total_size - 1];
	total_size--;
	blocks[cold_active_block] = blocks[total_size -1]; 
	total_size--; 

	for (int i = 0; i < window_size; i++){
		int j = rand() % (total_size - i);
		while ((p->blk_head[j]->free_page_num >0) || (j ==active_block) || (j == cold_active_block)) {
			j = rand() % (total_size - i); 
		}
		if (j != 0){
			int temp = blocks[i];
			blocks[i] = blocks[i+j];
			blocks[i + j] = temp;
		}
	}

	int block = -1;
	int max_invalid = 0; 
	for (int i = 0; i < window_size; i++){
		int temp = blocks[i]; 
		if (temp == active_block || temp == cold_active_block) continue; 
		if (p->blk_head[temp]->free_page_num > 0) continue; 
		if (p->blk_head[temp]->invalid_page_num > max_invalid) {
			block = temp; 
			max_invalid = p->blk_head[temp]->invalid_page_num; 
		}
		
	}
	if (block == -1) 
			greedy_algorithm(ssd, location); 
	
	location->block = block;
	delete blocks;

	return SUCCESS;
}
STATE RANDOM_algorithm(ssd_info * ssd, local * location){
	unsigned int block = -1;
	int active_block = get_active_block(ssd,location);
	int cold_active_block = get_cold_active_block(ssd, location); 
	unsigned int rand_block = rand() % ssd->parameter->block_plane;
	plane_info * the_plane = ssd->channel_head[location->channel]->lun_head[location->lun]->plane_head[location->plane]; 
	while ((rand_block == active_block) || (rand_block == cold_active_block) || (the_plane->blk_head[rand_block]->free_page_num > 0))
		rand_block = rand() % ssd->parameter->block_plane;

	location->block = rand_block;
	return SUCCESS;
}
STATE RANDOM_p_algorithm(ssd_info * ssd, local * location){
	unsigned int block = -1;
	int active_block = get_active_block(ssd,location);
	int cold_active_block = get_cold_active_block(ssd, location); 

	unsigned int rand_block = rand() % ssd->parameter->block_plane;
	while ((rand_block == active_block) || (rand_block == cold_active_block) || ssd->channel_head[location->channel]->lun_head[location->lun]->plane_head[location->plane]->blk_head[rand_block]->invalid_page_num == 0)
		rand_block = rand() % ssd->parameter->block_plane;

	location->block = rand_block;

	return SUCCESS;
}
STATE RANDOM_pp_algorithm(ssd_info * ssd, local * location){
	unsigned int least_invalid = ssd->parameter->overprovide * ssd->parameter->block_plane;

	unsigned int block = -1;
	int active_block = get_active_block(ssd,location);
	int cold_active_block = get_cold_active_block(ssd, location); 
	unsigned int rand_block = rand() % ssd->parameter->block_plane;
	plane_info * the_plane = ssd->channel_head[location->channel]->lun_head[location->lun]->plane_head[location->plane]; 
	int counter = 0;
	while ((rand_block == active_block) || (rand_block == cold_active_block )||(the_plane->blk_head[rand_block]->invalid_page_num < least_invalid && counter <= ssd->parameter->block_plane)
				|| (the_plane->blk_head[rand_block]->free_page_num > 0)){
		rand_block = rand() % ssd->parameter->block_plane;
		counter++;
	}

	location->block = rand_block;
	return SUCCESS;
}
