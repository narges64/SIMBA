#include "ssd.hh"
using namespace std;

int  main(int argc, char * argv[]){

	if (argc < 3) {
		printf("Not enough parameters: specify parameter file and statistic file! \n");
		return 0;
	}
	parameter_value * parameters = new parameter_value(argc, argv);
	ssd_info * ssd = new ssd_info(parameters, argv[2]); 

	ssd->tracefile = fopen(ssd->parameter->trace_filename, "r"); 
	if (ssd->tracefile == NULL){
		generate_trace_file(ssd, ssd->parameter->trace_filename); 
		ssd->tracefile = fopen(ssd->parameter->trace_filename, "r"); 
	}
	if (ssd->tracefile == NULL) {
		cout << "still trace file is null " << endl; 
		return 1; 
	}

	fseek(ssd->tracefile, 0 ,SEEK_SET);

	cerr << "start pre-conditioning " << endl;
	full_write_preconditioning(ssd, true);   // sequential 
	full_write_preconditioning(ssd, false);  // random 

	ssd->stats->print_all(); // does nothing now 
	ssd->stats->reset_all();
	
	ssd->reset_ssd_stats(); // reset read, write, erase number to zero 

	simulate(ssd);

	collect_gc_statistics(ssd, 0);
	print_epoch_statistics(ssd, 0);
	print_statistics(ssd, 0);

	close_files(ssd);
				
	delete ssd; 

	printf("\nthe simulation is completed!\n");

	return 1;
}
ssd_info *simulate(ssd_info *ssd){

	int flag=0;
	unsigned int a=0,b=0;
	static int second = 0;

	printf("\n");
	printf("begin simulating.......................\n");
	printf("\n");

	int i = 0;
	while(flag!=100)
	{
		flag=get_requests_consolidation(ssd);
		if(flag == 1)
		{
			distribute(ssd); // in ssd.cpp
		}
		process(ssd); // in ftl.cpp
		trace_output(ssd);
		if(flag == 0 && ssd->request_queue == NULL){
			flag = 100;
		}
		if (ssd->current_time != MAX_INT64 && ssd->current_time / EPOCH_LENGTH  > second){
			print_epoch_statistics(ssd, 1);
			while (ssd->current_time / (EPOCH_LENGTH) > second)
				second++;
		}

//		if (!ssd->dram->buffer->check_buffer()) {
//			cout << "BUFFER FAIL " << endl;
//			break;
//		}
	}

	return ssd;
}

int add_fetched_request(ssd_info * ssd, request * request1, uint64_t nearest_event_time) {
	if (request1 == NULL){	// no request before nearest_event_time
		if (ssd->current_time == MAX_INT64)
			return 0;  
		if (ssd->current_time <= nearest_event_time){
			if ((ssd->request_queue_length >= ssd->parameter->queue_length) &&
			 	(nearest_event_time == MAX_INT64)){
			// 	printf("2. HERE %d \n", ssd->request_queue_length);
			}
			else {
				ssd->current_time = nearest_event_time;
			}
		}
		if (nearest_event_time == MAX_INT64) return 0; 
		return -1;
	}

	if (request1->time == MAX_INT64){ // nothing left to read
		delete request1;
		if (ssd->current_time < nearest_event_time)
			ssd->current_time = nearest_event_time;
		return 0;
	}

	if (ssd->current_time <= request1->time) {
		ssd->current_time=request1->time ;
		if (ssd->current_time == MAX_INT64)
			printf("3. HERE \n");
	}
	add_to_request_queue(ssd, request1);
	if (request1->io_num % 10000 == 0){
		ssd->stats->total_flash_erase_count += ssd->stats->flash_erase_count;
		ssd->stats->flash_erase_count = 0;
		cout << ssd->current_time << " fetching io request number: " << request1->io_num ;
		cout << "\terase: " << ssd->stats->total_flash_erase_count ;
		cout << "\tq length: " << ssd->request_queue_length  << "  " << ssd->parameter->queue_length;
		cout << "\tmove count: " << ssd->stats->gc_moved_page;
		int64_t gc_time = ssd->stats->gc_moved_page * (ssd->parameter->time_characteristics.tR/100000 + ssd->parameter->time_characteristics.tPROG/100000) + ssd->stats->total_flash_erase_count * ssd->parameter->time_characteristics.tBERS/100000; 
		int64_t total_time = ssd->current_time; 
		cout << "\tgc contribution: " << (gc_time / 16.0 )/ (ssd->current_time/100000) << endl;  
	}
	return 1;
}

