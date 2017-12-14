#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <malloc.h>
#include <signal.h>
#include <errno.h>
#include <sys/socket.h>
#include <time.h>	/* For size_t */

#include "hal_thread.h"
#include "comm.h"

extern Semaphore localtime_mutex;

extern FILE * log_file;
#define LOG_MESSAGE(...)	do { print_time(log_file); fprintf(log_file, __VA_ARGS__); fflush(log_file); } while(false)
void print_time(FILE * log_file);
#ifdef WIN32

#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR,12)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdbool.h>
#include <windows.h>

static struct tm *localtime_r(const time_t *tloc, struct tm *result) {
	struct tm *tm;
	if((tm = localtime(tloc)))
		return memcpy(result, tm, sizeof(struct tm));
	return 0;
}

/*********************************************************************************************************/
int prepare_Send(char * addr, int port, struct sockaddr_in * server_addr){

	ServerSocket serverSocket = NULL;
	int ec;
	WSADATA wsa;
	SOCKET listen_socket = INVALID_SOCKET;
	socklen_t optlen;
	int buf_size = 163840;

	if ((ec = WSAStartup(MAKEWORD(2,0), &wsa)) != 0) {
		LOG_MESSAGE("winsock error: code %i\n");
		return -1;
	}

	if (prepareServerAddress(addr, port, server_addr) < 0){
	  	LOG_MESSAGE("error preparing address\n");
	  	return -1;
	}

	listen_socket = socket(AF_INET, SOCK_DGRAM,IPPROTO_UDP);

	if (listen_socket == INVALID_SOCKET) {
		LOG_MESSAGE("socket failed with error: %i\n", WSAGetLastError());
		WSACleanup();
		return -1;
	}
	
	optlen = sizeof(int);
	getsockopt(listen_socket,SOL_SOCKET, SO_SNDBUF, (char *) &buf_size, &optlen);
	LOG_MESSAGE("socket size %d\n", buf_size);

	setsockopt(listen_socket,SOL_SOCKET, SO_SNDBUF, (char *) &buf_size, optlen);
	setsockopt(listen_socket,SOL_SOCKET, SO_RCVBUF, (char *) &buf_size, optlen);
	
	return listen_socket;
}

/*********************************************************************************************************/
int SendT(int socketfd, void * msg, int msg_size, struct sockaddr_in * server_addr){
#ifdef DEBUG_MSGS
	printd("Sending message size %d\n", msg_size);
#endif
	if (sendto(socketfd, msg, msg_size, 0, (struct sockaddr*)server_addr, sizeof(struct sockaddr)) == SOCKET_ERROR) {
		LOG_MESSAGE("Error sending msg to IHM!\r\n");
		return -1;
	}
	return 0;
}

#else

/* FOR LINUX*/ 
#include <sys/socket.h>
#include <stddef.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

/*********************************************************************************************************/
int prepare_Send(char * addr, int port, struct sockaddr_in * server_addr)
{
	int socketfd;

	/*1) Create a UDP socket*/
	if ( (socketfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP) ) < 0 ) {
		LOG_MESSAGE("Error creating udp socket. Aborting!\r\n");
		return -1;
	}

	memset(server_addr, 0, sizeof(struct sockaddr_in));
	
	if (addr != NULL) {
		struct hostent *server;
		server = gethostbyname(addr);

		if (server == NULL){
		 	close(socketfd);
			LOG_MESSAGE("Error finding server name %s\r\n", addr);
			return -1;
		}

		memcpy((char *) &server_addr->sin_addr.s_addr, (char *) server->h_addr, server->h_length);
	}
	else {
		LOG_MESSAGE("null address\n");
		server_addr->sin_addr.s_addr = htonl(INADDR_ANY);
	}

	server_addr->sin_family = AF_INET;
	server_addr->sin_port = htons(port);
	
#ifdef DEBUG_MSGS
	printd("socket created %d - addr %s, port %d\n",socketfd, addr, port);
#endif
	return socketfd;
}

int SendT(int socketfd, void * msg, int msg_size, struct sockaddr_in * server_addr) {
	/*2) Establish connection*/
#ifdef DEBUG_MSGS
	printd("Sending message size %d to %d:%d\n", msg_size,server_addr->sin_port,server_addr->sin_addr.s_addr);
#endif
	if (sendto(socketfd, msg, msg_size, 0, (const struct sockaddr *)server_addr, sizeof(struct sockaddr)) < 0) {
		LOG_MESSAGE("Error sending msg to IHM!\r\n");
		return -1;
	}

	return 0;
}
#endif

