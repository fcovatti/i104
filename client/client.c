#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>
#include <malloc.h>
#include <signal.h>
#include "client.h"
#include "comm.h"
#include "util.h"
#include <time.h>
#include <sys/socket.h>
#include "iec60870_master.h"
#include "iec60870_common.h"
#include "hal_time.h"
#include "hal_thread.h"

/***************************** Defines for code debugging********************************************/
//#define HANDLE_DIGITAL_DATA_DEBUG 1
//#define HANDLE_ANALOG_DATA_DEBUG 1
//#define HANDLE_EVENTS_DATA_DEBUG 1
//#define DEBUG_READ_DATASET 1
//#define DEBUG_DIGITAL_REPORTS 1
//#define DEBUG_EVENTS_REPORTS	1
#define LOG_CONFIG	1
//#define LOG_COUNTERS	1
//#define DATA_LOG 1
//#define WINE_TESTING 1
/*********************************************************************************************************/

static int running = 1; //used on handler for signal interruption

/***************************************Configuration**************************************************/
// Variables used alongside the code to control the total of each element
static int num_of_analog_ids = 0;	
static int num_of_digital_ids = 0;	
static int num_of_event_ids = 0;	
static int num_of_commands = 0;	
static int num_of_datasets = 0;
static int num_of_analog_datasets = 0;
static int num_of_digital_datasets = 0;
static int num_of_event_datasets = 0;

//Configuration of the 104 client
static data_config * analog_cfg = NULL;
static data_config * digital_cfg = NULL;
static data_config * events_cfg = NULL;
static command_config * commands = NULL;
static dataset_config * dataset_conf = NULL;

//Servers Main and Backup data
static st_server_data srv_main;
static st_server_data srv_bckp;

//Variables Configured on CLIENT_CONFIG_FILE file
static char srv1[MAX_STR_NAME], srv2[MAX_STR_NAME], srv3[MAX_STR_NAME], srv4[MAX_STR_NAME]; 
static char srv5[MAX_STR_NAME], srv6[MAX_STR_NAME], srv7[MAX_STR_NAME], srv8[MAX_STR_NAME]; 
static int integrity_time=0, analog_buf=0, digital_buf=0, events_buf=0;
static int convert_hyphen_to_dollar=0;

//Backup client variables
static char bkp_addr[MAX_STR_NAME];
static int bkp_socket;
static int bkp_enabled;
static int bkp_present;
static unsigned int bkp_signature = CLIENT_BACKUP_SIGNATURE;
static struct sockaddr_in bkp_sock_addr;

//STATS variables
static char stats_addr[MAX_STR_NAME];
static int stats_socket_send;
static int stats_socket_receive;
static int stats_enabled;
static unsigned int stats_signature = CLIENT_STATS_SIGNATURE;
static struct sockaddr_in stats_sock_addr;

//IHM useful varibless
static char ihm_addr[MAX_STR_NAME];
static int ihm_main_socket_send=0;
static int ihm_bkp_socket_send=0;
static int ihm_socket_receive=0;
static int ihm_enabled=0;
static int ihm_station=0;
static struct sockaddr_in ihm_main_sock_addr;
static struct sockaddr_in ihm_bkp_sock_addr;
static struct timeval start, curr_time;

//IHM Counters
static unsigned int num_of_report_msgs;
static unsigned int num_of_digital_msgs;
static unsigned int num_of_analog_msgs;

//IHM Analog buffer
static st_analog_queue analog_queue;
static st_digital_queue digital_queue;
static Semaphore digital_mutex;

// Localtime mutex for not crashing under win32 platforms
Semaphore localtime_mutex;

//Threads
static Thread connections_thread;
static Thread bkp_thread;
static Thread stats_thread;


/*********************************************************************************************************/
#ifdef DATA_LOG
static FILE * data_file_analog = NULL;
static FILE * data_file_digital = NULL;
static FILE * data_file_events = NULL;
#endif

//Log file
FILE * log_file = NULL;