double norm_dist(double mu, double sigma ) {
	const double epsilon = std::numeric_limits<double>::min(); 
	const double two_pi = 2.0 * 3.14159265358979323846; 
	
	static double z0, z1 = 0; 
	static bool generate = false; 
	generate = !generate; 
	
	if (!generate) 
		return z1 * sigma + mu; 

	double u1, u2; 
	do 
	{
		u1 = rand() * (1.0 / RAND_MAX); 
		u2 = rand() * (1.0 / RAND_MAX); 

	}while (u1 <= epsilon); 

	z0 = sqrt(-2.0 * log(u1)) * cos(two_pi * u2); 
	z1 = sqrt(-2.0 * log(u1)) * sin(two_pi * u2); 
	
	return z0 * sigma + mu; 

}

double expo_dist(double mu){

	double u1 = rand() * (1.0 / RAND_MAX); 
		
	double time = (-1 * log(u1) / 0.43429) * mu; 
	return time;  

}

request * generate_next_request(ssd_info * ssd, int64_t nearest_event_time){
	if (ssd->request_queue_length >= ssd->parameter->queue_length){
		return NULL;
	}

	static uint64_t previous_time = 0;
	double rd_ratio = ssd->parameter->syn_rd_ratio;
	int lun_number = ssd->parameter->lun_num;
	uint64_t min_sector_address = 0;
	uint64_t max_sector_address =(long int)(((long int)ssd->parameter->subpage_page*ssd->parameter->page_block*ssd->parameter->block_plane*ssd->parameter->plane_lun*ssd->parameter->lun_num)*(1-ssd->parameter->overprovide));

	uint64_t size = ssd->parameter->syn_req_size;
	uint64_t avg_time = ssd->parameter->syn_interarrival_mean; 
	int event_count = ssd->parameter->syn_req_count;

	// Time
 	uint64_t new_time = 0;
 	uint64_t time_interval = expo_dist(avg_time);//, 1);
 	new_time = previous_time  + time_interval;
	if (new_time < ssd->current_time) new_time = ssd->current_time;

	if (new_time > nearest_event_time)
		return NULL;

 	previous_time = new_time;
	request * request1 = new request();
	request1->app_id = 1;
	request1->io_num = ssd->request_sequence_number;
	request1->time = new_time;

 	// Operation
 	int r = rand() % 100;
 	if (r < (rd_ratio*100)) {
		request1->operation = 1; // Read
 	}else {
		request1->operation = 0; // Write
 	}

 	// Address
	int align = ssd->parameter->subpage_page; 
 	uint64_t address = (rand() % (max_sector_address - min_sector_address)) + min_sector_address ;
	request1->lsn = address;

 	// Size
 	int req_size = size;
	request1->size = size;

	if (request1->io_num > event_count) {
		request1->time = MAX_INT64;
	}else {
		ssd->request_sequence_number++;
	}
	return request1;

}


void generate_trace_file(ssd_info * ssd, char * trace_filename){
	ssd->tracefile = fopen(trace_filename, "w"); 
	if (ssd->tracefile == NULL) {
		cout << "file is not ready " << endl; 
		return; 
	} 
	int64_t nearest_event_time = MAX_INT64; 
	struct request * request1; 
	do {
		request1 = generate_next_request(ssd, nearest_event_time);
		if (request1 != NULL && request1->time != MAX_INT64)
			request1->print_to_file(ssd->tracefile); 
	}while(request1 != NULL && request1->time != MAX_INT64);
	
	fclose(ssd->tracefile); 		

}