/*********************************************************************************************************/
int prepare_Wait(int port)
{
	int socketfd;
	int keepalive=1;
	struct sockaddr_in servSock;

#ifdef WIN32
	int ec;
	WSADATA wsa;

	if ((ec = WSAStartup(MAKEWORD(2,0), &wsa)) != 0) {
		LOG_MESSAGE("winsock error: code %i\n");
		return -1;
	}
#endif

	/*Create a UDP socket for incoming connections*/
	if ( (socketfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP) ) < 0 ) {
		LOG_MESSAGE("Error creating a udp socket port %d!\n", port);
		return -1;
	}

	memset(&servSock, 0, sizeof(servSock));
	servSock.sin_family = AF_INET;
	servSock.sin_port = htons(port);
	servSock.sin_addr.s_addr = htonl(INADDR_ANY);


	setsockopt(socketfd,SOL_SOCKET, SO_KEEPALIVE,(char *) &keepalive, sizeof(keepalive));

	/* 2) Assign a port to a socket(bind)*/
	if (bind(socketfd, (const struct sockaddr *)&servSock, sizeof(servSock) ) < 0) {
		close(socketfd);
		LOG_MESSAGE("Error on bind port %d.. %s\r\n", port, strerror(errno));
#ifdef WIN32
		WSACleanup();
#endif
		return -1;
	}

#ifdef WIN32
	BOOL bNewBehavior = FALSE;
	DWORD dwBytesReturned =0;
	if(WSAIoctl(socketfd, SIO_UDP_CONNRESET, &bNewBehavior, sizeof(bNewBehavior), NULL, 0, &dwBytesReturned, NULL, NULL) == SOCKET_ERROR){
		LOG_MESSAGE("error set behavior socket win32\n");
	}
#endif
	return socketfd;

}

/*********************************************************************************************************/
void * WaitT(unsigned int socketfd, int timeout_ms) {
	void * buf = NULL;
	int n, ret;
	fd_set readfds;
	struct timeval timeout_sock;
	
	FD_ZERO(&readfds);
	FD_SET(socketfd, &readfds);
	timeout_sock.tv_sec = timeout_ms/1000;
	timeout_sock.tv_usec = (timeout_ms%1000)*1000;
	if(timeout_ms)
		ret = select(socketfd + 1, &readfds, NULL, NULL, &timeout_sock);
	else
		ret = select(socketfd + 1, &readfds, NULL, NULL, NULL);
	if (ret < 0) {
		LOG_MESSAGE("select error socket %d\n", socketfd);
		return NULL;
	} else if (ret == 0) {
		return NULL;
	}

	if (FD_ISSET(socketfd, &readfds)) {

		buf = malloc(2000);
		n = recv(socketfd, buf, 2000, 0);
		if (n < 0) {
			LOG_MESSAGE("ERROR reading from socket %d %d\n", socketfd, n);
#ifdef WIN32
				LOG_MESSAGE("error %d\n", WSAGetLastError());
#endif
			free(buf);
			return NULL;
		}
	}

	return buf;
}
/*********************************************************************************************************/
int prepareServerAddress(char* address, int port, struct sockaddr_in * server_addr) 
{
	memset((char *) server_addr , 0, sizeof(struct sockaddr_in));

	if (address != NULL) {
		struct hostent *server;
		server = gethostbyname(address);

		if (server == NULL) {
			LOG_MESSAGE("could not find server host by name %s\n", address);
			return -1;
		}

		memcpy((char *) &(server_addr->sin_addr.s_addr), (char *) server->h_addr, server->h_length);
	}
	else
		server_addr->sin_addr.s_addr = htonl(INADDR_ANY);

    server_addr->sin_family = AF_INET;
    server_addr->sin_port = htons(port);

    return 0;
}
/*********************************************************************************************************/
static unsigned char get_analog_state(unsigned char state)
{
	unsigned char analog_state;
	if(!(state&0x10) && !(state&0x20)) 
		analog_state=0;//valid
	else if((state&0x10) && !(state&0x20))
		analog_state=0x10;//held/blocked
	else
		analog_state=0x80;//invalid

	if((state&0x08) && !(state&0x04))
		analog_state = analog_state|0x20; //manual

	return analog_state;
}
/*********************************************************************************************************/
int send_analog_to_ihm(int socketfd, struct sockaddr_in * server_sock_addr,unsigned int nponto, unsigned char ihm_station, float value, unsigned char state, char report)
{
			t_msgsup msg_sup;
			flutuante_seq float_value;
			memset(&msg_sup, 0, sizeof(t_msgsup));
			msg_sup.signature = IHM_SINGLE_POINT_SIGN;
			msg_sup.endereco = nponto;
			msg_sup.prim = ihm_station;
			msg_sup.tipo = 13;

			if(report)
				msg_sup.causa=3; //report
			else
				msg_sup.causa=20; //integrity

			msg_sup.taminfo=sizeof(flutuante_seq);
			
			float_value.qds = get_analog_state(state);
			
			float_value.fr=value;
			memcpy(msg_sup.info,(char *) &float_value, sizeof(flutuante_seq));
			return SendT(socketfd,(void *)&msg_sup, sizeof(t_msgsup), server_sock_addr);
}	

