////////////////////////////////////////////////////////////////////////////////////////////////
// File Name     : proxy_cache.c
// Date          : 2022/6/6
// OS            : Ubuntu 16.04 LTS 64bits
// Author        : cho su hoon
// Student ID    : 2019202102
// -------------------------------------------------------------------------------------------
// Title         : System Programming Assignment #3-2 (proxy server)
// Description   : Thread-based proxy server implementation
////////////////////////////////////////////////////////////////////////////////////////////////

// sha1_hash
////////////////////////////////////////////////////////////////////////////////////////////////
// Input         : input_url (char*)
// Output        : hashed_url (char*)
// Purpose       : Generate a SHA-1 hash of the given URL
////////////////////////////////////////////////////////////////////////////////////////////////

// getHomeDir
////////////////////////////////////////////////////////////////////////////////////////////////
// Input         : (empty char *)
// Output        : home path
// Purpose       : Retrieve the home directory path
////////////////////////////////////////////////////////////////////////////////////////////////

// misshit
////////////////////////////////////////////////////////////////////////////////////////////////
// Input         : char* dir2, char* file
// Output        : int
// Purpose       : Check if the requested file is a cache hit or miss
////////////////////////////////////////////////////////////////////////////////////////////////

// sig_child
////////////////////////////////////////////////////////////////////////////////////////////////
// Input         : void
// Output        : void
// Purpose       : Handle zombie process termination and notify child process information
////////////////////////////////////////////////////////////////////////////////////////////////

// sig_alarm
////////////////////////////////////////////////////////////////////////////////////////////////
// Input         : int
// Output        : void
// Purpose       : Handle the alarm signal, used for timing purposes
////////////////////////////////////////////////////////////////////////////////////////////////

// sig_int
////////////////////////////////////////////////////////////////////////////////////////////////
// Input         : void
// Output        : void
// Purpose       : Handle the interrupt signal (Ctrl+C), write to log file
////////////////////////////////////////////////////////////////////////////////////////////////

// p
////////////////////////////////////////////////////////////////////////////////////////////////
// Input         : int
// Output        : void
// Purpose       : Acquire a semaphore
////////////////////////////////////////////////////////////////////////////////////////////////

// v
////////////////////////////////////////////////////////////////////////////////////////////////
// Input         : int
// Output        : void
// Purpose       : Release a semaphore
////////////////////////////////////////////////////////////////////////////////////////////////

// thread_MISS
////////////////////////////////////////////////////////////////////////////////////////////////
// Input         : char*
// Output        : void*
// Purpose       : Thread function for handling cache misses
////////////////////////////////////////////////////////////////////////////////////////////////

// thread_HIT
////////////////////////////////////////////////////////////////////////////////////////////////
// Input         : struct MultipleArg
// Output        : void*
// Purpose       : Thread function for handling cache hits
////////////////////////////////////////////////////////////////////////////////////////////////



#include <stdio.h>
#include <string.h>
#include <openssl/sha.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <dirent.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <pthread.h>

#define BUFFSIZE	1024
#define PORTNO		39999

int misshit(char* dir2, char* file); //miss or hit
char*getHomeDir(char *home);
char*sha1_hash(char *input_url, char*hashed_url);
static void sig_child(); //sig child handler
static void sig_alarm();//sig alarm handler
static void sig_int(); //sig int handler
char *getIPAddr(char *addr); //get IP addr
void v(int semid); //p function for semaphore
void p(int semid); //v function for semaphore
void *thread_MISS(void* url ); //thread function for MISS
void *thread_HIT(void* multiple_arg); //thread function for HIT

int miss=0;//for count miss
int hit =0;//for count hit
time_t now,end,t_url; //program start time, program terminate time, input url time
int sub_pro_count; //sub process counting
FILE *fp; //logfile pointer
struct tm *ltp; //for localtime()

struct MultipleArg //struct multiple arg for thread_HIT function
{
	char* buf1;
 	char* buf2;
	char* buf3;
};

