/************************************************************
proxy.c

A http caching web proxy handles HTTP/1.0 GET requests.

The proxy will start a thread for each client's request.
I implement the cache as a singly linked list that approximates
a least-recently-used (LRU) eviction policy.

For each request, the proxy will search the cache to see if there
is corresponding response cached. If find corresponding response in
cache, just use it for response, otherwise, connect to server and get
response,send back to client also save the response in cache when necessary


*************************************************************/
#include <stdio.h>
#include "csapp.h"
#include <stdbool.h>
#include "cache.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/*Length of different strings*/
#define len_of_HOST 4
#define len_of_User_Agent 10
#define len_of_Connection 10
#define len_of_Proxy_Connection 16


static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";


//****************global variables************

Cache_t * cache;
size_t total_cache_size;
sem_t read_lock,write_lock;
int readcnt;

//*************helper function**********************
void serve_client(int clientfd);
void sigint_handler(int sig);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);
int parse_request_uri(char * request_uri, char* host, char* port,
	char* query);
void *thread_for_client(void *vargp);
int modified_open_clientfd(char *hostname, char *port);
int read_request_line(rio_t * rio,
char* method, char* request_uri ,char * version);
int handle_request_headers(rio_t* rio_for_client,
	char* server_buf,char* host);
int handle_response_from_server
(int clientfd, rio_t* rio_for_server, char *request_uri);


/* $begin proxy main */
int main(int argc, char **argv) {

    Signal(SIGPIPE, SIG_IGN);
    Signal(SIGINT, sigint_handler);

    int listenfd, *clientfd;
    char hostname[MAXLINE], port[MAXLINE];

    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    int rc;
    Sem_init(&read_lock, 0, 1);
    Sem_init(&write_lock, 0, 1);

    cache = NULL;
    total_cache_size = 0;
    readcnt = 0;
    pthread_t tid;
    /* Check command line args */
    if (argc != 2) {
    	fprintf(stderr, "usage: %s <port>\n", argv[0]);
    	exit(1);
    }

    listenfd = Open_listenfd(argv[1]);

    while (1) {

    	clientlen = sizeof(clientaddr);
    	clientfd = malloc(sizeof(int));

        if(clientfd==NULL) { // handle the malloc error
            fprintf(stderr, "%s: %s\n", "malloc error", strerror(errno));
            continue;
        }

        *clientfd = accept(listenfd, (SA *)&clientaddr, &clientlen);

        if(*clientfd<0) { // handle the accept error
            fprintf(stderr, "accept error:%s\n",strerror(errno));
            continue;
        }

        rc = getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                        port, MAXLINE, 0);

        if(rc != 0) {  // handle getnameinfo error
            fprintf(stderr,"%s: %s\n","getnameinfo error",gai_strerror(rc));
            continue;
        }
            
        printf("Accepted connection from (%s, %s)\n", hostname, port);
            
    	rc = pthread_create(&tid, NULL, thread_for_client, clientfd);

        if(rc!=0) { // pthread create error
            fprintf(stderr,"%s: %s\n","pthread create error",strerror(rc));
        }
                                              
    }
}
/* $end proxy main */


/*
    sigint_handler: free the cache when receive SIGINT signal
*/
/* $begin sigint_handler */
void sigint_handler(int sig) {
    free_cache(cache);
    exit(0);
}
/* $end sigint_handler */



/*
    thread_for_client: thread function to serve the client's request.
    	The descriptor for communicate with client is passed by the vargp
    	pointer 

*/
/* $begin thread_for_client*/
void * thread_for_client(void *vargp) {   
    
    int rc = pthread_detach(pthread_self());
    int clientfd;
    if(rc != 0) {
        fprintf(stderr, "%s: %s\n","Pthread_detach error", strerror(rc));        
        clientfd = *((int *)vargp);
        Free(vargp);
        if (close(clientfd)<0) {
            fprintf(stderr,"%s: %s\n","close clientfd error",strerror(errno));
        }
        return NULL;
    }
   
    clientfd = *((int *)vargp); 
    Free(vargp);
    serve_client(clientfd);
    if (close(clientfd)<0) {
        fprintf(stderr, "%s: %s\n", "close clientfd error", strerror(errno));
    }

    printf("a service thread end\n");
    return NULL;
}
/* $end thread_for_client*/


