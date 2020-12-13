

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

int QueueLength = 5;
int masterSocket;
pthread_mutex_t mutex;

// Processes time request
void processTimeRequest( int socket );

void processRequestThread(int slaveSocket);

void poolOfSlaves(int masterSocket);

void set_signal_handler();

extern "C" void sig_handler( int sig );

int main( int argc, char ** argv )
{
  // Print usage if not enough arguments
  if ( argc < 2 ) {
    fprintf( stderr, "no enough args" );
    exit( -1 );
  }

  //signal handler
  set_signal_handler();
  
  // Get the port from the arguments
  int port;
  if (strchr(argv[1], '-') != NULL)
  {
    //flag specified
    if (argc == 3)
    {
      port = atoi( argv[2] );
    }
    else {
      port = atoi( "11111" );//default port
    }
  } else {
    //no flag
    port = atoi( argv[1] );
  }
  
  // Set the IP address and port for this server
  struct sockaddr_in serverIPAddress; 
  memset( &serverIPAddress, 0, sizeof(serverIPAddress) );
  serverIPAddress.sin_family = AF_INET;//required setting
  serverIPAddress.sin_addr.s_addr = INADDR_ANY;//system chooses interface
  serverIPAddress.sin_port = htons((u_short) port);//initialize with port
  
  // Allocate a socket
  masterSocket =  socket(PF_INET, SOCK_STREAM, 0);
  if ( masterSocket < 0) {
    perror("socket");
    exit( -1 );
  }

  // Set socket options to reuse port. Otherwise we will
  // have to wait about 2 minutes before reusing the same port number
  int optval = 1; 
  int err = setsockopt(masterSocket, SOL_SOCKET, SO_REUSEADDR, 
		       (char *) &optval, sizeof( int ) );
   
  // Bind the socket to the IP address and port
  int error = bind( masterSocket,
		    (struct sockaddr *)&serverIPAddress,
		    sizeof(serverIPAddress) );
  if ( error ) {
    perror("bind");
    exit( -1 );
  }
  
  // Put socket in listening mode and set the 
  // size of the queue of unprocessed connections(client receive error mess if queue is full)
  error = listen( masterSocket, QueueLength);
  if ( error ) {
    perror("listen");
    exit( -1 );
  }

  //concurrency mode/process request
  if (!strcmp(argv[1], "-p"))
  {
    //fprintf(stderr, "\nPoolofthreads mode triggered\n");
    // pool of threads
    pthread_attr_t attr1;
    pthread_t thread[QueueLength];
    //initilaize mutext lock
    pthread_mutex_init(&mutex,NULL);
    pthread_attr_init( &attr1 );
    pthread_attr_setscope(&attr1, PTHREAD_SCOPE_SYSTEM);
    for (int i = 0; i < QueueLength; i++)
    {
      pthread_create(&thread[i], &attr1, (void * (*)(void *))poolOfSlaves, (void *)(unsigned long long)masterSocket);
    }
    //UNSURE: wait for all threads to end
    pthread_join(thread[0], NULL);//UNSURE
    /*for (int j = 0; j < 4; j++)
    {
      pthread_join(thread[j], NULL);
    }*/
  }

  while ( 1 ) {
    // Accept incoming connections
    struct sockaddr_in clientIPAddress;
    int alen = sizeof( clientIPAddress );
    int slaveSocket = accept( masterSocket, (struct sockaddr *)&clientIPAddress, (socklen_t*)&alen);
    if ( slaveSocket < 0 ) {
      perror( "accept" );
      exit( -1 );
    }

    //process request
    if (!strcmp(argv[1], "-f"))
    {
      if (slaveSocket == -1 && errno == EINTR)
      {
        continue;
      }
      // fork
      int ret = fork();
      int status;
      if (ret == 0)
      {
        // Process request.
        processTimeRequest( slaveSocket );
        close(slaveSocket);//close in child
        exit(EXIT_SUCCESS); 
      }
      close( slaveSocket );//close in parent
      //UNSURE: zombies
    }
    else if (!strcmp(argv[1], "-t"))
    {
      // new thread
      pthread_attr_t attr2;
      pthread_t thread1;
      pthread_attr_init(&attr2);
      pthread_attr_setdetachstate(&attr2, PTHREAD_CREATE_DETACHED);//detached state: reuse thread id when done
      pthread_create(&thread1, &attr2, (void * (*)(void *))processRequestThread, (void *)(unsigned long long)slaveSocket);
    }
    else 
    {
      // Process request.
      processTimeRequest( slaveSocket );
      close( slaveSocket );
    }

  }
}

