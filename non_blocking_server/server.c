#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>

#include <time.h>

#include <stdlib.h>

/*
./server -p 31415
./server -p 27182
*/

#define NEGO_SIZE 8
#define MAXLINE 128
#define MSG_MAX 10*1024*1024 //10M

#define PROTOCOL_NON 0
#define PROTOCOL_1 1
#define PROTOCOL_2 2

#define PHASE_1 1
#define PHASE_2 2

#define LAST_CHAR_NONE 0
#define LAST_CHAR_STORED 1

#define RECV_START 0
#define RECV_CONTINUE 4
#define RECV_BACKSLASH 5
#define RECV_END 6

struct negotiation_struct
{
	char op;
	char proto;
	unsigned short checksum;
	unsigned int trans_id;
};

struct client_state
{
	int phase;
	int nego_size;
	char *nego_buf;

	int protocol_type;

	char *send_buf;
	int recv_state;
	int index;
	int string_length;
	int has_last_char;
	char last_char;
};

struct client_state *client_info[FD_SETSIZE];

unsigned short calc_checksum(unsigned short *num, int len);
int client_init_phase1(struct client_state *client_info);
int client_init_phase2(struct client_state *client_info);
void client_exit(struct client_state *client_info);
int phase1(int sockfd, struct client_state *client_info);
int phase2(int sockfd, struct client_state *client_info);
void write_char(struct client_state *client_info, char c);
int truncate_proto1(char *recv_buf, int recv_len, struct client_state *client_info);
int truncate_proto2(char *recv_buf, int recv_len, struct client_state *client_info);
int send_truncated_msg(int sockfd, int protocol_type, char *send_buf, int index);

int main(int argc, char **argv)
{
	struct sockaddr_in addr;
	int sockfd, cli_sockfd;
	int clilen;
	struct sockaddr_storage tstr;
	int opt;
	int portnum;

	fd_set master;
	fd_set read_fds;
	int fdmax;

	while( (opt = getopt(argc, argv, "p:")) != -1)
	{
		switch(opt)
		{
			case 'p':
				portnum = atoi(optarg);
				break;
			default:
				return 1;
		}
	}

	if( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		perror("socket error");
		return 1;
	}

	printf("=================Server Start\n");
	
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr=INADDR_ANY;
	addr.sin_port=htons(portnum);
	
	if( bind (sockfd,(struct sockaddr *)&addr,sizeof(addr))<0)
	{
		printf("Bind Error\n");
		return 1;
	}
	
	if( listen(sockfd,5)<0 )
	{
		printf("Listen Error");
		return 1;
	}

	FD_ZERO(&master);
	FD_ZERO(&read_fds);
	FD_SET(sockfd, &master);
	fdmax = sockfd;

	int phase2_state, i;

	while(1){
		read_fds = master;
		if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1){
			printf("Error: select\n");
			close(sockfd);
			exit(0);
		}
		for(i = 0;i <= fdmax;i++){
			if(FD_ISSET(i, &read_fds)){
				if (i == sockfd){
					clilen=sizeof(tstr);
					cli_sockfd = accept(sockfd,(struct sockaddr *)&tstr,(unsigned int*)&clilen);
					if(cli_sockfd < 0)
						printf("Error: accept\n");
					else{
						FD_SET(cli_sockfd, &master);
						if (cli_sockfd > fdmax)
							fdmax = cli_sockfd;
						client_info[cli_sockfd] = (struct client_state *)malloc(sizeof(struct client_state));
						if (client_info[cli_sockfd] == NULL) {
							printf("Error: malloc client_info\n");
							close(cli_sockfd);
							FD_CLR(cli_sockfd, &master);
							exit(0);
						}
						client_init_phase1(client_info[cli_sockfd]);
						printf("New connection: socket(%d)\n", cli_sockfd);
					}
				}
				else{
					if(client_info[i]->phase == PHASE_1){
						if(phase1(i, client_info[i]) < 0){
							printf("Error: phase 1 (socket %d)\n", i);
							client_exit(client_info[i]);
							close(i);
							FD_CLR(i, &master);
						}
					}
					else if(client_info[i]->phase == PHASE_2){
						phase2_state = phase2(i, client_info[i]);
						if(phase2_state <= 0){
							printf("Socket closed (socket %d)\n",i);
							client_exit(client_info[i]);
							close(i);
							FD_CLR(i, &master);
						}
					}
				}
			}
		}
	}
}