int get_requests_consolidation(ssd_info *ssd)  {
	int64_t  nearest_event_time=find_nearest_event(ssd);
	
	struct request *request1;
	
	// Trace-based 
	if (true) { 

		// read request before nearest_event_time, if any
		request1 = read_request_from_file(ssd, nearest_event_time);

	}
	// Synthetic  
	else {
		request1 = generate_next_request(ssd, nearest_event_time);
	}

	// Add request to the queue 
	int ret = add_fetched_request(ssd, request1, nearest_event_time);
	return ret;
}
request * read_request_from_file(ssd_info * ssd, int64_t nearest_event_time){

	static int64_t last_request_time = 0; 

	unsigned int total_size = ssd->parameter->lun_num * ssd->parameter->plane_lun *
						ssd->parameter->block_plane * ssd->parameter->page_block;
	total_size = total_size * (1-ssd->parameter->overprovide);

	if (ssd->request_queue_length >= ssd->parameter->queue_length){
		return NULL;
	}	

	static int io_num = 0;
	int64_t time_tt = MAX_INT64;
	int unused;
	char * type = new char[5];
	int64_t lsn=0;
	long int size = 0; 
	int app_id = 0;
	
	long filepoint;
	filepoint= ftell(ssd->tracefile);
	
	char buffer[200];
	fgets(buffer, 200, ssd->tracefile);
	// sscanf(buffer,"%d %lld %d %d %s %d %ld %d",&io_num,&time_tt,&unused,&unused,type,&lsn,&size,&app_id);
	sscanf(buffer,"%lld %s %d %ld ",&time_tt,type,&lsn,&size);
	
	if (time_tt != MAX_INT64){
		if (time_tt > MAX_INT64 || time_tt < 0)
			time_tt = MAX_INT64;
	}
	

	if ((lsn < 0) || (size <= 0))
	{
		ssd->last_times = last_request_time; 
		if (ssd->repeat_times < ssd->parameter->repeat_trace){	 
			ssd->repeat_times++; 
			fseek(ssd->tracefile,0, SEEK_SET);
		}
		
		return NULL;
	}


	// if next request is not before the next event, we discard it 	
	if (time_tt > nearest_event_time ){
		fseek(ssd->tracefile,filepoint,0);
		return NULL;
	}

	int ope; 
	if (strcasecmp(type, "Read") == 0)
		ope = 1;
	else
		ope = 0;

	request * request1 = new request();

	request1->app_id = app_id;
	request1->time = ssd->last_times + time_tt;
	request1->lsn = (int64_t)((lsn % total_size) / ssd->parameter->subpage_page ) * ssd->parameter->subpage_page;
	request1->size = size;
	request1->io_num = io_num++;
	request1->operation = ope;
	request1->begin_time = ssd->current_time; 

	last_request_time = request1->time; 

	return request1;

}