/*********************************************************************************************************/
int send_analog_list_to_ihm(int socketfd, struct sockaddr_in * server_sock_addr,unsigned int * npontos, unsigned char ihm_station, float * values, unsigned char * states, int list_size)
{
	t_msgsupsq msg_sup;
	flutuante_seq float_value;
	int i, offset;
	
	if(list_size > MAX_MSGS_SQ_ANALOG){
		LOG_MESSAGE("wrong analog size list\n");
		return -1;
	}
	memset(&msg_sup, 0, sizeof(t_msgsupsq));
	msg_sup.signature = IHM_POINT_LIST_SIGN;
	msg_sup.numpoints = list_size;
	msg_sup.prim = ihm_station;
	msg_sup.tipo=13;
	msg_sup.causa=20; //integrity
	msg_sup.taminfo=sizeof(flutuante_seq);

	offset=sizeof(flutuante_seq)+sizeof(int);
	
	for (i=0; i < list_size; i++){
		float_value.qds = get_analog_state(states[i]);
		float_value.fr=values[i];
		memcpy(&(msg_sup.info[i*offset]),(char *) &npontos[i], sizeof(unsigned int));
		memcpy(&(msg_sup.info[i*offset+sizeof(int)]),(char *) &float_value, sizeof(flutuante_seq));
	}
	i = SendT(socketfd,(void *)&msg_sup, sizeof(t_msgsupsq), server_sock_addr);
	return i;

}
/*********************************************************************************************************/
static unsigned char get_digital_state(unsigned char state)
{
	unsigned char digital_state;
	if(!(state&0x40) && (state&0x80)) 
		digital_state=0x01;//off
	else if((state&0x40) && !(state&0x80))
		digital_state=0x00;//on
	else
		digital_state=0x80;//invalid

	if(!(state&0x10) && !(state&0x20)) 
		digital_state=digital_state|0;//valid
	else if((state&0x10) && !(state&0x20))
		digital_state=digital_state|0x10;//held/blocked
	else
		digital_state=digital_state|0x80;//invalid

	if((state&0x08) && !(state&0x04))
		digital_state=digital_state|0x20; //manual

	if(state&0x01){ 
		digital_state=digital_state|0x08; //invalid timestamp
	}
	return digital_state;

}
/*********************************************************************************************************/
int send_digital_to_ihm(int socketfd, struct sockaddr_in * server_sock_addr,unsigned int nponto, unsigned char ihm_station, unsigned char state, time_t time_stamp,unsigned short time_stamp_extended, char report){
	//struct tm * time_result;
	struct tm time_result = {0};

	t_msgsup msg_sup;
	unsigned char digital_state;

	memset(&msg_sup, 0, sizeof(t_msgsup));
	msg_sup.signature = IHM_SINGLE_POINT_SIGN;
	msg_sup.endereco = nponto;
	msg_sup.prim = ihm_station;
	
	digital_state = get_digital_state(state);

	//only send as report if timestamp is valid
	if(report && time_stamp != 0xffffffff){
		digital_w_time7_seq digital_value;
		msg_sup.causa=3; //report
		msg_sup.tipo=30;
		msg_sup.taminfo=sizeof(digital_w_time7_seq);
		digital_value.iq = digital_state;
		//time_result = (struct tm *)localtime(&time_stamp);
		//
		Semaphore_wait(localtime_mutex);	
		if(localtime_r(&time_stamp, &time_result) == 0){
			LOG_MESSAGE("error obtaining localtime nponto %d, time_stamp %d, extended %d \n", nponto, time_stamp, time_stamp_extended);
		}
		Semaphore_post(localtime_mutex);	

		digital_value.ms=(time_result.tm_sec*1000)+time_stamp_extended;
		digital_value.min=time_result.tm_min;
		digital_value.hora=time_result.tm_hour;
		digital_value.dia=time_result.tm_mday;
		digital_value.mes=time_result.tm_mon+1;
		if(time_result.tm_year >=100)
			digital_value.ano=time_result.tm_year-100;
		else
			digital_value.ano=time_result.tm_year;
		memcpy(msg_sup.info,(char *) &digital_value, sizeof(digital_w_time7_seq));
	}
	else {
		digital_seq digital_value_gi;
		msg_sup.tipo=1;
		if(report){
			msg_sup.causa=3; //report
			LOG_MESSAGE("report with invalid timestamp nponto %d, time_stamp %d, extended %d \n", nponto, time_stamp, time_stamp_extended);
		}else
			msg_sup.causa=20; //integrity
		msg_sup.taminfo=sizeof(digital_seq);
		digital_value_gi.iq = digital_state;
		memcpy(msg_sup.info,(char *) &digital_value_gi, sizeof(digital_seq));
	}
	
	return SendT(socketfd,(void *)&msg_sup, sizeof(t_msgsup), server_sock_addr);
}

