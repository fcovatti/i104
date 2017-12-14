#include "i104.h"
#include "client.h"

int command_variable(int con, char * variable, int value){
	printf("command varible %s\n", variable);
	return 0;
}
int check_connection(int con, int * loop_error){
	printf("check connection\n");
	return 0;
}
int connect_to_server(int con, char * server){
	printf("connect to server\n");
	return 0;
}
int start_client(st_server_data * srv_data){
	printf("start server\n");
	return 0;
}

