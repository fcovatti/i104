#ifndef I104_H_INCLUDED
#define I104_H_INCLUDED
#include "client.h"
int command_variable(int con, char * variable, int value);
int check_connection(int con, int * loop_error);
int connect_to_server(int con, char * server);
int start_client(st_server_data * srv_data);
#endif