unsigned short
calc_checksum(unsigned short *num, int len)
{
	unsigned long sum=0;
	while(len--)
	{
        sum += *num;
        num++;
	}
 
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
 
    return (unsigned short)(((~sum)<<32)>>32);
}

int
client_init_phase1(struct client_state *client_info){
	client_info->phase = PHASE_1;
	client_info->nego_size = 0;
	client_info->nego_buf = (char *) malloc(sizeof(char) * NEGO_SIZE);
	if(client_info->nego_buf == NULL)
		return -1;
	memset(client_info->nego_buf, 0, NEGO_SIZE);
	return 1;
}

int
client_init_phase2(struct client_state *client_info){
	client_info->phase = PHASE_2;
	client_info->send_buf = (char *) malloc(sizeof(char)*MSG_MAX);
	if(client_info->send_buf == NULL)
		return -1;
	client_info->recv_state = RECV_START;
	client_info->index = 0;
	client_info->has_last_char = LAST_CHAR_NONE;
	memset(client_info->send_buf, 0, MSG_MAX);
	return 1;
}
void
client_exit(struct client_state *client_info){
	free(client_info->nego_buf);
	free(client_info->send_buf);
	free(client_info);
}

int
phase1(int sockfd, struct client_state *client_info){

	int recv_size = recv(sockfd, client_info->nego_buf + client_info->nego_size,8,0);

	if(recv_size + client_info->nego_size < 8){
		client_info->nego_size = recv_size + client_info->nego_size;
		return 1;
	}

	struct negotiation_struct nego_send, nego_recv;
	memcpy(&nego_recv, client_info->nego_buf, 8);

	if ((int)nego_recv.op != 0)
		return -1;
	if (calc_checksum((unsigned short *)&nego_recv,4) != 0)
		return -1;

	memset(&nego_send,0,8);
	nego_send.op = (char)1;
	nego_send.proto = nego_recv.proto;
	if(nego_send.proto == PROTOCOL_NON){
		srand(time(NULL));
		nego_send.proto = rand()%2 + 1;
	}
	nego_send.trans_id = nego_recv.trans_id;
	nego_send.checksum = calc_checksum((unsigned short *)&nego_send,4);

	int send_size = send(sockfd,&nego_send,8,0);

	client_init_phase2(client_info);
	client_info->protocol_type = nego_send.proto;

	return 1;
}

int
phase2(int sockfd, struct client_state *client_info){
	int protocol_type = client_info->protocol_type;
	int read_size;
	int recv_size = 0;
	char recv_buf[MAXLINE];
	char *trans_buf;

	// char *print_buf;

	if((recv_size = recv(sockfd, recv_buf, MAXLINE, 0)) <= 0){
		printf("Client disconnected\n");
		return 0;
	}

	while(recv_size > 0){
		if(protocol_type == PROTOCOL_1){
			read_size = truncate_proto1(recv_buf, recv_size, client_info);
		}
		else if(protocol_type == PROTOCOL_2){
			read_size = truncate_proto2(recv_buf, recv_size, client_info);
		}
		else{
			printf("Error: unavailable protocol %d\n", protocol_type);
			return -1;
		}
		if(read_size < 0){
			printf("Error: protocol violation\n");
			return -1;
		}
		if(client_info->index > 0)
			client_info->has_last_char = LAST_CHAR_STORED;

		if(client_info->recv_state == RECV_END){

			// print_buf = malloc(sizeof(char)*client_info->index);
			// memset(print_buf,0,client_info->index);
			// memcpy(print_buf,client_info->send_buf,client_info->index);
			// printf("length: %d\n", client_info->index);
			// printf("msg: %s\n\n",print_buf);
			// free(print_buf);

			if(send_truncated_msg(sockfd, protocol_type, client_info->send_buf, client_info->index) < 0){
				printf("Error: send\n");
				return -1;
			}
			memset(client_info->send_buf, 0, MSG_MAX);
			client_info->index = 0;
			client_info->recv_state = RECV_START;
		}

		if(recv_size == read_size)
			return 1;

		recv_size -= read_size;

		trans_buf = malloc(sizeof(char)*recv_size);
		memcpy(trans_buf,recv_buf+read_size,recv_size);
		memcpy(recv_buf,trans_buf,recv_size);
		free(trans_buf);
	}

	return 1;
}