int main()
{
	char hashed[100];
	char home1[100];
	char home2[100];
	char *cachedir;
	char hashed3[100];
	char elsehashed[100];
	char dir1[100];
	char dir2[100];
	char file[100];
	char *logfile;
	int status;
	pid_t pid;
	int semid; //semaphore id 
	union semun{ //for using in semctl() function
		int val;
		struct semid_ds *buf;
		unsigned short int *array;
	}arg;
	if((semid = semget((key_t)PORTNO,1, IPC_CREAT|0666))==-1){
		perror("semget failed");
		exit(1);
	}
	arg.val = 1;
	if((semctl(semid, 0 ,SETVAL,arg))==-1){
		perror("sectil failed");
		exit(1);
	}
	getHomeDir(home1); //get home path
	getHomeDir(home2); //get home path
	umask(000); //for permission 777
	cachedir = strcat(home1,"/cache/"); //get cache directory path
	logfile = strcat(home2, "/logfile/"); //get logfile directory path
	mkdir(cachedir,0777); //make cache directory
	mkdir(logfile,0777); //make logfile directory
	strcat(logfile, "/logfile.txt"); //get logfile txt file path
	fp =fopen(logfile , "a");  //open logfile.txt
	//struct tm *ltp; //for localtime()
	struct sockaddr_in server_addr, client_addr;
	int socket_fd, client_fd;
	int len, len_out;
	int state;
	if((socket_fd = socket(PF_INET, SOCK_STREAM, 0)) < 0) //socket()
	{
		printf("Server : Can't open stream socket\n"); //if socket failed
		return 0;
	}
	bzero((char*)&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(PORTNO);
	int opt=1;
	setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt , sizeof(opt)); //protect bind error
	if(bind(socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr))<0)//bind()
	{
		printf("Server : can't bind local address\n"); //if bind failed
		close(socket_fd);
		return 0;
	}
	listen(socket_fd, 5); //listen()
	signal(SIGCHLD,(void*)sig_child); //sig_child	
	signal(SIGALRM,(void*)sig_alarm); //sig_alarm
	signal(SIGINT, (void*)sig_int); //sig_int
	time(&now); //program start time
	while(1){
		struct in_addr inet_client_address;
		char buf[BUFFSIZE];
		char response_header[BUFFSIZE]={0,};
		char response_message[BUFFSIZE]={0,};
		char tmp[BUFFSIZE]={0,};
		char method[20]={0,};
		char url[BUFFSIZE]={0,};
		char* tok=NULL;
		len = sizeof(client_addr);
		client_fd = accept(socket_fd, (struct sockaddr*)&client_addr, &len); //accept()
		if(client_fd<0) //if accept failed
		{
			printf("Server : accept failed %d \n", getpid());
			close(socket_fd);
			return 0;
		}
		inet_client_address.s_addr =client_addr.sin_addr.s_addr;
		//printf("[%s : %d] Client was connected \n" ,inet_ntoa(inet_client_address), client_addr.sin_port);
		pid = fork();
		sub_pro_count++;
		if(pid<0) //make child process
			fprintf(fp, "fork error\n");

		else if(pid==0){//child
			//alarm(10); //alarm 10 seconds
			if(read(client_fd, buf, BUFFSIZE)>0){
			strcpy(tmp, buf); //input_url copy to tmp
			//puts("======================================");
			//printf("Request from [%s : %d]\n",inet_ntoa(inet_client_address),client_addr.sin_port);
			//puts(buf);
			//puts("======================================\n");
			tok = strtok(tmp, " ");
			strcpy(method, tok); //copy to method
			if(strcmp(method,"GET")==0){
				tok =strtok(NULL," ");
				
				strcpy(url, tok); //copy to url
				
			}
			/* for get host name */
			tok = strtok(NULL, "\n");
			tok = strtok(NULL, " ");
			char host[255];
			strcpy(host, tok);
			if(strcmp(host , "Host:")==0){
				tok = strtok(NULL,"\n");
			}
			tok[strlen(tok)-1] = '\0';
			/* for get host ip */
			char* IPAddr;
			IPAddr = getIPAddr(tok);	
			/* for get url */
			sscanf(url, "http://%s/",url);		
			int size =strlen(url);
			if(url[size-1]=='/'){
				url[size-1]='\0';
			}
			time(&t_url); //input url time
			ltp = localtime(&t_url);//local time 
			sha1_hash(url, hashed); //get hashed_url	
			strncpy(dir1,hashed,3); //get 3letters
			dir1[3] = '\0';
			strcpy(dir2,cachedir);
			strcat(dir2,dir1);	
				for(int i=3; i<strlen(hashed)+1 ;i++){ //get other letters
					elsehashed[i-3]=hashed[i];
				}


			strcpy(file, dir2);
			strcat(file, "/");
			strcat(file,elsehashed);
			p(semid);// p function
				if(misshit(dir2,file)){//if miss
					DIR * pDir;
					mkdir(dir2,0777); //creat 3 letters directory
					pDir=opendir(dir2); //open 3 letters directory
					int fd;
					fd = open(file, O_WRONLY | O_CREAT | O_EXCL , 0644); //make other letters file 
					closedir(pDir);	//close directory
					void* tret;
					pthread_t tid; //thread id variable
					int err = pthread_create(&tid, NULL, thread_MISS,(void*) url); //thread create when MISS
					if(err!=0){
						printf("phtread_create() error.\n");
						return 0;
					}
					sprintf(response_message,//save response message
					"<h1>MISS</h1><br>"
					"%s:%d<br>"
					"%s<br>"
					"kw2019202102<br>"
					, inet_ntoa(inet_client_address), client_addr.sin_port,url);
					sprintf(response_header,//save response header
					"HTTP/1.0 200 OK\r\n"
					"Server:2018 simple web server\r\n"
					"Request Metohd: GET\r\n"
					"Host url : %s\r\n"
					"Content-length:%lu\r\n"
					"Content-type:text/html\r\n\r\n",tok,strlen(response_message));
					write(fd, response_header,strlen(response_header)); //write to file
					pthread_join(tid, &tret);
				}

				else	{//if HIT
					struct MultipleArg *multiple_arg; 
					multiple_arg = (struct MultipleArg*)malloc(sizeof(struct MultipleArg));
					multiple_arg -> buf1 = dir1;
					multiple_arg -> buf2 = elsehashed;
					multiple_arg -> buf3 = url;
					void *tret;
					pthread_t tid; //thread id variable
					int err = pthread_create(&tid, NULL, thread_HIT, (void*)multiple_arg); //thread create when HIT
					if(err!=0){
						printf("phtread_create() error.\n");
						return 0;
					}
					sprintf(response_message,//save response message
					"<h1>HIT</h1><br>"
					"%s:%d<br>"
					"%s<br>"
					"kw2019202102<br>"
					, inet_ntoa(inet_client_address), client_addr.sin_port,url);
					sprintf(response_header, //save response header
					"HTTP/1.0 200 OK\r\n"
					"Server:2018 simple web server\r\n"
					"Content-length:%lu\r\n"
					"Content-type:text/html\r\n\r\n",strlen(response_message));

					pthread_join(tid, &tret); //wait thread exit
		    			}	
			write(client_fd, response_header, strlen(response_header)); //write reponse header to browser
			write(client_fd, response_message, strlen(response_message));//write reponse message to browser
			//alarm(0); //if response, disable alarm		
			//printf("[%s :%d] Client was discoonected\n", inet_ntoa(inet_client_address), client_addr.sin_port);	
			}//if read
			v(semid); //v function
			close(client_fd);//child close client_fd
			exit(0); //child process terminate
		}//child
	       close(client_fd); //parent close client_fd
		}//while
		if((semctl(semid, 0 ,IPC_RMID, arg))==-1){ //delete semaphore
			perror("semctl failed");
			exit(1);
		}
		fclose(fp);//close logfile
		close(socket_fd); //close socket
		return 0;
	}//main

