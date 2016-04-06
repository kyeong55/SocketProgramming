#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>

#include <unistd.h>
#include <getopt.h>

/*
./client -h 143.248.48.110 -p 31415 -m 1 < samples/sample2.txt > out.out
*/

#define MAXLINE 1024

#define TRANS_ID 12345

#define PROTOCOL_NON 0
#define PROTOCOL_1 1
#define PROTOCOL_2 2
#define PROTOCOL_DEBUG 2

#define STDIN_END 0
#define STDIN_CONTINUE 1

#define STDOUT_START 0
#define STDOUT_CONTINUE 4
#define STDOUT_BACKSLASH 5
#define STDOUT_END 6

void help(char *progname)
{
	printf("Usage : %s -h [ip] -p [port number] -m [protocol]\n", progname);
}

struct negotiation_struct
{
	char op;
	char proto;
	unsigned short checksum;
	unsigned int trans_id;
};

struct protocol2_struct
{
	int len;
	char buf[MAXLINE-4];
};

unsigned short calc_checksum(unsigned short *num, int len);
int phase1(int sockfd, int protocol_type);
int phase2(int sockfd, int protocol_type);
int read_proto1(char *buf, int *std_state);
int read_proto2(char *buf, int *std_state);
int write_proto1(char *buf, int len, int *stdout_state);
int write_proto2(char *buf, int len, int *stdout_state, int *remain);