void set_signal_handler() {
  struct sigaction sa;
  sa.sa_handler = sig_handler;
  sigemptyset(&sa.sa_mask);  //no signals can be received during handling a signal(?)
  sa.sa_flags = SA_RESTART;  //restart a sys call if it is interrupted
  //install new action for ctrl-c
  if( sigaction(SIGINT, &sa, NULL) ){
    perror("sigaction");
    exit(2);
  }

  struct sigaction sa_zombie;
  sa_zombie.sa_handler = sig_handler;
  sigemptyset(&sa_zombie.sa_mask);  
  sa_zombie.sa_flags = SA_RESTART; 
  //install new action for zombie elimination
  if( sigaction(SIGCHLD, &sa_zombie, NULL) ){
    perror("sigaction");
    exit(2);
  }
}

extern "C" void sig_handler( int sig )
{
  if (sig == 2)
  {
    //ctrl+c
    //close master socket
    //fprintf(stderr, "mastersocket: %d\n", masterSocket);
    close(masterSocket);
    exit(0);
  }
  if (sig == 17)
  {
    while(waitpid(-1, NULL, WNOHANG) > 0);;
  }
}

void processRequestThread(int slaveSocket) {
  processTimeRequest( slaveSocket );
  close( slaveSocket );
  return;
}

void poolOfSlaves(int masterSocket) {
  while (1)
  {
    //fprintf(stderr, "\nin poolOfSlaves\n");
    // Accept incoming connections
    struct sockaddr_in clientIPAddress;
    int alen = sizeof( clientIPAddress );
    pthread_mutex_lock(&mutex);
    int slaveSocket = accept( masterSocket, (struct sockaddr *)&clientIPAddress, (socklen_t*)&alen);//UNSURE: mutext lock
    pthread_mutex_unlock(&mutex);
    if ( slaveSocket < 0 ) {
      perror( "accept" );
      exit( -1 );
    }

    processTimeRequest( slaveSocket );
    close(slaveSocket);
  }
}

void send_authen(int fd) {
  const char* response = "HTTP/1.1 401 Unauthorized\r\n"
                         "WWW-Authenticate: Basic realm=\"myhttpd-cs252\"\r\n\r\n";
                         int n;
  if ((n = write(fd, response, strlen(response))) != strlen(response))
  {
    perror("write authen");
  }
}

void send_access_denied(int fd) {
  const char* response = "HTTP/1.1 200 Document follows\r\n"
                         "Server: cs.data.purdue.edu\r\n"
                         "Content-type: text/html\r\n\r\n";
  const char* error_mes = "<H1> Access Denied</H1>";
  write(fd, response, strlen(response));
  write(fd, error_mes, strlen(error_mes));
  return;
}

void send_header(int fd, char* contentType, char* otherInfo)
{

  char response[10000] = {0};
  const char* prefix = "HTTP/1.1 200 Document follows\r\n"
                       "Server: cs.data.purdue.edu\r\n"
                       "Content-type: ";
  strcat(response, prefix);
  strcat(response, contentType);
  strcat(response, "\r\n");
  if (otherInfo != NULL) //unsure
  {
    strcat(response, otherInfo);
    strcat(response, "\r\n");
  }
  strcat(response, "\r\n");
  fprintf(stderr, "\nheader: %s\n", response);
  int n;
  if ((n = write(fd, response, strlen(response))) != strlen(response))
  {
    perror("write header");
  }
  
  return;
}

void send_404notfound(int fd)
{
  const char* response = "HTTP/1.1 404 File Not Found\r\n"
                         "Server: cs.data.purdue.edu\r\n"
                         "Content-type: text/html\r\n\r\n";
  const char* error_mes = "Could not find the specified URL." 
                          "The server returned an error.";
  write(fd, response, strlen(response));
  write(fd, error_mes, strlen(error_mes));
  return;
}

bool StartsWith(const char *a, const char *b)
{
    //if string a starts with string b return true
   if(strncmp(a, b, strlen(b)) == 0) return 1;
   return 0;
}