int misshit(char *dir2, char* file) {
    struct dirent *pFile;
    DIR *pDir;
    pDir = opendir(dir2);
    if (pDir != NULL) {
        hit++;
        closedir(pDir);
        return 0;
    } else {
        miss++;
        return 1;
    }

    while ((pFile = readdir(pDir)) != NULL) {
        if (strcmp(pFile->d_name, file) == 0) {
            hit++;
            closedir(pDir);
            return 0;
        } else {
            miss++;
            closedir(pDir);
            return 1;
        }
    }
    closedir(pDir);
    return 1;
}

char* sha1_hash(char * input_url, char *hashed_url) {
    unsigned char hashed_160bits[20];
    char hashed_hex[41];
    int i;

    SHA1((unsigned char*)input_url, strlen(input_url), hashed_160bits);
    for (i = 0; i < sizeof(hashed_160bits); i++) {
        sprintf(hashed_hex + i * 2, "%02x", hashed_160bits[i]);
    }
    strcpy(hashed_url, hashed_hex);

    return hashed_url;
}

char *getHomeDir(char * home) {
    struct passwd *usr_info = getpwuid(getuid());
    strcpy(home, usr_info->pw_dir);
    return home;
}

static void sig_child() {
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        sub_pro_count--;
    }
}

static void sig_int() {
    time(&end);
    fprintf(fp, "**SERVER** [Terminated] run time : %.0lf sec. #sub process:%d\n", difftime(end, now), sub_pro_count);
    exit(0);
}