/*********************************************************************************************************/
static int get_time_ms(){
	gettimeofday(&curr_time, NULL);
	return ((curr_time.tv_sec-start.tv_sec)*1000 + (curr_time.tv_usec-start.tv_usec)/1000);
}
/*********************************************************************************************************/
static int open_log_file(){
	/*****************
	 * OPEN LOG FILES
	 * **********/
	time_t t = time(NULL);
	struct tm now = *localtime(&t); 
	char flog[MAX_STR_NAME];

#ifdef WIN32
#ifdef WINE_TESTING
	snprintf(flog,MAX_STR_NAME, "/tmp/i104_info-%04d%02d%02d%02d%02d.log", now.tm_year+1900, now.tm_mon+1, now.tm_mday,now.tm_hour, now.tm_min);
#else
	snprintf(flog,MAX_STR_NAME, "..\\logs\\i104_info-%04d%02d%02d%02d%02d.log", now.tm_year+1900, now.tm_mon+1, now.tm_mday,now.tm_hour, now.tm_min);
#endif
#else
	snprintf(flog,MAX_STR_NAME, "/tmp/i104_info-%04d%02d%02d%02d%02d.log", now.tm_year+1900, now.tm_mon+1, now.tm_mday,now.tm_hour, now.tm_min);
#endif

	log_file = fopen(flog, "w");
	if(log_file==NULL){
		printf("Error, cannot open log file %s\n",flog);
		fclose(log_file);
		return -1;
	}
	return 0;
}
/*********************************************************************************************************/
static int read_configuration() {
	FILE * file = NULL;
	char line[300];
	int origin = 0;
	int event = 0;
	unsigned int nponto = 0;
	unsigned int nponto_monitored = 0;
	char id_ponto[MAX_STR_NAME] = "";
	char state_name[MAX_STR_NAME] = "";
	int state_split = 0;
	int state_data=0;
	char type = 0;
	int i;
	const char *str1;
	char cfg_file[MAX_STR_NAME];
	char config_param[MAX_STR_NAME], config_value[MAX_STR_NAME];
	int cfg_params = 0;


	/*****************
	 * READ CLIENT CONFIGURATION PARAMETERS
	 **********/
	file = fopen(CLIENT_CONFIG_FILE, "r");
	if(file==NULL){
		LOG_MESSAGE( "WARN -  cannot open configuration file %s\n", CLIENT_CONFIG_FILE);
#ifdef WIN32
		char cfg_file2[MAX_STR_NAME]; 
		snprintf(cfg_file2, MAX_STR_NAME, "..\\conf\\%s", CLIENT_CONFIG_FILE);
		file = fopen(cfg_file2, "r");
		if (file==NULL){
			LOG_MESSAGE( "ERROR -  cannot open configuration file %s\n", cfg_file2);
			return -1;
		}
#else
		return -1;
#endif
	}
	
	while ( fgets(line, 300, file)){
		if (line[0] == '/' && line[1]=='/')
			continue;
		if(sscanf(line, "%[^=]=\"%[^\";]; ",config_param, config_value) < 1)
			break;
		if(strcmp(config_param, "SERVER_NAME_1") == 0){
			snprintf(srv1, MAX_STR_NAME, "%s", config_value);
			LOG_MESSAGE("SERVER_NAME_1=%s\n", srv1);
			cfg_params++;
		}
		if(strcmp(config_param, "SERVER_NAME_2") == 0){
			snprintf(srv2, MAX_STR_NAME, "%s", config_value);
			LOG_MESSAGE("SERVER_NAME_2=%s\n", srv2);
			cfg_params++;
		}
		if(strcmp(config_param, "SERVER_NAME_3") == 0){
			snprintf(srv3, MAX_STR_NAME, "%s", config_value);
			LOG_MESSAGE("SERVER_NAME_3=%s\n", srv3);
			cfg_params++;
		}
		if(strcmp(config_param, "SERVER_NAME_4") == 0){
			snprintf(srv4, MAX_STR_NAME, "%s", config_value);
			LOG_MESSAGE("SERVER_NAME_4=%s\n", srv4);
			cfg_params++;
		}
		if(strcmp(config_param, "SERVER_NAME_5") == 0){
			snprintf(srv5, MAX_STR_NAME, "%s", config_value);
			LOG_MESSAGE("SERVER_NAME_5=%s\n", srv5);
			cfg_params++;
		}
		if(strcmp(config_param, "SERVER_NAME_6") == 0){
			snprintf(srv6, MAX_STR_NAME, "%s", config_value);
			LOG_MESSAGE("SERVER_NAME_6=%s\n", srv6);
			cfg_params++;
		}
		if(strcmp(config_param, "SERVER_NAME_7") == 0){
			snprintf(srv7, MAX_STR_NAME, "%s", config_value);
			LOG_MESSAGE("SERVER_NAME_7=%s\n", srv7);
			cfg_params++;
		}
		if(strcmp(config_param, "SERVER_NAME_8") == 0){
			snprintf(srv8, MAX_STR_NAME, "%s", config_value);
			LOG_MESSAGE("SERVER_NAME_8=%s\n", srv8);
			cfg_params++;
		}

		if(strcmp(config_param, "IHM_ADDRESS") == 0){
			snprintf(ihm_addr, MAX_STR_NAME, "%s", config_value);
			LOG_MESSAGE("IHM_ADDRESS=%s\n", ihm_addr);
			cfg_params++;
		}

		if(strcmp(config_param, "CLIENT_BKP_ADDRESS") == 0){
			snprintf(bkp_addr, MAX_STR_NAME, "%s", config_value);
			LOG_MESSAGE("CLIENT_BKP_ADDRESS=%s\n", bkp_addr);
			cfg_params++;
		}

		if(strcmp(config_param, "CLIENT_STATS_ADDRESS") == 0){
			snprintf(stats_addr, MAX_STR_NAME, "%s", config_value);
			LOG_MESSAGE("CLIENT_STATS_ADDRESS=%s\n", stats_addr);
			cfg_params++;
		}

		if(strcmp(config_param, "CONFIG_FILE") == 0){
			snprintf(cfg_file, MAX_STR_NAME, "%s", config_value);
			LOG_MESSAGE("CONFIG_FILE=%s\n", cfg_file);
			cfg_params++;
		}
		if(strcmp(config_param, "DATASET_INTEGRITY_TIME") == 0){
			integrity_time = atoi(config_value);
			LOG_MESSAGE("DATASET_INTEGRITY_TIME=%d\n", integrity_time);
			cfg_params++;
		}
		if(strcmp(config_param, "DATASET_ANALOG_BUFFER_INTERVAL") == 0){
			analog_buf = atoi(config_value);
			LOG_MESSAGE("DATASET_ANALOG_BUFFER_INTERVAL=%d\n", analog_buf);
			cfg_params++;
		}
		if(strcmp(config_param, "DATASET_DIGITAL_BUFFER_INTERVAL") == 0){
			digital_buf = atoi(config_value);
			LOG_MESSAGE("DATASET_DIGITAL_BUFFER_INTERVAL=%d\n", digital_buf);
			cfg_params++;
		}
		if(strcmp(config_param, "DATASET_EVENTS_BUFFER_INTERVAL") == 0){
			events_buf = atoi(config_value);
			LOG_MESSAGE("DATASET_EVENTS_BUFFER_INTERVAL=%d\n", events_buf);
			cfg_params++;
		}
	}

	if (cfg_params!=16){
		LOG_MESSAGE( "ERROR - wrong number of parameters on %s\n",CLIENT_CONFIG_FILE);
		return -1;
	}

	/*****************
	 * START CONFIGURATIONS
	 **********/

    analog_cfg = calloc(DATASET_MAX_SIZE*DATASET_ANALOG_MAX_NUMBER, sizeof(data_config) ); 
    digital_cfg = calloc(DATASET_MAX_SIZE*DATASET_DIGITAL_MAX_NUMBER, sizeof(data_config) ); 
    events_cfg  = calloc(DATASET_MAX_SIZE*DATASET_EVENTS_MAX_NUMBER, sizeof(data_config) ); 
	srv_main.analog = calloc(DATASET_MAX_SIZE*DATASET_ANALOG_MAX_NUMBER,   sizeof(data_to_handle) );
	srv_main.digital = calloc(DATASET_MAX_SIZE*DATASET_DIGITAL_MAX_NUMBER, sizeof(data_to_handle) );
	srv_main.events  = calloc(DATASET_MAX_SIZE*DATASET_EVENTS_MAX_NUMBER,  sizeof(data_to_handle) );
	srv_bckp.analog = calloc(DATASET_MAX_SIZE*DATASET_ANALOG_MAX_NUMBER,   sizeof(data_to_handle) );
	srv_bckp.digital = calloc(DATASET_MAX_SIZE*DATASET_DIGITAL_MAX_NUMBER, sizeof(data_to_handle) );
	srv_bckp.events  = calloc(DATASET_MAX_SIZE*DATASET_EVENTS_MAX_NUMBER,  sizeof(data_to_handle) ); 

    commands  = calloc(COMMANDS_MAX_NUMBER , sizeof(command_config) ); 

	file = fopen(cfg_file, "r");
	if(file==NULL){
		LOG_MESSAGE( "WARN - cannot open configuration file %s\n", cfg_file);
#ifdef WIN32
		char cfg_file2[MAX_STR_NAME]; 
		snprintf(cfg_file2, MAX_STR_NAME, "..\\conf\\%s", cfg_file);
		file = fopen(cfg_file2, "r");
		if (file==NULL){
			LOG_MESSAGE( "WARN - cannot open configuration file %s\n", cfg_file2);
			snprintf(cfg_file2, MAX_STR_NAME, "..\\conf\\point_list.txt");
			file = fopen(cfg_file2, "r");
			if (file==NULL){
				LOG_MESSAGE( "ERROR - cannot open configuration file %s\n", cfg_file2);
				return -1;
			}
		}
#else
		return -1;
#endif
	}
	
	//first two rows of CONFIG_FILE are the reader, discard them
	if(!fgets(line, 300, file)){
		LOG_MESSAGE( "ERROR - Error reading %s file header\n", cfg_file);
		return -1;
	}else {
		if(sscanf(line, "%*s %*d %*s %d", &ihm_station) <1){
			LOG_MESSAGE( "ERROR - cannot get ihm station from header\n");
			return -1;
		}else {
			LOG_MESSAGE("IHM station %d\n", ihm_station);
		}
	}

	if(!fgets(line, 300, file)){
		LOG_MESSAGE( "ERROR - Error reading %s file header second line\n", cfg_file);
		return -1;
	}

	while ( fgets(line, 300, file)){
		//if(sscanf(line, "%d %*d %22s %c", &configuration[num_of_ids].nponto,  configuration[num_of_ids].id, &configuration[num_of_ids].type ) <1)
		if(sscanf(line, "%d %*d %22s %c %31s %*d %*d %*d %d %*c %*d %*d %*f %*f %d %d", &nponto,  id_ponto, &type, state_name, &origin, &nponto_monitored, &event ) <1)
			break;

		//change - for $
		if (convert_hyphen_to_dollar){
			for ( i=0; i <22; i++) {
				if (id_ponto[i] == '-'){
					id_ponto[i] = '$';
				}
			}
		}
		//change + for $
		for ( i=0; i <22; i++) {
			if (id_ponto[i] == '+'){
				id_ponto[i] = '$';
			}
		}

	    if(origin == ORIGIN_CALC){
			LOG_MESSAGE("INFO - Ignoring Calculate object %s\n", id_ponto);
		}else if (origin == ORIGIN_MANUAL){
			LOG_MESSAGE("INFO - Ignoring Manual object %s\n", id_ponto);
		}else if (origin == ORIGIN_LUA){
			LOG_MESSAGE("INFO - Ignoring Lua object %s\n", id_ponto);
		}//Command Digital or Analog
		else if((type == 'D'||type=='A') && origin == ORIGIN_COMMAND ){
			//add $C to the end of command
			if (id_ponto[21] == 'K') {
				id_ponto[22] = '$';
				id_ponto[23] = 'C';
			}
			memcpy(commands[num_of_commands].id,id_ponto,25);
			commands[num_of_commands].nponto = nponto;
			commands[num_of_commands].monitored = nponto_monitored;
			if(type=='D')
				commands[num_of_commands].type = DATASET_COMMAND_DIGITAL;
			else
				commands[num_of_commands].type = DATASET_COMMAND_ANALOG;

			num_of_commands++;
		}//Events
		else if(type == 'D' && event == 3 && origin == ORIGIN_DEFAULT){
			memcpy(events_cfg[num_of_event_ids].id,id_ponto,25);
			events_cfg[num_of_event_ids].nponto = nponto;
			
			state_split=0;
			for ( i=0; i <MAX_STR_NAME; i++) {
				if (state_name[i] == '/' ){
					state_split=i;
					events_cfg[num_of_event_ids].state_on[i]=0;
					continue;
				}
				if(state_split){
					if (state_name[i] == 0 ) {
						events_cfg[num_of_event_ids].state_off[i-state_split-1]=0;
						break;
					}else
						events_cfg[num_of_event_ids].state_off[i-state_split-1] = state_name[i];

				}else
					events_cfg[num_of_event_ids].state_on[i]=state_name[i];
			}
			num_of_event_ids++;
		} //Digital
		else if(type == 'D' && origin == ORIGIN_DEFAULT){
			memcpy(digital_cfg[num_of_digital_ids].id,id_ponto,25);
			digital_cfg[num_of_digital_ids].nponto = nponto;
			
			state_split=0;
			for ( i=0; i <MAX_STR_NAME; i++) {
				if (state_name[i] == '/' ){
					state_split=i;
					digital_cfg[num_of_digital_ids].state_on[i]=0;
					continue;
				}
				if(state_split){
					if (state_name[i] == 0 ) {
						digital_cfg[num_of_digital_ids].state_off[i-state_split-1] =0;
						break;
					}else
						digital_cfg[num_of_digital_ids].state_off[i-state_split-1] = state_name[i];
				}else
					digital_cfg[num_of_digital_ids].state_on[i]=state_name[i];
			}
			num_of_digital_ids++;
		} //Analog
		else if(type == 'A'  && origin == ORIGIN_DEFAULT){
			memcpy(analog_cfg[num_of_analog_ids].id,id_ponto,25);
			analog_cfg[num_of_analog_ids].nponto = nponto;
			memcpy(analog_cfg[num_of_analog_ids].state_on,state_name,  16);
			num_of_analog_ids++;
		} //Unknown 
		else if(origin != ORIGIN_DEFAULT){
			LOG_MESSAGE("INFO - Ignoring Unkwnon type object %s\n", id_ponto);
		}
		else {
			LOG_MESSAGE("WARNING - ERROR reading configuration file! Unknown type\n");
		}
	}

#ifdef LOG_CONFIG
	LOG_MESSAGE( "***************ANALOG***********************\n");
	LOG_MESSAGE( " DATASET OFFSET NPONTO DESCRITIVO\n");
	for (i=0; i < num_of_analog_ids; i++) {
		LOG_MESSAGE( "%d %d %d \t%s\t \n",((i+1)/DATASET_MAX_SIZE), i, analog_cfg[i].nponto,  analog_cfg[i].id);
	}

	LOG_MESSAGE( "***************DIGITAL***********************\n");
	LOG_MESSAGE( " DATASET OFFSET NPONTO DESCRITIVO\n");
	for (i=0; i < num_of_digital_ids; i++) {
		LOG_MESSAGE( "%d %d %d \t%s\t \n",((i+1)/DATASET_MAX_SIZE),i, digital_cfg[i].nponto,  digital_cfg[i].id);
	}

	LOG_MESSAGE( "***************EVENTS***********************\n");
	LOG_MESSAGE( " DATASET OFFSET NPONTO DESCRITIVO\n");
	for (i=0; i < num_of_event_ids; i++) {
		LOG_MESSAGE( "%d %d %d \t%s\t \n",((i+1)/DATASET_MAX_SIZE),i, events_cfg[i].nponto,  events_cfg[i].id);
	}

	LOG_MESSAGE( "***************COMMANDS***********************\n");
	LOG_MESSAGE( " OFFSET NPONTO SUPERV DESCRITIVO \n");
	for (i=0; i < num_of_commands; i++) {
		LOG_MESSAGE( "%d %d \t %d \t%s \t\n",i, commands[i].nponto, commands[i].monitored,  commands[i].id);
	}
#endif

	num_of_analog_datasets = num_of_analog_ids/DATASET_MAX_SIZE;
	if(num_of_analog_ids%DATASET_MAX_SIZE)
		num_of_analog_datasets++;

	num_of_digital_datasets += num_of_digital_ids/DATASET_MAX_SIZE;
	if(num_of_digital_ids%DATASET_MAX_SIZE)
		num_of_digital_datasets++;

	num_of_event_datasets += num_of_event_ids/DATASET_MAX_SIZE;
	if(num_of_event_ids%DATASET_MAX_SIZE)
		num_of_event_datasets++;

	num_of_datasets = num_of_analog_datasets + num_of_digital_datasets + num_of_event_datasets;

	//alloc data for datasets
	dataset_conf = calloc(num_of_datasets, sizeof(dataset_config));

	for (i=0; i < num_of_datasets; i++) {
		snprintf(dataset_conf[i].id, DATASET_NAME_SIZE, "ds_%03d", i);
	}

	fclose(file);
	return 0;
}
/*********************************************************************************************************/
static void sigint_handler(int signalId)
{
	running = 0;
}
/*********************************************************************************************************/
static void cleanup_variables()
{
	int i;

	Thread_destroy(connections_thread);
	if(bkp_enabled)
		Thread_destroy(bkp_thread);

	if(stats_enabled)
		Thread_destroy(stats_thread);

	/* TODO: 
	
	if(srv_main.enabled)
		MmsConnection_conclude(srv_main.con, &mmsError);

	if(srv_bckp.enabled)
		MmsConnection_conclude(srv_bckp.con, &mmsError);

	MmsConnection_destroy(srv_main.con);
	MmsConnection_destroy(srv_bckp.con);
*/
	LOG_MESSAGE("cleanning up variables\n");

#ifdef LOG_COUNTERS
	LOG_MESSAGE( "***************ANALOG***********************\n");
	LOG_MESSAGE( " DATASET OFFSET NPONTO RCV_MSGS DESCRITIVO\n");
	for (i=0; i < num_of_analog_ids; i++) {
		LOG_MESSAGE( "%7d %6d %6d %7d \t%s\t \n",((i+1)/DATASET_MAX_SIZE),i, analog_cfg[i].nponto, analog_cfg[i].num_of_msg_rcv, analog_cfg[i].id);
	}

	LOG_MESSAGE( "***************DIGITAL***********************\n");
	LOG_MESSAGE( " DATASET OFFSET NPONTO RCV_MSGS DESCRITIVO\n");
	for (i=0; i < num_of_digital_ids; i++) {
		LOG_MESSAGE( "%7d %6d %6d %7d \t%s\t \n",((i+1)/DATASET_MAX_SIZE),i, digital_cfg[i].nponto, digital_cfg[i].num_of_msg_rcv, digital_cfg[i].id);
	}

	LOG_MESSAGE( "***************EVENTS***********************\n");
	LOG_MESSAGE( " DATASET OFFSET NPONTO RCV_MSGS DESCRITIVO\n");
	for (i=0; i < num_of_event_ids; i++) {
		LOG_MESSAGE( "%7d %6d %6d %7d \t%s\t \n",((i+1)/DATASET_MAX_SIZE),i, events_cfg[i].nponto, events_cfg[i].num_of_msg_rcv, events_cfg[i].id);
	}
	LOG_MESSAGE("Total Sent %d - A:%d D:%d R:%d\n", (num_of_digital_msgs+num_of_analog_msgs+num_of_report_msgs),
				 num_of_analog_msgs, num_of_digital_msgs, num_of_report_msgs);
#endif
#ifdef DATA_LOG
	fclose(data_file_analog);
	fclose(data_file_digital);
	fclose(data_file_events);
#endif
	fclose(log_file);
	free(dataset_conf);
	free(analog_cfg);
	free(digital_cfg);
	free(events_cfg);
	free(srv_main.analog);
	free(srv_main.digital);
	free(srv_main.events);
	free(srv_bckp.analog);
	free(srv_bckp.digital);
	free(srv_bckp.events);

	if(ihm_enabled){
		close(ihm_main_socket_send);
		close(ihm_bkp_socket_send);
		close(ihm_socket_receive);
	}
	if(bkp_enabled){
		close(bkp_socket);
	}

	close(stats_socket_send);
	close(stats_socket_receive);

}
/*********************************************************************************************************/
#ifdef DATA_LOG
static int open_data_logs(void) {
	data_file_analog = fopen(DATA_ANALOG_LOG, "w");
	if(data_file_analog==NULL){
		LOG_MESSAGE("Error, cannot open configuration data log file %s\n", DATA_ANALOG_LOG);
		return -1;
	}
	data_file_digital = fopen(DATA_DIGITAL_LOG, "w");
	if(data_file_digital==NULL){
		LOG_MESSAGE("Error, cannot open configuration data log file %s\n", DATA_DIGITAL_LOG);
		return -1;
	}
	data_file_events = fopen(DATA_EVENTS_LOG, "w");
	if(data_file_events==NULL){
		LOG_MESSAGE("Error, cannot open configuration data log file %s\n", DATA_EVENTS_LOG);
		return -1;
	}
	return 0;

}
#endif
/*********************************************************************************************************/
static int create_ihm_comm(){
	ihm_main_socket_send = prepare_Send(ihm_addr, PORT_IHM_TRANSMIT, &ihm_main_sock_addr);
	if(ihm_main_socket_send < 0){
		LOG_MESSAGE("could not create UDP socket to trasmit to IHM\n");
		return -1;
	}	
	LOG_MESSAGE("Created UDP socket %d for IHM %s Port %d\n",ihm_main_socket_send, ihm_addr, PORT_IHM_TRANSMIT);

	if(bkp_enabled){
		ihm_bkp_socket_send = prepare_Send(bkp_addr, PORT_IHM_TRANSMIT, &ihm_bkp_sock_addr);
		if(ihm_bkp_socket_send < 0){
			LOG_MESSAGE("could not create UDP socket to trasmit to IHM bkp\n");
			return -1;
		}	
		LOG_MESSAGE("Created UDP socket %d for IHM bkp %s Port %d\n",ihm_bkp_socket_send, bkp_addr, PORT_IHM_TRANSMIT);
	}

	ihm_socket_receive = prepare_Wait(PORT_IHM_LISTEN);
	if(ihm_socket_receive < 0){
		LOG_MESSAGE("could not create UDP socket to listen to IHM\n");
		close(ihm_main_socket_send);
		return -1;
	}
	LOG_MESSAGE("Created UDP local socket %d for IHM Port %d\n",ihm_socket_receive,PORT_IHM_LISTEN);
	return 0;
}
/*********************************************************************************************************/
static int create_bkp_comm(){

	bkp_socket = prepare_Wait(PORT_CLIENT_BACKUP);
	if(bkp_socket < 0){
		LOG_MESSAGE("could not create UDP socket to listen to Backup Client\n");
		return -1;
	}
	if(prepareServerAddress(bkp_addr, PORT_CLIENT_BACKUP, &bkp_sock_addr) < 0){
	  	LOG_MESSAGE("error preparing backup address\n");
	  	return -1;
	}
	LOG_MESSAGE("Created UDP socket %d for  Bakcup Port %d\n",bkp_socket,PORT_CLIENT_BACKUP);
	return 0;
}
/*********************************************************************************************************/
static int create_stats_comm(){

	stats_socket_receive = prepare_Wait(PORT_STATS_LISTEN);
	if(stats_socket_receive < 0){
		LOG_MESSAGE("could not create UDP socket to listen to Stats Messages\n");
		return -1;
	}
	LOG_MESSAGE("Created UDP socket %d for stats listen Port %d\n",stats_socket_receive,PORT_STATS_LISTEN);
	
	stats_socket_send = prepare_Send(stats_addr, PORT_STATS_TRANSMIT, &stats_sock_addr);
	if(stats_socket_send < 0){
		LOG_MESSAGE("could not create UDP socket to trasmit stats\n");
		close(stats_socket_receive);
		return -1;
	}	
	
	LOG_MESSAGE("Created UDP socket %d for stats transmit Port %d\n",stats_socket_send,PORT_STATS_TRANSMIT);
	return 0;
}