void
write_char(struct client_state *client_info, char c){
	int i = client_info->index;
	client_info->send_buf[i] = c;
	client_info->index = i+1;
}

int
truncate_proto1(char *recv_buf, int recv_len, struct client_state *client_info)
{
	int count = 0;
	char c;

	if(recv_len == 0)
		return 0;
	if(client_info->has_last_char == 0){
		c = recv_buf[count];
		count++;
		if(c == '\\'){
			client_info->recv_state = RECV_BACKSLASH;
			client_info->last_char = '0';
		}
		else{
			write_char(client_info,c);
			client_info->last_char = c;
		}
	}
	if(client_info->recv_state == RECV_BACKSLASH){
		c = recv_buf[count];
		count++;
		if((c == '\\')&&(client_info->last_char != c)){
			write_char(client_info,c);
			write_char(client_info,c);
			client_info->last_char = '\\';
		}
		else if(c == '0'){
			write_char(client_info,'\\');
			write_char(client_info,c);
			client_info->recv_state = RECV_END;
			return count;
		}
		else if(c != '\\')
			return -1;
	}
	while(count < recv_len){
		c = recv_buf[count];
		count++;
		if(c == '\\'){
			if(count == recv_len){
				client_info->recv_state = RECV_BACKSLASH;
				return count;
			}
			c = recv_buf[count];
			count++;
			if((c == '\\')&&(client_info->last_char != c)){
				write_char(client_info,c);
				write_char(client_info,c);
				client_info->last_char = '\\';
			}
			else if(c == '0'){
				write_char(client_info,'\\');
				write_char(client_info,c);
				client_info->recv_state = RECV_END;
				return count;
			}
			else if(c != '\\')
				return -1;
		}
		else if(client_info->last_char != c){
			write_char(client_info,c);
			client_info->last_char = c;
		}
	}
	client_info->recv_state = RECV_CONTINUE;
	return count;
}

int
truncate_proto2(char *recv_buf, int recv_len, struct client_state *client_info)
{
	int count = 0;
	char c;

	if (recv_len == 0)
		return 0;
	if(client_info->recv_state < RECV_CONTINUE){
		if(client_info->recv_state + recv_len < 4){
			memcpy((char *)&client_info->string_length + client_info->recv_state, recv_buf, recv_len);
			client_info->recv_state += recv_len;
			return recv_len;
		}
		memcpy((char *)&client_info->string_length + client_info->recv_state, recv_buf, 4 - client_info->recv_state);
		client_info->string_length = ntohl(client_info->string_length) + (4 - client_info->recv_state);
		count += (4 - client_info->recv_state);
		client_info->recv_state = RECV_CONTINUE;

		//10M length limitation
		if(client_info->string_length > MSG_MAX)
			return -1;
	}

	if((client_info->has_last_char == 0) && (count<recv_len) && (count<client_info->string_length)){
		c = recv_buf[count];
		count++;
		write_char(client_info,c);
		client_info->last_char = c;
	}

	while((count < recv_len) && (count < client_info->string_length)){
		c = recv_buf[count];
		count++;
		if(client_info->last_char != c){
			write_char(client_info,c);
			client_info->last_char = c;
		}
	}

	if(count == client_info->string_length)
		client_info->recv_state = RECV_END;

	client_info->string_length = client_info->string_length - count;

	return count;
}

int
send_truncated_msg(int sockfd, int protocol_type, char *send_buf, int index){
	int send_count = 0;
	int send_len;

	if(protocol_type == PROTOCOL_2){
		send_len = htonl(index);
		if(send(sockfd, &send_len, 4, 0) < 0)
			return -1;
	}

	send_len = index;

	while(send_len > MAXLINE){
		if(send(sockfd,send_buf + send_count,MAXLINE,0) < 0)
			return -1;
		send_count += MAXLINE;
		send_len -= MAXLINE;
	}
	if(send(sockfd, send_buf + send_count, send_len, 0) < 0)
		return -1;
	return 1;
}