/*********************************************************************************************************/
int send_digital_list_to_ihm(int socketfd, struct sockaddr_in * server_sock_addr,unsigned int * npontos, unsigned char ihm_station, unsigned char * states, int list_size)
{
	t_msgsupsq msg_sup;
	digital_seq digital_value_gi;
	int i, offset;
	
	if(list_size > MAX_MSGS_SQ_DIGITAL){
		LOG_MESSAGE("wrong digital size list\n");
		return -1;
	}

	memset(&msg_sup, 0, sizeof(t_msgsupsq));
	msg_sup.signature = IHM_POINT_LIST_SIGN;
	msg_sup.numpoints = list_size;
	msg_sup.prim = ihm_station;
	msg_sup.tipo=1;
	msg_sup.causa=20; //integrity
	msg_sup.taminfo=sizeof(digital_seq);
	offset=sizeof(digital_seq)+sizeof(int);
	
	for (i=0; i < list_size; i++){
		digital_value_gi.iq = get_digital_state(states[i]);
		memcpy(&(msg_sup.info[i*offset]),(char *) &npontos[i], sizeof(unsigned int));
		memcpy(&(msg_sup.info[i*offset+sizeof(int)]),(char *) &digital_value_gi, sizeof(digital_seq));
	}
	return SendT(socketfd,(void *)&msg_sup, sizeof(t_msgsupsq), server_sock_addr);
}

/*********************************************************************************************************/
int send_cmd_response_to_ihm(int socketfd, struct sockaddr_in * server_sock_addr,unsigned int nponto, unsigned char ihm_station, char cmd_ok){
	t_msgsup msg_sup;
	digital_seq digital_value_gi;
	memset(&msg_sup, 0, sizeof(t_msgsup));
	msg_sup.signature = IHM_SINGLE_POINT_SIGN;
	msg_sup.endereco = nponto;
	msg_sup.prim = ihm_station;
	if (!cmd_ok){
		msg_sup.causa=0x43; //command response not ok
	}else{
		msg_sup.causa=0x3; //command response not ok
	}
	msg_sup.tipo=45; //IHM accepts all types
	msg_sup.taminfo=sizeof(digital_seq);
	digital_value_gi.iq = 1;
	memcpy(msg_sup.info,(char *) &digital_value_gi, sizeof(digital_seq));
	return SendT(socketfd,(void *)&msg_sup, sizeof(t_msgsup), server_sock_addr);
}
/*********************************************************************************************************/
void print_time(FILE * log_file){
	time_t t = time(NULL);
	struct tm now = {0};

	Semaphore_wait(localtime_mutex);	
	localtime_r(&t, &now ); 
	Semaphore_post(localtime_mutex);	
	fprintf(log_file, "%04d/%02d/%02d %02d:%02d:%02d - ", now.tm_year+1900, now.tm_mon+1, now.tm_mday,now.tm_hour, now.tm_min,now.tm_sec);
}