/*********************************************************************************************************/
static int check_backup(unsigned int msg_timeout){
	char * msg_rcv;
	msg_rcv = WaitT(bkp_socket, msg_timeout);	
	if(msg_rcv != NULL) {
		unsigned int msg_code = 0;
		memcpy(&msg_code, msg_rcv, sizeof(unsigned int));
		free(msg_rcv);
		if(msg_code == CLIENT_BACKUP_SIGNATURE) 
			return 0;		
	}
	return -1;
}
/*********************************************************************************************************/
static void check_commands(){
	char * msg_rcv;
	msg_rcv = WaitT(ihm_socket_receive, 2000);	
	if(msg_rcv != NULL) {
		int i;
		t_msgcmd cmd_recv = {0};
		memcpy(&cmd_recv, msg_rcv, sizeof(t_msgcmd));
		if(cmd_recv.signature != 0x4b4b4b4b) {
			char * cmd_debug = (char *)&cmd_recv;
			LOG_MESSAGE("ERROR - command received with wrong signature!\n");  
			for (i=0; i < sizeof(t_msgcmd); i++){
				LOG_MESSAGE(" %02x", cmd_debug[i]);
			}
			LOG_MESSAGE("\n");
		}
		for (i=0; i < num_of_commands; i++) {
			if(cmd_recv.endereco==commands[i].nponto)
				break;
		}
		if(i<num_of_commands){
			commands[i].num_of_msg_rcv++;
			LOG_MESSAGE( "Command %d, type %d, onoff %d, sbo %d, qu %d, utr %d\n", cmd_recv.endereco, cmd_recv.tipo, 
					cmd_recv.onoff, cmd_recv.sbo, cmd_recv.qu, cmd_recv.utr);

			if(srv_main.enabled) {
				//TODO:check if both connections enabled and send to the one with monitored state valid
				if(command_variable(srv_main.con, commands[i].id, cmd_recv.onoff)<0){
					send_cmd_response_to_ihm(ihm_main_socket_send, &ihm_main_sock_addr, commands[i].nponto, ihm_station, 0); //CMD ERROR
					if(bkp_enabled)
						send_cmd_response_to_ihm(ihm_bkp_socket_send, &ihm_bkp_sock_addr, commands[i].nponto, ihm_station, 0); //CMD ERROR
					commands[i].num_of_cmd_error++;
					LOG_MESSAGE("Error writing %d to %s\n", cmd_recv.onoff, commands[i].id);
				} else {
					send_cmd_response_to_ihm(ihm_main_socket_send, &ihm_main_sock_addr, commands[i].nponto, ihm_station, 1); //CMD OK
					if(bkp_enabled)
						send_cmd_response_to_ihm(ihm_bkp_socket_send, &ihm_bkp_sock_addr, commands[i].nponto, ihm_station, 1); //CMD OK
					commands[i].num_of_cmd_ok++;
					LOG_MESSAGE("Done writing %d to %s\n", cmd_recv.onoff, commands[i].id);
				}
			} else{
				LOG_MESSAGE("WARN - discarding command %d connection is not enabled\n", cmd_recv.endereco);
			}
		}else{
			char * cmd_debug = (char *)&cmd_recv;
			LOG_MESSAGE("ERROR - command received %d not found! \n", cmd_recv.endereco);  
			for (i=0; i < sizeof(t_msgcmd); i++){
				LOG_MESSAGE(" %02x", cmd_debug[i]);
			}
			LOG_MESSAGE("\n");
		}
		free(msg_rcv);
	}
}
/*********************************************************************************************************/
static void * check_bkp_thread(void * parameter)
{
	LOG_MESSAGE("Backup Thread Started\n");
	while(running){
		if(check_backup(2000) == 0){
			running = 0;	
			LOG_MESSAGE("ERROR - Detected backup from other server - exiting\n");
		}
		if(SendT(bkp_socket,(void *)&bkp_signature, sizeof(unsigned int), &bkp_sock_addr) < 0){
			running = 0;
			LOG_MESSAGE("Error sending message to backup server\n");
		}
	}
	return NULL;
}