void add_to_request_queue(ssd_info * ssd, request * req){
	req->begin_time = ssd->current_time;
	if (ssd->request_queue == NULL)          // The queue is empty
	{
		ssd->request_queue = req;
		ssd->request_tail = req;
		ssd->request_queue_length++;
	}
	else
	{
		ssd->request_tail->next_node = req;
		ssd->request_tail = req;
		ssd->request_queue_length++;
	}


}
void collect_statistics(ssd_info * ssd, request * req){

	sub_request * sub;
	sub = req->subs; //  critical_sub;

	while (sub != NULL)
	{
		if (sub->complete_time != req->response_time) { sub = sub->next_subs; continue;  }
	for (unsigned int i = 0; i < SR_MODE_NUM; i++){
			ssd->stats->subreq_state_time[i] += sub->state_time[i];
		}

		sub = sub->next_subs;

	}

	if(req->response_time-req->begin_time==0)
	{
		printf("1. the response time is 0?? %lld %lld \n", req->response_time, req->begin_time);
		getchar();
	}
	
	if (req->operation==READ)
	{
		ssd->stats->read_request_size[req->app_id] += req->size;
		ssd->stats->read_RT.update(req->response_time - req->begin_time); 
		ssd->stats->read_throughput.add_time(req->begin_time, req->response_time); // changed 
		ssd->stats->read_throughput.add_capacity(req->size);
		ssd->stats->read_throughput.add_count(1);
	}
	else
	{
		ssd->stats->write_request_size[req->app_id] += req->size; 
		ssd->stats->write_RT.update(req->response_time - req->begin_time); 
		ssd->stats->write_throughput.add_time(req->begin_time, req->response_time); 
		ssd->stats->write_throughput.add_capacity(req->size);
		ssd->stats->write_throughput.add_count(1);
	}

}
void print_epoch_statistics(ssd_info * ssd, int app_id){
	int epoch_num = ssd->current_time / EPOCH_LENGTH;
	fprintf(ssd->statisticfile, "epoch %d , current_time %lld \n", epoch_num, ssd->current_time);
	// =================== LATENCY ================================================================
	ssd->stats->total_flash_erase_count += ssd->stats->flash_erase_count;

	fprintf(ssd->statisticfile, "Latency epoch: %d \n", epoch_num);
	fprintf(ssd->statisticfile, "read RT avg %lld us , var %lld ms2, count %lld , max %lld us\n", ssd->stats->read_RT.get_average() , ssd->stats->read_RT.get_variance(), ssd->stats->read_RT.get_count(), ssd->stats->read_RT.get_max());
	fprintf(ssd->statisticfile, "write RT avg %lld us , var %lld ms2, count %lld , max %lld us \n", ssd->stats->write_RT.get_average(),ssd->stats->write_RT.get_variance(), ssd->stats->write_RT.get_count(), ssd->stats->write_RT.get_max());
	fprintf(ssd->statisticfile, "erase(gc) count %lld , total %lld\n", ssd->stats->flash_erase_count, ssd->stats->total_flash_erase_count);
	fprintf(ssd->statisticfile, "GC move count %d \n" , ssd->stats->gc_moved_page);
	ssd->stats->flash_erase_count = 0;

	// =================== SUBREQ STATES ===========================================================
	fprintf(ssd->statisticfile, "SUBREQ ep: %d time:\t", epoch_num);
	for (int i = 0; i < SR_MODE_NUM; i++){
		fprintf(ssd->statisticfile, "%lld\t",ssd->stats->subreq_state_time[i]);
		ssd->stats->subreq_state_time[i] = 0;
	}
	fprintf(ssd->statisticfile, "\n");

	// =================== LUN STATES ============================================================
	for (int i = 0; i < ssd->parameter->lun_num; i++){
		int channel_num = i % ssd->parameter->channel_number;
		int lun_num = (i / ssd->parameter->channel_number) % ssd->channel_head[channel_num]->lun_num;
		lun_info * the_lun = ssd->channel_head[channel_num]->lun_head[lun_num];
		fprintf(ssd->statisticfile, "lun ep: %d (%d,%d): ",epoch_num , channel_num, lun_num);
		for (int i = 0; i < LUN_MODE_NUM; i++){
			fprintf(ssd->statisticfile, "%lld\t", the_lun->state_time[i]);
			the_lun->state_time[i] = 0;
		}
		fprintf(ssd->statisticfile, "\n");
		for (int j = 0; j < ssd->parameter->plane_lun; j++){
			int plane_num = j;
			plane_info * the_plane = ssd->channel_head[channel_num]->lun_head[lun_num]->plane_head[plane_num];
			fprintf(ssd->statisticfile, "plane ep: %d (%d,%d,%d) ", epoch_num, channel_num, lun_num, plane_num);
			for (int i = 0; i < PLANE_MODE_NUM; i++){
				fprintf(ssd->statisticfile, "%lld\t",the_plane->state_time[i]);
				the_plane->state_time[i] = 0;
			}
			fprintf(ssd->statisticfile, "\n");
		}

	}
	// ==================== OTHER STATS ===================================
	fprintf(ssd->statisticfile, "Multiplane ep: %d read %lld , write %lld , erase %lld \n", epoch_num, ssd->stats->read_multiplane_count , ssd->stats->write_multiplane_count , ssd->stats->erase_multiplane_count);
}
void print_statistics(ssd_info *ssd, int app){

	fprintf(ssd->statisticfile, "======== Application %d , time %lld ============ \n", app, ssd->total_execution_time);

	fprintf(ssd->statisticfile, "request read RT avg %lld us , var %lld ms2 , count %lld , max %lld us\n", 
									ssd->stats->read_RT.get_average(), ssd->stats->read_RT.get_variance(), 
									ssd->stats->read_RT.get_count() , ssd->stats->read_RT.get_max());

	fprintf(ssd->statisticfile, "request write RT avg %lld us , var %lld ms2, count %lld , max %lld us \n", 
									ssd->stats->write_RT.get_average(), ssd->stats->write_RT.get_variance(), 
									ssd->stats->write_RT.get_count() , ssd->stats->write_RT.get_max());
	
	fprintf(ssd->statisticfile, "read IOPS and BW [%d]: %f , %f  \n", app, ssd->stats->read_throughput.get_IOPS() , ssd->stats->read_throughput.get_BW());
	fprintf(ssd->statisticfile, "write IOPS and BW [%d]: %f , %f  \n", app, ssd->stats->write_throughput.get_IOPS() , ssd->stats->write_throughput.get_BW());

	fprintf(ssd->statisticfile,"erase: ( %lld )\n", ssd->stats->total_flash_erase_count);
	fprintf(ssd->statisticfile,"total gc move count: %d \n", ssd->stats->gc_moved_page);
	fprintf(ssd->statisticfile, "buffer read hit %d , write hit %d , gc buffer read hit %d , write hit %d \n ", 
								ssd->dram->buffer->read_hit , ssd->dram->buffer->write_hit , 
								ssd->dram->gc_buffer->read_hit , ssd->dram->gc_buffer->write_hit); 

	fprintf(ssd->statisticfile, "flash program count %d , WAF %f\n" , ssd->stats->flash_prog_count , (ssd->stats->flash_prog_count)/(double)(ssd->stats->flash_prog_count - ssd->stats->gc_moved_page));  

	fprintf(ssd->statisticfile, "========================\n");

	ssd->stats->flash_erase_count = 0;

	// Print lun statistic output
	int chan,lun = 0;
	/*


	fprintf(ssd->statisticfile, "\nlun IOPS \n");
	for (chan = 0; chan < ssd->parameter->channel_number; chan++){
		for (lun = 0; lun < ssd->channel_head[chan]->lun_num; lun++){
			fprintf(ssd->statisticfile, "%d,", (int)ssd->channel_head[chan]->lun_head[lun]->stat_read_throughput.get_IOPS());
			fprintf(ssd->statisticfile, "%d,", (int)ssd->channel_head[chan]->lun_head[lun]->stat_write_throughput.get_IOPS());
			fprintf(ssd->statisticfile, "%d\t", (int)ssd->channel_head[chan]->lun_head[lun]->stat_rw_throughput.get_IOPS());
		}
		fprintf(ssd->statisticfile, "\n");
	}

	fprintf(ssd->statisticfile, "\nlun BW  (MB/s) \n");
	for (chan = 0; chan < ssd->parameter->channel_number; chan++){
		for (lun = 0; lun < ssd->channel_head[chan]->lun_num; lun++){
			fprintf(ssd->statisticfile, "%d,", (int)ssd->channel_head[chan]->lun_head[lun]->stat_read_throughput.get_BW());
			fprintf(ssd->statisticfile, "%d,", (int)ssd->channel_head[chan]->lun_head[lun]->stat_write_throughput.get_BW());
			fprintf(ssd->statisticfile, "%d\t", (int)ssd->channel_head[chan]->lun_head[lun]->stat_rw_throughput.get_BW());
		}
		fprintf(ssd->statisticfile, "\n");
	}

	fprintf(ssd->statisticfile, "\nlun noop IOPS \n");
	for (chan = 0; chan < ssd->parameter->channel_number; chan++){
		for (lun = 0; lun < ssd->channel_head[chan]->lun_num; lun++){
			fprintf(ssd->statisticfile, "%d,", (int)ssd->channel_head[chan]->lun_head[lun]->stat_read_throughput.get_noop_IOPS());
			fprintf(ssd->statisticfile, "%d,", (int)ssd->channel_head[chan]->lun_head[lun]->stat_write_throughput.get_noop_IOPS());
			fprintf(ssd->statisticfile, "%d\t", (int)ssd->channel_head[chan]->lun_head[lun]->stat_rw_throughput.get_noop_IOPS());
		}
		fprintf(ssd->statisticfile, "\n");
	}


	fprintf(ssd->statisticfile, "\nlun noop BW (MB/s) \n");
	for (chan = 0; chan < ssd->parameter->channel_number; chan++){
		for (lun = 0; lun < ssd->channel_head[chan]->lun_num; lun++){
			fprintf(ssd->statisticfile, "%d,", (int)ssd->channel_head[chan]->lun_head[lun]->stat_read_throughput.get_noop_BW());
			fprintf(ssd->statisticfile, "%d,", (int)ssd->channel_head[chan]->lun_head[lun]->stat_write_throughput.get_noop_BW());
			fprintf(ssd->statisticfile, "%d\t", (int)ssd->channel_head[chan]->lun_head[lun]->stat_rw_throughput.get_noop_BW());
		}
		fprintf(ssd->statisticfile, "\n");
	}
	*/

	fprintf(ssd->statisticfile, "\nlun erase count \n");
	for (chan = 0; chan < ssd->parameter->channel_number; chan++){
		for (lun = 0; lun < ssd->channel_head[chan]->lun_num; lun++){
			fprintf(ssd->statisticfile, "%d\t", ssd->channel_head[chan]->lun_head[lun]->erase_count);
		}
		fprintf(ssd->statisticfile, "\n");
	}

	fprintf(ssd->statisticfile, "\nplanes erase number: \n");
	for(int i=0;i<ssd->parameter->channel_number;i++){
		for (int j = 0; j < ssd->parameter->lun_channel[i]; j++){
			for(int l=0;l<ssd->parameter->plane_lun;l++)
			{
				int plane_erase = ssd->channel_head[i]->lun_head[j]->plane_head[l]->erase_count;
				fprintf(ssd->statisticfile,"(%d,%d)",l, plane_erase);
			}
			fprintf(ssd->statisticfile, "\t");
		}
		fprintf(ssd->statisticfile, "\n");
	}

}