static void sig_alarm() {
    printf("========= No Response =========\n");
    kill(getpid(), SIGKILL);
}

char *getIPAddr(char *addr) {
    struct hostent* hent;
    char* haddr;
    if ((hent = (struct hostent*)gethostbyname(addr)) != NULL) {
        haddr = inet_ntoa(*((struct in_addr*)hent->h_addr_list[0]));
    }
    return haddr;
}

void p(int semid) {
    printf("*PID# %d is waiting for the semaphore.\n ", getpid());
    struct sembuf pbuf = {0, -1, SEM_UNDO};
    if (semop(semid, &pbuf, 1) == -1) {
        perror(" p : semop Failed");
        exit(1);
    }
    printf("*PID# %d is in the critical zone.\n", getpid());
}

void v(int semid) {
    struct sembuf vbuf = {0, 1, SEM_UNDO};
    if (semop(semid, &vbuf, 1) == -1) {
        perror(" v : semop failed");
        exit(1);
    }
    printf("*PID# %d exited in the critical zone.\n", getpid());
}

void *thread_MISS(void* url) {
    printf("*PID# %d create the *TID %ld.\n ", getpid(), pthread_self());
    fprintf(fp, "[MISS]%s-[%04d/%02d/%02d,%02d:%02d:%02d]\n", (char *)url, ltp->tm_year + 1900, ltp->tm_mon + 1, ltp->tm_mday, ltp->tm_hour, ltp->tm_min, ltp->tm_sec);
    printf("*TID# %ld exited the critical section.\n", pthread_self());
    return NULL;
}

void *thread_HIT(void* multiple_arg) {
    struct MultipleArg *my_multiple_arg = (struct MultipleArg*)multiple_arg;
    printf("*PID# %d create the *TID %ld.\n ", getpid(), pthread_self());
    fprintf(fp, "[HIT]%s/%s-[%04d/%02d/%02d,%02d:%02d:%02d]\n", my_multiple_arg->buf1, my_multiple_arg->buf2, ltp->tm_year + 1900, ltp->tm_mon + 1, ltp->tm_mday, ltp->tm_hour, ltp->tm_min, ltp->tm_sec);
    fprintf(fp, "[HIT]%s\n", my_multiple_arg->buf3);
    printf("*TID# %ld exited the critical section.\n", pthread_self());
    return NULL;
}