/*********************************************************************************************************/
static void * check_stats_thread(void * parameter)
{
	LOG_MESSAGE("Stats Thread Started\n");
	int i;

	while(running){
		stats_data_msg * msg_rcv;
		msg_rcv = (stats_data_msg *) WaitT(stats_socket_receive, 2000);	
		if(msg_rcv != NULL) {
			if (msg_rcv->cmd== GET_GLOBAL_COUNTERS) {
				stats_global_counters msg_send;
				msg_send.analog_ids =  num_of_analog_ids;	
				msg_send.digital_ids = num_of_digital_ids;	
				msg_send.event_ids =  num_of_event_ids;	
				msg_send.commands = num_of_commands;	
				msg_send.datasets = num_of_datasets;
				msg_send.analog_datasets = num_of_analog_datasets;
				msg_send.digital_datasets = num_of_digital_datasets;
				msg_send.event_datasets = num_of_event_datasets;
				if(SendT(stats_socket_send,(void *)&msg_send, sizeof(stats_global_counters), &stats_sock_addr) < 0){
					LOG_MESSAGE("Error sending message to backup server\n");
				}
			} else if (msg_rcv->cmd == GET_HMI_COUNTERS) {
				stats_hmi_counters msg_send;
				msg_send.report_msgs = num_of_report_msgs;
				msg_send.digital_msgs = num_of_digital_msgs;
				msg_send.analog_msgs = num_of_analog_msgs;
				if(SendT(stats_socket_send,(void *)&msg_send, sizeof(stats_hmi_counters), &stats_sock_addr) < 0){
					LOG_MESSAGE("Error sending message to backup server\n");
				}
			} else if (msg_rcv->cmd == GET_NPONTO_COUNTERS) {
				stats_nponto_counters msg_send;
				int found = 0;
				memset(&msg_send, 0xFF , sizeof(stats_nponto_counters));
				for (i=0;((i<num_of_analog_ids) && !found); i++) {
					if(analog_cfg[i].nponto == msg_rcv->nponto){
						memcpy(&msg_send, &analog_cfg[i], sizeof(stats_nponto_counters));
						found = 1;
					}
				}
				for (i=0;((i<num_of_digital_ids) && !found); i++) {
					if(digital_cfg[i].nponto == msg_rcv->nponto){
						memcpy(&msg_send, &digital_cfg[i], sizeof(stats_nponto_counters));
						found = 1;
					}
				}
				for (i=0;((i<num_of_event_ids) && !found); i++) {
					if(events_cfg[i].nponto == msg_rcv->nponto){
						memcpy(&msg_send, &events_cfg[i], sizeof(stats_nponto_counters));
						found = 1;
					}
				}
				if(SendT(stats_socket_send,(void *)&msg_send, sizeof(stats_nponto_counters), &stats_sock_addr) < 0){
					LOG_MESSAGE("Error sending message to backup server\n");
				}
			} else if (msg_rcv->cmd == GET_NPONTO_STATE) {
				stats_nponto_state msg_send;
				int found = 0;
				memset(&msg_send, 0xFF , sizeof(stats_nponto_state));
				for (i=0;((i<num_of_analog_ids) && !found); i++) {
					if(analog_cfg[i].nponto == msg_rcv->nponto){
						memcpy(&msg_send, &srv_main.analog[i], sizeof(stats_nponto_state));
						found = 1;
					}
				}
				for (i=0;((i<num_of_digital_ids) && !found); i++) {
					if(digital_cfg[i].nponto == msg_rcv->nponto){
						memcpy(&msg_send, &srv_main.digital[i], sizeof(stats_nponto_state));
						found = 1;
					}
				}
				for (i=0;((i<num_of_event_ids) && !found); i++) {
					if(events_cfg[i].nponto == msg_rcv->nponto){
						memcpy(&msg_send, &srv_main.events[i], sizeof(stats_nponto_state));
						found = 1;
					}
				}
				if(SendT(stats_socket_send,(void *)&msg_send, sizeof(stats_nponto_counters), &stats_sock_addr) < 0){
					LOG_MESSAGE("Error sending message to backup server\n");
				}
			} else if (msg_rcv->cmd == GET_CMD_COUNTERS) {
				stats_cmd_counters msg_send;
				memset(&msg_send, 0xFF , sizeof(stats_nponto_counters));
				for (i=0;i<num_of_commands; i++) {
					if(commands[i].nponto == msg_rcv->nponto){
						memcpy(&msg_send, &commands[i], sizeof(stats_cmd_counters));
						break;
					}
				}
				if(SendT(stats_socket_send,(void *)&msg_send, sizeof(stats_cmd_counters), &stats_sock_addr) < 0){
					LOG_MESSAGE("Error sending message to backup server\n");
				}
			 } else {
				 LOG_MESSAGE ("ERROR - unknown stats message %d\n", msg_rcv->cmd);
			 } 
			free(msg_rcv);
		}	
	}
	return NULL;
}