int
main(int argc, char **argv)
{
	struct sockaddr_in addr;
	int sockfd;
	int opt;
	char ipaddr[36]={0x00,};
	int portnum;
	int protocol_type = PROTOCOL_NON;

	while( (opt = getopt(argc, argv, "h:p:m:")) != -1)
	{
		switch(opt)
		{
			case 'h':
				sprintf(ipaddr, "%s", optarg);
				break;

			case 'p':
				portnum = atoi(optarg);
				break;

			case 'm':
				protocol_type = atoi(optarg);
				break;

			default:
				help(argv[0]);
				return 1;
		}
	}

	if(ipaddr[0] == '\0')
	{
		printf ("ip address not setting\n");
		return 0;
	}

	if( (sockfd = socket(AF_INET,SOCK_STREAM,0)) < 0)
	{
		printf("Socket Error\n");
		return 0;
	}   
	
	addr.sin_family=AF_INET;
	addr.sin_port=htons(portnum);
	// addr.sin_addr.s_addr=inet_addr("127.0.0.1");
	addr.sin_addr.s_addr=inet_addr(ipaddr);
	
	if(connect(sockfd, (struct sockaddr *)&addr,sizeof(addr)) < 0)
	{
		printf("Connect Error\n");
		close(sockfd);
		return 0;
	}

	if((protocol_type = phase1(sockfd, protocol_type)) < 0){
		printf("Phase1 Error\n");
		close(sockfd);
		return 0;
	}
	if(phase2(sockfd, protocol_type) < 0){
		printf("Phase2 Error\n");
	}

	close(sockfd);
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
phase1(int sockfd, int protocol_type)
{	
	char recv_buf[8];
	struct negotiation_struct nego_send, nego_recv;

	memset(&nego_send,0,8);
	nego_send.op = (char)0;
	nego_send.proto = (char)protocol_type;
	nego_send.trans_id = TRANS_ID;
	nego_send.trans_id = htonl(nego_send.trans_id);
	nego_send.checksum = calc_checksum((unsigned short *)&nego_send,4);
	
	int send_size = send(sockfd,&nego_send,8,0);
	int recv_size = recv(sockfd,recv_buf,8,0);
	while (recv_size<8){
		recv_size += (recv(sockfd,recv_buf+recv_size, 8 - recv_size, 0));
	}
	memcpy(&nego_recv, recv_buf, 8);

	if ((int)nego_recv.op != 1)
		return -1;
	if (nego_send.trans_id != nego_recv.trans_id)
		return -1;
	if (calc_checksum((unsigned short *)&nego_recv,4) != 0)
		return -1;

	return (int)nego_recv.proto;
}

int
phase2(int sockfd, int protocol_type)
{
	int send_size, recv_size;
	char send_buf[MAXLINE];
	char recv_buf[MAXLINE];

	int stdin_len, stdout_len;
	int stdin_state;
	int stdout_state;
	int stdout_proto2_remain;

	do{
		if(protocol_type == PROTOCOL_1)
			stdin_len = read_proto1(send_buf, &stdin_state);
		else if(protocol_type == PROTOCOL_2)
			stdin_len = read_proto2(send_buf, &stdin_state);
		else
			return -1;
		// printf("%s",((struct protocol2_struct*)send_buf)->buf);
		
		send_size = send(sockfd,send_buf,stdin_len,0);

		stdout_state = STDOUT_START;
		do{
			recv_size = recv(sockfd,recv_buf,MAXLINE,0);
			if(recv_size <= 0)
				return recv_size;
			if(protocol_type == PROTOCOL_1)
				stdout_len = write_proto1(recv_buf,recv_size,&stdout_state);
			else if(protocol_type == PROTOCOL_2)
				stdout_len = write_proto2(recv_buf,recv_size,&stdout_state,&stdout_proto2_remain);
			else
				return -1;

			if (stdout_len < 0){
				printf("ERROR: cannot write files\n");
				return -1;
			}
		}while(stdout_state != STDOUT_END);
	}while(stdin_state == STDIN_CONTINUE);
	return 1;
}

int
read_proto1(char *buf, int *stdin_state){
	int count = 0;
	char c = getchar();

	*stdin_state = STDIN_END;

	while(!feof(stdin)){
		buf[count] = c;
		count++;
		if (c == '\\'){
			buf[count] = '\\';
			count++;
		}
		if(count >= MAXLINE - 3){
			*stdin_state = STDIN_CONTINUE;
			break;
		}
		c = getchar();
	}

	buf[count] = '\\';
	count++;
	buf[count] = '0';
	count++;
	return count;
}

int
read_proto2(char *buf, int *stdin_state){
	int count = 0;
	char c = getchar();
	char instant_buf[MAXLINE];
	struct protocol2_struct buf_struct;

	*stdin_state = STDIN_END;

	while(!feof(stdin)){
		instant_buf[count] = c;
		count++;
		if(count >= MAXLINE - 4){
			*stdin_state = STDIN_CONTINUE;
			break;
		}
		c = getchar();
	}

	buf_struct.len = htonl(count);
	memcpy(buf_struct.buf,instant_buf,count);

	count += 4;

	memcpy(buf,&buf_struct,count);
	return count;
}

int
write_proto1(char *buf, int len, int *stdout_state){
	int length = 0;
	int count = 0;
	char c;
	if (len <= 0)
		return len;
	if(*stdout_state == STDOUT_BACKSLASH){
		if(buf[count] == '\\')
			count++;
		else if(buf[count] == '0'){
			*stdout_state = STDOUT_END;
			return length;
		}
		else
			return -1;
		putchar('\\');
		length++;
	}
	while(count<len){
		c = buf[count];
		count++;
		if(c == '\\'){
			if(count == len){
				*stdout_state = STDOUT_BACKSLASH;
				return length;
			}
			else if(buf[count] == '\\')
				count++;
			else if(buf[count] == '0'){
				*stdout_state = STDOUT_END;
				return length;
			}
			else
				return -1;
		}
		putchar(c);
		length++;
	}
	*stdout_state = STDOUT_CONTINUE;
	return length;
}

int
write_proto2(char *buf, int len, int *stdout_state, int *remain){
	int count = 0;
	int trans;
	if (len <= 0){
		return len;
	}
	// printf("len: %d, (state %d)\n", len, *stdout_state);
	if(*stdout_state < STDOUT_CONTINUE){
		if(*stdout_state + len < 4){
			memcpy((char *)remain + *stdout_state, buf, len);
			*stdout_state += len;
			return len;
		}
		memcpy((char *)remain + *stdout_state, buf, 4 - *stdout_state);
		// printf("remain: %d\n", ntohl(*remain));
		*remain = ntohl(*remain) + (4 - *stdout_state);
		count += (4 - *stdout_state);
		*stdout_state = STDOUT_CONTINUE;
	}

	while((count < len) && (count < *remain)){
		putchar(buf[count]);
		count++;
	}

	if (count < *remain){
		trans = *remain - count;
		*remain = trans;
	}
	else if (count == *remain)
		*stdout_state = STDOUT_END;
	else
		return -1;

	return count;
}