void remove_request(ssd_info * ssd, request ** req, request ** pre_node){
	if(*pre_node == NULL)
	{
		if((*req)->next_node == NULL)
		{
			delete (*req);
			*req = NULL;
			ssd->request_queue = NULL;
			ssd->request_tail = NULL;
			ssd->request_queue_length--;
		}
		else
		{
			ssd->request_queue = (*req)->next_node;
			(*pre_node) = (*req);
			(*req) = (*req)->next_node;
			delete (*pre_node);
			*pre_node = NULL;
			ssd->request_queue_length--;
		}
	}
	else
	{
		if((*req)->next_node == NULL)
		{
			(*pre_node)->next_node = NULL;
			delete (*req);
			*req = NULL;
			ssd->request_tail = (*pre_node);
			ssd->request_queue_length--;
		}
		else
		{
			(*pre_node)->next_node = (*req)->next_node;
			delete (*req);
			(*req) = (*pre_node)->next_node;
			ssd->request_queue_length--;
		}
	}

}

void trace_output(ssd_info* ssd){
	int flag = 1;
	int64_t start_time, end_time;
	request *req, *pre_node;
	sub_request *sub, *tmp;

	pre_node=NULL;
	req = ssd->request_queue;

	if(req == NULL)
		return;

	while(req != NULL)
	{
		if(req->response_time != 0)
		{

			collect_statistics(ssd, req);
			remove_request(ssd, &req, &pre_node);
		}
		else
		{
			sub = req->subs;
			flag = 1;
			start_time = 0;
			end_time = 0;

			while(sub != NULL)
			{
				if(start_time == 0)
					start_time = sub->begin_time;
				if(start_time > sub->begin_time)
					start_time = sub->begin_time;
				if (end_time < sub->complete_time){
					end_time = sub->complete_time;
					req->critical_sub = sub;
				}

				// if any sub-request is not completed, the request is not completed
				if( find_subrequest_state(ssd, sub) == SR_MODE_COMPLETE )
				{
					sub = sub->next_subs;
				}
				else
				{
					flag=0;
					break;
				}

			}

			if (flag == 1)
			{
				req->response_time = end_time;
				req->begin_time = start_time;
				collect_statistics(ssd, req );
				remove_request(ssd, &req, &pre_node);

			}
			else
			{
				// request is not complete yet, go to the next node
				pre_node = req;
				req = req->next_node;
			}
		}
	}

}
unsigned int transfer_size(ssd_info *ssd,unsigned int lpn,request *req){
	unsigned int first_lpn,last_lpn,trans_size;
	uint64_t state;
	uint64_t mask=0,offset1=0,offset2=0;

	first_lpn=req->lsn/ssd->parameter->subpage_page;
	last_lpn=(req->lsn+req->size-1)/ssd->parameter->subpage_page;

	mask=~(0xffffffffffffffff<<(ssd->parameter->subpage_page));
	state=mask;
	if(lpn==first_lpn)
	{
		offset1=ssd->parameter->subpage_page-((lpn+1)*ssd->parameter->subpage_page-req->lsn);
		state=state&(0xffffffffffffffff<<offset1);
	}
	if(lpn==last_lpn)
	{
		offset2=ssd->parameter->subpage_page-((lpn+1)*ssd->parameter->subpage_page-(req->lsn+req->size));
		state=state&(~(0xffffffffffffffff<<offset2));
	}

	trans_size=size(state);

	return trans_size;
}
int64_t find_nearest_event(ssd_info *ssd) {
	unsigned int i,j;
	int64_t time=MAX_INT64;
	int64_t time1=MAX_INT64;
	int64_t time2=MAX_INT64;



	for (i=0;i<ssd->parameter->channel_number;i++)
	{
		if (ssd->channel_head[i]->next_state==CHANNEL_MODE_IDLE)
			if(time1>ssd->channel_head[i]->next_state_predict_time)
				if (ssd->channel_head[i]->next_state_predict_time > ssd->current_time)
					time1=ssd->channel_head[i]->next_state_predict_time;
		for (j=0;j<ssd->parameter->lun_channel[i];j++)
		{
			if (ssd->channel_head[i]->lun_head[j]->next_state==LUN_MODE_IDLE)
				if(time2>ssd->channel_head[i]->lun_head[j]->next_state_predict_time)
					if (ssd->channel_head[i]->lun_head[j]->next_state_predict_time>ssd->current_time)
						time2=ssd->channel_head[i]->lun_head[j]->next_state_predict_time;

		}
	}


	time=(time1>time2)?time2:time1;


	return time;
}


void collect_gc_statistics(ssd_info * ssd, int app){
	// FIXME
}
void free_all_node(ssd_info *ssd){
		delete ssd;
}
void close_files(ssd_info * ssd) {

	fflush(ssd->statisticfile);
	fclose(ssd->statisticfile);
}