/*
    read_request_line: read the request line,
    get the method, request uri and version field.
    return -1 when meet some error
    return 0 when success 

*/
/* $begin read_request_line*/
int read_request_line(rio_t * rio, char* method, char* request_uri ,
	char * version) {
    char buf[MAXLINE];

    
    int result=rio_readlineb(rio, buf, MAXLINE);
    if(result==-1) {
        return -1; // bad request line
    }

    if(result==0) {
        return -1; // bad request line
    }
    
    if((sscanf(buf, "%s %s %s", method, request_uri, version))!=3) {
        return -1; // bad request line
    }
    if(strlen(request_uri)>=MAXLINE||strlen(method)>=MAXLINE
    	||strlen(version)>=MAXLINE)
        return -1; // bad request line
    return 0;
}
/* $end read_request_line*/



/*
    handle_request_headers: read the request headers from client and modified 
    them according to the requirement in writeup, 
    save them in the buffer for server.

    return 0 when success
    return -1 when find some error
*/
/* $begin handle_request_headers*/
int handle_request_headers(rio_t* rio_for_client, 
	char* server_buf, char* host) {
    
    char buf[MAXLINE];
    char key[MAXLINE];
    char value[MAXLINE]; 
    if (rio_readlineb(rio_for_client, buf, MAXLINE) == -1) {      
        return -1;
    }

    // test if these headers appear
    bool has_proxy_connection=false;
    bool has_connection=false;
    bool has_user_agent=false;
    bool has_host=false;

    while(strcmp(buf, "\r\n")) {          
    	
        if(sscanf(buf,"%s %s",key,value)!=2) {             
             return -1;  // read header error
        }
        if(!strncasecmp("Host",key,len_of_HOST)) {
            has_host=true;
            strcat(server_buf,buf);
        }
        else if(!strncasecmp("User-Agent",key,len_of_User_Agent)) {
            has_user_agent=true;
            strcat(server_buf,user_agent_hdr);
        }
        else if(!strncasecmp("Connection",key,len_of_Connection)) {
            has_connection=true;
            strcat(server_buf,connection_hdr);    
        }
        else if(!strncasecmp("Proxy-Connection",key,
        	len_of_Proxy_Connection)) {
            has_proxy_connection=true;
            strcat(server_buf,proxy_connection_hdr);       
        }
        else
            strcat(server_buf,buf);

        if(rio_readlineb(rio_for_client, buf, MAXLINE)==-1) {
            return -1; // read header error
        }
    }
    // add the required headers
    if(!has_host) {
        strcat(server_buf,"Host: ");
        strcat(server_buf,host);
        strcat(server_buf,"\r\n");
        
    }
    if(!has_user_agent)
        strcat(server_buf,user_agent_hdr);
    if(!has_connection)
        strcat(server_buf,connection_hdr);
    if(!has_proxy_connection)
        strcat(server_buf,proxy_connection_hdr);
    strcat(server_buf,buf);
    return 0;
}
/* $end handle_request_headers*/

/*
    handle_response_from_server: 
    get the response from server and send them to client.
    if the response's size is smaller than MAX_OBJECT_SIZE, put the response 
    into cache;
    return 0 when success finish
    return -1 when read from server error
    return -2 when write to client error

*/
/* $begin handle_response_from_server*/
int handle_response_from_server(int clientfd, rio_t* rio_for_server,
 char *request_uri) {
    int n;
    char buf[MAXLINE];
    char response_buf[MAX_OBJECT_SIZE]; 
    long response_size=0;
    n=rio_readlineb(rio_for_server, buf, MAXLINE);
    if(n==-1) {     
        return -1;
    }
    if(rio_writen(clientfd, buf, n)==-1) {
            
            return -2;
    }
     while(n != 0) {
     	// record the response
        if(response_size+n<MAX_OBJECT_SIZE) {
            memcpy((response_buf+response_size),buf,n);          
        }
        // update the response size
        response_size = response_size+n;

        n=rio_readlineb(rio_for_server, buf, MAXLINE);
        if(n==-1) {
            return -1;
        }
        if(rio_writen(clientfd, buf, n)==-1) {
    
            return -2;
        }
    }

    if(response_size<MAX_OBJECT_SIZE&&response_size>0) {
    	// put the response into cache when the size is suitable
        Cache_t* new_cache_block=
        construct_cache_block(request_uri,response_buf,response_size);

        P(&write_lock);
        while(total_cache_size+response_size>MAX_CACHE_SIZE) {
        	//evict to get enough pace
            if(evict_cache(&total_cache_size,&cache)==-1) {
            	fprintf(stderr, "cache evict error\n");
            	V(&write_lock);
            	return 0; // do not cache and return as normal
            }
        }

        add_to_cache(new_cache_block,&cache);
        total_cache_size=total_cache_size+response_size;
        V(&write_lock);
     }
     return 0;
}
/* $end handle_response_from_server*/