/*********************************************************************************************************/

int start_bkp_configuration(void){
	if(strcmp(bkp_addr,"no")==0) {
		LOG_MESSAGE("no client backup configured\n");
		bkp_present=0;
	}else{
		bkp_enabled=1;
		bkp_present=10;
		LOG_MESSAGE("bkp enabled\n");
		if(create_bkp_comm() < 0){
			LOG_MESSAGE("Error, cannot open communication to bkp server\n");
			return -1;
		}
		while (running && bkp_present){
			if(check_backup(((rand()%400) + 600)) < 0){//random buffer timeout
				bkp_present--;
			}else{
				bkp_present=10;
			}
		}

		if(!running){
			return -1;
		}	

		//Send Message to backup in order to mantain it disabled	
		if((SendT(bkp_socket,(void *)&bkp_signature, sizeof(unsigned int), &bkp_sock_addr)) < 0){
			LOG_MESSAGE("Error sending message to backup server on initialization\n");
			return -1;
		}
		if ((SendT(bkp_socket,(void *)&bkp_signature, sizeof(unsigned int), &bkp_sock_addr)) < 0){
			LOG_MESSAGE("Error sending message to backup server on initialization\n");
			return -1;
		}

		bkp_thread = Thread_create(check_bkp_thread, NULL, false);
		Thread_start(bkp_thread);

		LOG_MESSAGE("Backup client not alive\n");
	}
	return 0;
}
/*********************************************************************************************************/
int start_stats_configuration(void){
	if(strcmp(stats_addr,"no")==0) {
		LOG_MESSAGE("no client stats configured\n");
	}else{
		LOG_MESSAGE("stats enabled\n");
		stats_enabled=1;
		if(create_stats_comm() < 0){
			LOG_MESSAGE("Error, cannot open communication to stats server\n");
			return -1;
		}
		
		stats_thread = Thread_create(check_stats_thread, NULL, false);
		Thread_start(stats_thread);
	}
	return 0;
}

