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

#define MAXLINE 128
#define MSG_MAX 10*1024*1024 //10M

#define PROTOCOL_NON 0
#define PROTOCOL_1 1
#define PROTOCOL_2 2

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

unsigned short calc_checksum(unsigned short *num, int len);
int phase1(int sockfd);
int phase2(int sockfd, int protocol_type);
void write_char(char *buf, int *index, char c);
int truncate_proto1(char *recv_buf, char *send_buf, int recv_len, int *send_index, int *recv_state, int has_last_char, char *last_char);
int truncate_proto2(char *recv_buf, char *send_buf, int recv_len, int *send_index, int *recv_state, int *string_length, int has_last_char, char *last_char);
int send_truncated_msg(int sockfd, int protocol_type, char *send_buf, int index);

int main(int argc, char **argv)
{
	struct sockaddr_in addr;
	int sockfd, cli_sockfd;
	int clilen;
	int pid;
	char tstr[36];
	int opt;
	int portnum;

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
	
	while(1){
		clilen=sizeof(tstr);
		cli_sockfd = accept(sockfd,(struct sockaddr *)tstr,(unsigned int*)&clilen);
		if(cli_sockfd < 0) exit(0);
		pid = fork();
	  
		if(pid==0){	//	Child
			int protocol_type;
			if((protocol_type = phase1(cli_sockfd)) < 0){
				printf("Phase 1 error\n");
			}
			else if(phase2(cli_sockfd, protocol_type) < 0){
				// printf("Phase 2 error\n");
			}
			printf("Close socket.\n");
			close(cli_sockfd);
			break;
		}
		else{			//	Parent
			close(cli_sockfd);
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
phase1(int sockfd){
	char recv_buf[8];
	struct negotiation_struct nego_send, nego_recv;

	int recv_size = recv(sockfd,recv_buf,8,0);
	while (recv_size<8){
		recv_size += (recv(sockfd,recv_buf+recv_size, 8 - recv_size, 0));
	}
	memcpy(&nego_recv, recv_buf, 8);

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

	return (int)nego_send.proto;
}

int
phase2(int sockfd, int protocol_type){
	int read_size;
	int recv_size = 0;
	char recv_buf[MAXLINE];
	char *send_buf = (char *) malloc(sizeof(char)*MSG_MAX);
	char *trans_buf;

	int has_last_char = 0;
	char last_char;
	int index;
	int recv_state;
	int string_length;

	char *print_buf;

	while(1){
		memset(send_buf,0,MSG_MAX);
		index = 0;
		recv_state = RECV_START;
		do{
			if(recv_size < 0)
				return -1;
			else if(recv_size > 0){
				trans_buf = malloc(sizeof(char)*recv_size);
				memcpy(trans_buf,recv_buf+read_size,recv_size);
				memcpy(recv_buf,trans_buf,recv_size);
				free(trans_buf);
			}
			else if((recv_size = recv(sockfd,recv_buf,MAXLINE,0)) <= 0){
				//TODO: socket terminate
				printf("Socket terminates.\n");
				return -1;
			}

			if (protocol_type == PROTOCOL_1)
				read_size = truncate_proto1(recv_buf, send_buf, recv_size, 
					&index, &recv_state, has_last_char, &last_char);
			else if (protocol_type == PROTOCOL_2){
				read_size = truncate_proto2(recv_buf, send_buf, recv_size, 
					&index, &recv_state, &string_length, has_last_char, &last_char);
			}
			else
				return -1;
			if(read_size < 0){
				//TODO: protocol violation
				printf("Protocol violation\n");
				return -1;
			}

			if(index > 0)
				has_last_char = 1;

			recv_size -= read_size;

		}while(recv_state != RECV_END);

		print_buf = malloc(sizeof(char)*index);
		memset(print_buf,0,index);
		memcpy(print_buf,send_buf,index);
		printf("length: %d\n", index);
		printf("msg: %s\n\n",print_buf);
		free(print_buf);

		if(send_truncated_msg(sockfd, protocol_type, send_buf, index) < 0)
			return -1;
	}

	free(send_buf);
	return 1;
}

void
write_char(char *buf, int *index, char c){
	int i = *index;
	buf[i] = c;
	*index = i+1;
}

int
truncate_proto1(char *recv_buf, char *send_buf, int recv_len, int *send_index, 
	int *recv_state, int has_last_char, char *last_char)
{
	int count = 0;
	char c;

	if(recv_len == 0)
		return 0;
	if(has_last_char == 0){
		c = recv_buf[count];
		count++;
		if(c == '\\'){
			*recv_state = RECV_BACKSLASH;
			*last_char = '0';
		}
		else{
			write_char(send_buf,send_index,c);
			*last_char = c;
		}
	}
	if(*recv_state == RECV_BACKSLASH){
		c = recv_buf[count];
		count++;
		if((c == '\\')&&(*last_char != c)){
			write_char(send_buf,send_index,c);
			write_char(send_buf,send_index,c);
			*last_char = '\\';
		}
		else if(c == '0'){
			write_char(send_buf,send_index,'\\');
			write_char(send_buf,send_index,c);
			*recv_state = RECV_END;
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
				*recv_state = RECV_BACKSLASH;
				return count;
			}
			c = recv_buf[count];
			count++;
			if((c == '\\')&&(*last_char != c)){
				write_char(send_buf,send_index,c);
				write_char(send_buf,send_index,c);
				*last_char = '\\';
			}
			else if(c == '0'){
				write_char(send_buf,send_index,'\\');
				write_char(send_buf,send_index,c);
				*recv_state = RECV_END;
				return count;
			}
			else if(c != '\\')
				return -1;
		}
		else if(*last_char != c){
			write_char(send_buf,send_index,c);
			*last_char = c;
		}
	}
	*recv_state = RECV_CONTINUE;
	return count;
}

int
truncate_proto2(char *recv_buf, char *send_buf, int recv_len, int *send_index, 
	int *recv_state, int *string_length, int has_last_char, char *last_char)
{
	int count = 0;
	char c;

	if (recv_len == 0)
		return 0;
	if(*recv_state < RECV_CONTINUE){
		if(*recv_state + recv_len < 4){
			memcpy((char *)string_length + *recv_state, recv_buf, recv_len);
			*recv_state += recv_len;
			return recv_len;
		}
		memcpy((char *)string_length + *recv_state, recv_buf, 4 - *recv_state);
		*string_length = ntohl(*string_length) + (4 - *recv_state);
		count += (4 - *recv_state);
		*recv_state = RECV_CONTINUE;

		//10M length limitation
		if(*string_length > MSG_MAX)
			return -1;
	}

	if((has_last_char == 0) && (count<recv_len) && (count<*string_length)){
		c = recv_buf[count];
		count++;
		write_char(send_buf,send_index,c);
		*last_char = c;
	}

	while((count < recv_len) && (count < *string_length)){
		c = recv_buf[count];
		count++;
		if(*last_char != c){
			write_char(send_buf,send_index,c);
			*last_char = c;
		}
	}

	if(count == *string_length)
		*recv_state = RECV_END;

	*string_length -= count;

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