/*
  serve_client - handle one HTTP request/response transaction for client
   parse the request line and headers, If find corresponding response in
   cache, just use it for response, otherwise, connect to server and get
   response, also save the response in cache when necessary.
 
 */

/* $begin serve_client */
void serve_client(int clientfd) {
   
    char server_buf[MAXLINE],method[MAXLINE],
    request_uri[MAXLINE],version[MAXLINE],query[MAXLINE];
 
    rio_t rio_for_client,rio_for_server;
    char host[MAXLINE],port[MAXLINE];

   
    Rio_readinitb(&rio_for_client, clientfd);

    
     /* Read request line*/
    if(read_request_line(&rio_for_client,method,request_uri,version)==-1) {
       
        fprintf(stderr,"bad request line\n");    
        return;
    }


    if (strcasecmp(method, "GET")) {               
        
        clienterror(clientfd, method, "501", "Not Implemented",
                    "proxy does not implement this method");   
        return;
    }

    P(&read_lock);
    readcnt++;
    printf("Receive request uri = %s\n",request_uri);
    if (readcnt == 1)
        P(&write_lock);
    V(&read_lock);
    // search if the request is cached
    Cache_t* hit_cache=find_in_cache(request_uri,cache);
    if(hit_cache) {
    	/*if hit*/
        printf("Cache Hit!!!!!!!\n");
        
        if(rio_writen(clientfd, hit_cache->response,
        	hit_cache->response_size)==-1) {
            fprintf(stderr, "write cached object to client error:%s\n"
            	,strerror(errno));
        }
        P(&read_lock);
        readcnt--;
        if(readcnt==0)
            V(&write_lock);
        V(&read_lock);
        // update the time stamp
        P(&write_lock);
        update_time_stamp(hit_cache,cache);
        V(&write_lock);
        return;
    }
    /*
		if miss
    */
    P(&read_lock);
    readcnt--;
    if(readcnt==0)
        V(&write_lock);
    V(&read_lock);
    // update the time stamp
    P(&write_lock);
    update_time_stamp(hit_cache,cache);
    V(&write_lock);

    printf("Cache Miss!!!!!!!\n");
    if(parse_request_uri(request_uri,host,port,query)==-1) {
        
        fprintf(stderr, "invalid request uri error = %s\n",strerror(errno));
        return;     
    }


    int serverfd;
    /* I modified the open_clientfd function, so it won't exit for 
       invalid host and port
    */
    serverfd = modified_open_clientfd(host, port);
    if(serverfd ==-1) {
        fprintf(stderr, "proxy cannot connect to server error:%s\n",
        	strerror(errno));
        return;
    }


    // handle the request headers
    sprintf(server_buf, "%s %s %s\r\n",method,query,"HTTP/1.0"); 
     
    if(handle_request_headers(&rio_for_client,server_buf,host)==-1) {
        fprintf(stderr, "proxy read headers error:%s\n",strerror(errno));    
        Close(serverfd);
        return;
    }
 
    if(rio_writen(serverfd,server_buf, strlen(server_buf))==-1) {
        fprintf(stderr, "proxy write to server error:%s\n",strerror(errno));
        Close(serverfd);
        return;
    }


    // get response from server
    Rio_readinitb(&rio_for_server, serverfd);
    int result;
    result=handle_response_from_server(clientfd,&rio_for_server,request_uri);
    if(result==-1) {
        fprintf(stderr, "proxy read from server error:%s\n",strerror(errno));
        Close(serverfd);
        return;
    }
    if(result==-2) { 
        fprintf(stderr, "write response object to client error:%s\n",
        	strerror(errno));
        Close(serverfd);
        return;
    }
    Close(serverfd);
    return;
}
/* $end serve_client */

