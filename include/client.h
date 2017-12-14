#ifndef CLIENT_H_INCLUDED
#define CLIENT_H_INCLUDED

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
//#include <pthread.h>
#include <unistd.h>
#include <ctype.h>
#include <malloc.h>
#include <signal.h>
#include "client.h"
#include "hal_thread.h"
#include  <semaphore.h>
#include "comm.h"

#define CLIENT_CONFIG_FILE "client_config.txt"

#define MAX_STR_NAME 35

// Max size of the dataset.
#define DATASET_MAX_SIZE 65536

// MAx number of datasets allowed on the client
#define DATASET_ANALOG_MAX_NUMBER 1

// MAx number of datasets allowed on the client
#define DATASET_DIGITAL_MAX_NUMBER 1

// MAx number of datasets allowed on the client
#define DATASET_EVENTS_MAX_NUMBER 1

// MAx number of commands allowed on the client(maximum number for sage is 20000)
#define COMMANDS_MAX_NUMBER 20000

// Name of the data received log file
#define DATA_ANALOG_LOG "data_analog.bin"
#define DATA_DIGITAL_LOG "data_digital.bin"
#define DATA_EVENTS_LOG "data_events.bin"


// name size of the standard dataset created
#define DATASET_NAME_SIZE 7

// name size of SAGE standard transferset
#define TRANSFERSET_NAME_SIZE 13

// Max Connection errors before aborting
#define MAX_CONNECTION_ERRORS 10

// Max Reading variable error in 
#define MAX_READ_ERRORS 10

// Object configuration origins
#define ORIGIN_DEFAULT  0
#define ORIGIN_CALC  1
#define ORIGIN_MANUAL 6
#define ORIGIN_COMMAND 7
#define ORIGIN_LUA 23

//Report Handlings
#define REPORT_BUFFERED		0x01		//RBE flag, buffer reports
#define REPORT_INTERVAL_TIMEOUT 0x02    // send "General interrogations"
#define REPORT_OBJECT_CHANGES	0x04    // send reports

//State mask
//bit 0 is timestamp validity
#define STATE_MASK_TIMESTAMP_INVALID 	0x01
//bit 4 and 5 is data validity
#define STATE_MASK_DATA_INVALID	   		0x30
//bit 6 and 7 is data value
#define STATE_MASK_DATA_VALUE	   		0xC0

//Get state commands
#define GET_GLOBAL_COUNTERS 	0x01
#define GET_HMI_COUNTERS 		0x03
#define GET_NPONTO_COUNTERS 	0x05
#define GET_NPONTO_STATE	 	0x07
#define GET_CMD_COUNTERS	 	0x09

typedef struct {
	unsigned int cmd;
	unsigned int nponto;
} __attribute__((packed)) stats_data_msg;

typedef struct {
	unsigned int analog_ids;	
	unsigned int digital_ids;	
	unsigned int event_ids;	
	unsigned int commands;	
	unsigned int datasets;
	unsigned int analog_datasets;
	unsigned int digital_datasets;
	unsigned int event_datasets;
} __attribute__((packed)) stats_global_counters;

typedef struct {
	unsigned int report_msgs;
	unsigned int digital_msgs;
	unsigned int analog_msgs;
} __attribute__((packed)) stats_hmi_counters;

#define stats_nponto_counters data_config
#define stats_nponto_state data_to_handle
#define stats_cmd_counters command_config


typedef struct {
	unsigned int nponto;
	char id[25];
	char state_on[16];
	char state_off[16];
//	char type; //FIXME not used anymore...remove
	unsigned int num_of_msg_rcv;
	unsigned int num_of_reports;
	unsigned int num_of_flapping;
}  __attribute__((packed)) data_config;

typedef struct {
	float f;
	unsigned char state;
	time_t time_stamp;
	unsigned short time_stamp_extended;
	unsigned char not_present;
}  __attribute__((packed)) data_to_handle;

typedef struct {
	unsigned int nponto;
	char id[25];
	char type;
	unsigned int monitored;
	unsigned int num_of_msg_rcv;
	unsigned int num_of_cmd_ok;
	unsigned int num_of_cmd_error;
}  __attribute__((packed)) command_config;

typedef enum{
	DATASET_ANALOG,
	DATASET_DIGITAL,
	DATASET_EVENTS,
	DATASET_COMMAND_DIGITAL,
	DATASET_COMMAND_ANALOG,
} DataSetTypes;

typedef struct {
	char id[DATASET_NAME_SIZE];
	char ts[TRANSFERSET_NAME_SIZE];
	DataSetTypes type;
	int size;
	unsigned int offset;
	int num_of_rcv_gi;
} dataset_config;

typedef struct {
	unsigned int npontos[MAX_MSGS_SQ_ANALOG];
	float values[MAX_MSGS_SQ_ANALOG];
	unsigned char states[MAX_MSGS_SQ_ANALOG];
	unsigned int size;
	unsigned int time;
	Semaphore mutex;
} st_analog_queue;

typedef struct {
	unsigned int npontos[MAX_MSGS_SQ_DIGITAL];
	unsigned char states[MAX_MSGS_SQ_DIGITAL];
	unsigned int size;
	unsigned int time;
	Semaphore mutex;
} st_digital_queue;

typedef struct {
	unsigned int nponto;
	float f;
	unsigned char state;
	time_t time_stamp;
} __attribute__((packed)) data_analog_out;

typedef struct {
	unsigned int nponto;
	unsigned char state;
	time_t time_stamp;
	unsigned short time_stamp_extended;
} __attribute__((packed)) data_digital_out;

typedef union {
	float f;
	char  s[4];
}float_data ;

typedef union {
	unsigned int t;
	char  s[4];
} time_data;

//TODO: store for each server connection the data in here
typedef struct {
	int con;//connection fd
    int enabled;//connection enabled
    int error; //connection error
	data_to_handle * analog; //analog data
	data_to_handle * digital; //digital data
	data_to_handle * events;  //events
}__attribute__((packed)) st_server_data;

#endif