/*********************************************************************************************************/
static void * check_connections_thread(void * parameter)
{
	LOG_MESSAGE("Connections Thread Started\n");
	//if connection not enabled or lost, restart all
	while(running){
		if (srv_main.enabled){
			if (check_connection(srv_main.con,&srv_main.error) < 0){
				srv_main.enabled=0;
			}
		} else {
			if(connect_to_server(&srv_main.con, srv1,srv2,srv3,srv4) == 0){
				//FIXME: MmsConnection_setInformationReportHandler(srv_main.con, informationReportHandler, (void *) &srv_main);
				Thread_sleep(10000);
				if((check_connection(srv_main.con,&srv_main.error) < 0) || (start_client(&srv_main)<0)){
					LOG_MESSAGE("could not start configuration for connection principal\n");
					running = 0;
				} else{
					srv_main.enabled=1;
				}

			}
		}
		if (srv_bckp.enabled){
			if (check_connection(srv_bckp.con,&srv_bckp.error) < 0){
				srv_bckp.enabled=0;
			}
		} else {
			if(connect_to_server(&srv_bckp.con, srv5,srv6,srv7,srv8) == 0){
	 			//FIXME: MmsConnection_setInformationReportHandler(srv_bckp.con, informationReportHandler, (void *) &srv_bckp);
	 			Thread_sleep(10000);
				if((check_connection(srv_bckp.con, &srv_bckp.error) < 0) || (start_client(&srv_bckp)<0)){
					LOG_MESSAGE("could not start configuration for connection backup\n");
					running = 0;
				} else
					srv_bckp.enabled=1;
			}
		}
		Thread_sleep(2000);
	}
	return NULL;

}
/*********************************************************************************************************/
int main (int argc, char ** argv){
	unsigned int i = 0;
	gettimeofday(&start, NULL);
	signal(SIGINT, sigint_handler);
	///TODO:srv_main.con = MmsConnection_create();
	//TODO: srv_bckp.con = MmsConnection_create();
	analog_queue.mutex = Semaphore_create(1); //semaphore
	digital_queue.mutex = Semaphore_create(1); //semaphore
    digital_mutex = Semaphore_create(1); //semaphore
    localtime_mutex = Semaphore_create(1); //semaphore

	// OPEN LOG FILE
	if (open_log_file() != 0) {
		printf("Error opening log file\n");
		return -1;
	}

	// READ CONFIGURATION FILE
	if (read_configuration() != 0) {
		LOG_MESSAGE("Error reading configuration\n");
		return -1;
	} else {
		LOG_MESSAGE("Start configuration with:\n  %d analog, %d digital, %d events, %d commands\n", num_of_analog_ids, num_of_digital_ids, num_of_event_ids, num_of_commands);
		LOG_MESSAGE("  %d datasets:\n   - %d analog\n   - %d digital \n   - %d events \n", num_of_datasets, num_of_analog_datasets, num_of_digital_datasets, num_of_event_datasets);
	}

#ifdef DATA_LOG
	// OPEN DATA LOG FILES	
	if(open_data_logs()<0) {
		LOG_MESSAGE("Error, cannot open configuration data log files\n");
		cleanup_variables();
		return -1;
	}
#endif

	//CHECK IF BACKUP CLIENT IS CONFIGURED AND START IT
	if(start_bkp_configuration() < 0){
		cleanup_variables();
		return -1;
	}
			
	//INITIALIZE IHM CONNECTION 
	if(strcmp(ihm_addr,"no")==0) {
		LOG_MESSAGE("no ihm configured\n");
	}else{
		ihm_enabled=1;
		if(create_ihm_comm() < 0){
			LOG_MESSAGE("Error, cannot open communication to ihm server\n");
			cleanup_variables();
			return -1;
		}
	}

	//START Stats Thread
	if(start_stats_configuration() < 0){
		cleanup_variables();
		return -1;
	}
	
	//TODO: INITIALIZE I104 CONNECTIONs TO SERVERs 
/*	if(connect_to_server(&srv_main.con, srv1,srv2,srv3,srv4) < 0){
		LOG_MESSAGE("Warning, cannot connect to client pool main\n");
	} else{
		srv_main.enabled = 1;
	}
	if(connect_to_server(&srv_bckp.con, srv5,srv6,srv7,srv8) < 0){
		LOG_MESSAGE("Warning, cannot connect to pool backup\n");
	} else {
		srv_bckp.enabled = 1;
	}
*/	
	if(!srv_main.enabled && !srv_bckp.enabled){
		LOG_MESSAGE("Error,Could not connect to any servers\n");
	}
	
	// HANDLE REPORTS
//	MmsConnection_setInformationReportHandler(srv_main.con, informationReportHandler, (void *) &srv_main);
//	MmsConnection_setInformationReportHandler(srv_bckp.con, informationReportHandler, (void *) &srv_bckp);

	//START CONFIGURATION
/*	if(srv_main.enabled){
	   if(start_client(&srv_main)<0){
			LOG_MESSAGE("could not start configuration for connection principal\n");
	   }
	}
	if(srv_bckp.enabled){
	   if(start_client(&srv_bckp)<0){
			LOG_MESSAGE("could not start configuration for connection backup\n");
	   }
	}
*/		
	connections_thread = Thread_create(check_connections_thread, NULL, false);
	Thread_start(connections_thread);
	
	LOG_MESSAGE("Client Process Successfully Started!\n");

	// LOOP TO MANTAIN CONNECTION ACTIVE AND CHECK COMMANDS	
	while(running) {

		if(!ihm_enabled)
			Thread_sleep(2000);
		else{
			if(ihm_socket_receive > 0)
				check_commands();
	
			if(ihm_main_socket_send < 0){
				LOG_MESSAGE("IHM main socket closed\n");
				running=0;
			} else if(bkp_enabled && ihm_bkp_socket_send < 0) {
				LOG_MESSAGE("IHM bkp socket closed\n");
				running=0;
			} else {
				
				// EMPTY ANALOG QUEUE	
/*				Semaphore_wait(analog_queue.mutex);	
				if(analog_queue.size && ((get_time_ms()-analog_queue.time) > 4000)){//timeout to send analog buffered messages if not empty
					if(send_analog_list_to_ihm(ihm_main_socket_send, &ihm_main_sock_addr,analog_queue.npontos, ihm_station, analog_queue.values, analog_queue.states, analog_queue.size) <0){
						LOG_MESSAGE( "Error sending analog buffer to main IHM\n");
					}
					if(bkp_enabled){
						if(send_analog_list_to_ihm(ihm_bkp_socket_send, &ihm_bkp_sock_addr,analog_queue.npontos, ihm_station, analog_queue.values, analog_queue.states, analog_queue.size) <0){
							LOG_MESSAGE( "Error sending analog buffer to bkp IHM \n");
						}
					}
					num_of_analog_msgs++;
					analog_queue.size=0;
				}
				Semaphore_post(analog_queue.mutex);	

				// EMPTY DIGITAL QUEUE	
				Semaphore_wait(digital_queue.mutex);	
				if(digital_queue.size && ((get_time_ms()- digital_queue.time) > 3000)){//timeout to send digital buffered messages if not empty
					if(send_digital_list_to_ihm(ihm_main_socket_send, &ihm_main_sock_addr,digital_queue.npontos, ihm_station,  digital_queue.states, digital_queue.size) <0){
						LOG_MESSAGE( "Error sending digital buffer to main IHM \n");
					}
					if(bkp_enabled){
						if(send_digital_list_to_ihm(ihm_bkp_socket_send, &ihm_bkp_sock_addr,digital_queue.npontos, ihm_station,  digital_queue.states, digital_queue.size) <0){
							LOG_MESSAGE( "Error sending digital buffer to bkp IHM\n");
						}
					}
					num_of_digital_msgs++;
					digital_queue.size=0;
				}
				Semaphore_post(digital_queue.mutex);	
				*/
			}
		}

	}

	cleanup_variables();

	return 0;
}