void processTimeRequest( int fd )
{
  // Buffer used to store the name received from the client
  const int MaxInput = 10000;
  char input[ MaxInput + 1 ];
  int inputLength = 0;
  int n;

  // Currently character read
  unsigned char newChar;

  //
  // The client should send <name><cr><lf>
  // Read the name of the client character by character until a
  // <CR><LF> is found.
  //

  unsigned char lastChar[3];
  lastChar[0] = 0;
  lastChar[1] = 0;
  lastChar[2] = 0;
  while ( inputLength < MaxInput &&
	  ( n = read( fd, &newChar, sizeof(newChar) ) ) > 0 ) {

    if ( lastChar[0] == '\r' && lastChar[1] == '\n' && lastChar[2] == '\r' && newChar == '\n' ) {
      // Discard previous <CR> from name
      inputLength--;
      break;
    }

    input[ inputLength ] = newChar;
    inputLength++;

    //not finish
    lastChar[0] = lastChar[1];
    lastChar[1] = lastChar[2];
    lastChar[2] = newChar; //to accpet newChar
  }

  // Add null character at the end of the string
  input[ inputLength ] = 0;

  printf( "%s\n", input );

  char request[10000]; //max request header is 10000
  char* pos_slash = strchr(input, '/');
  char* pos_nextspace = strchr(pos_slash, ' ');
  int len = (int)(pos_nextspace - pos_slash);
  strncpy(request, pos_slash, len);
  request[len] = '\0';//null-terminated

  fprintf(stderr, "request: %s\n", request);

  // favicon.ico
  if (!strcmp(request, "/favicon.ico"))
  {
    return;
  }
  
  // authentication
  char* pos_authen;
  if ((pos_authen = strstr(input, "Authorization")) == NULL) //UNSURE
  {
    send_authen(fd);
  } else {
    // check user/password
    const char* userandpw = "eHUxMjEwOjc2ODI=";
    char input_userandpw[10000] = {0};
    char* pos_basic = strstr(pos_authen, "Basic");
    char* pos_userandpw = strchr(pos_basic, ' ') + 1;
    int len_userandpw =(int) (strchr(pos_userandpw, '\r') - pos_userandpw);
    strncpy(input_userandpw, pos_userandpw, len_userandpw); 

    //fprintf(stderr, "input_userandpw: %s\n", input_userandpw);
    if (strcmp(userandpw, input_userandpw))
    {
      // wrong user or pw
      send_access_denied(fd);
      return;
    }
    
  }
  
  // determine doc path
  char path[256] = {0};
  getcwd(path, 256);
  fprintf(stderr, "cwd path: %s\n", path);
  if (StartsWith(request, "/icons"))
  {
      strcat(path, "/http-root-dir/");
      strncat(path, request, strlen(request));//UNSURE
  }
  else if (StartsWith(request, "/htdocs"))
  {
      strcat(path, "/http-root-dir/");
      strncat(path, request, strlen(request));//UNSURE
  } 
  else if(!strcmp(request, "/")) {
      strcat(path, "/http-root-dir/htdocs/index.html");
  } 
  else {
      strcat(path, "/http-root-dir/htdocs");
      strncat(path, request, strlen(request));//UNSURE
  }
  fprintf(stderr, "absulute path: %s\n", path);
  
  // check if path contains ".."
  if (strstr(path, "..") != NULL)
  {
    send_404notfound(fd);
    return;
  }

  //determine content type
  char suffix[10000];
  char contentType[10000];
  char* pos_dot = strchr(path, '.');
  if (pos_dot != NULL)
  {
    //file
    strcpy(suffix, pos_dot);
    if ((!strcmp(suffix, ".html")) || (!strcmp(suffix, ".html/")))
    {
      strcpy(contentType, "text/html");
    }
    else if ((!strcmp(suffix, ".gif")) || (!strcmp(suffix, ".gif/")))
    {
      strcpy(contentType, "image/gif");
    } 
    else {
      strcpy(contentType, "text/plain");
    }
  } else {
    // directory
    strcpy(contentType, "text/plain");
  }
  
  // open files
  int file_fd;
  file_fd = open((const char*)path, O_RDONLY);
  if (file_fd == -1) //UNSURE
  {
    // file not found
    send_404notfound(fd);
    return;
  }
  fprintf(stderr, "file_fd: %d\n", file_fd);
  
  // send header
  send_header(fd, contentType, NULL);//UNSURE: otherinfo

  int m;
  unsigned char char_to_write;
  // send file data
  while ( ( m = read( file_fd, &char_to_write, sizeof(char_to_write) ) ) > 0 ) {
    //fprintf(stderr, "char_to_write: %c\n", char_to_write);
    if (write(fd, &char_to_write, sizeof(char_to_write)) != m)
    {
      perror("write");
    }
  }

  close(file_fd);
  
  return;
}