/*
    parse_request_uri: get host, port and query from the given request uri,
    return -1 when the request uri is invalid
    return 0 when success
*/

/* $begin parse_request_uri */
int parse_request_uri( char * request_uri, char* host, char* port, 
	char* query) {
    
    // my proxy force the request uri begins with "http://"
    if(strncasecmp("http://",request_uri,strlen("http://"))) {    
       return -1;
    }
    char *host_start= (request_uri+strlen("http://"));
    char *host_end= strchr(host_start,'/');

    if(host_end==NULL) {       
        return -1;
    }
    int len_of_port=0;
    char * port_start=strchr(host_start,':');
    int len_of_host=0;
    char* query_start=NULL;

    if(port_start==NULL||(port_start>=host_end)) {
    	/*	handle the case when no port number found
			or the ':' appears afer the '/'(in the query part),
			use default 80 port number
    	*/
        
        strcpy(port,"80");
        len_of_host=(int)(host_end-host_start);
        query_start=host_start+len_of_host;
    }
    else {
    	/*
			handle the normal case when host and port number
			are provided
    	*/
        port_start=port_start+1;
        len_of_port=(int)(host_end-port_start);

        if(len_of_port>=MAXLINE)
            return -1;
        strncpy(port,port_start, len_of_port);
        port[len_of_port]='\0';
        len_of_host=(int)(host_end-host_start-len_of_port-1);
        query_start=port_start+len_of_port;
    }
     
    if(len_of_host>=MAXLINE) 
        return -1;
    strncpy(host,host_start, len_of_host);
    host[len_of_host]='\0';

    printf("parse host=%s\n",host);
    printf("parse port=%s\n",port);

    //get the rest as query
    strcpy(query,query_start);
    printf("parse query=%s\n",query);

    return 0;
}
/* $end parse_request_uri */



/*  (modified from tiny.c by handling the return value of 'rio_writen' 
	function)
    clienterror: - returns an error message to the client
*/
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum, 
         char *shortmsg, char *longmsg){

    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The kaimin's proxy </em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    // I just use the rio bucause 
    if(rio_writen(fd, buf, strlen(buf))==-1) 
        return;
    sprintf(buf, "Content-type: text/html\r\n");
    if(rio_writen(fd, buf, strlen(buf))==-1)
        return;
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    if(rio_writen(fd, buf, strlen(buf))==-1)
        return;
    if(rio_writen(fd, body, strlen(body))==-1)
        return;
}
/* $end clienterror */


/*
    modified_open_clientfd (modified from CSAPP.C):
        Open connection to server at <hostname, port> and
        return a socket descriptor ready for reading and writing. This
        function is reentrant and protocol-independent.
 
    On error, returns -1 and sets errno.
*/

/* $begin modified_open_clientfd*/
int modified_open_clientfd(char *hostname, char *port) {
    int clientfd;
    struct addrinfo hints, *listp, *p;

    /* Get a list of potential server addresses */
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_socktype = SOCK_STREAM;  /* Open a connection */
    hints.ai_flags = AI_NUMERICSERV;  /* ... using a numeric port arg. */
    hints.ai_flags |= AI_ADDRCONFIG;  /* Recommended for connections */



/***I modified here because the Getaddrinfo exit when some error occurs***/
   // Getaddrinfo(hostname, port, &hints, &listp);
    int rc;

    if ((rc = getaddrinfo(hostname, port, &hints, &listp)) != 0) {
        fprintf(stderr, "%s: %s\n", "Getaddrinfo error", gai_strerror(rc));
        return -1; 
    }
    /* Walk the list for one that we can successfully connect to */
    for (p = listp; p; p = p->ai_next) {
        /* Create a socket descriptor */
        if ((clientfd = socket(p->ai_family, p->ai_socktype,
         p->ai_protocol)) < 0) 
            continue; /* Socket failed, try the next */

        /* Connect to the server */
        if (connect(clientfd, p->ai_addr, p->ai_addrlen) != -1) 
            break; /* Success */
        Close(clientfd); /* Connect failed, try another */ 
    } 

    /* Clean up */
    Freeaddrinfo(listp);
    if (!p) /* All connects failed */
        return -1;
    else    /* The last connect succeeded */
        return clientfd;
}
/* $end modified_open_clientfd*/
