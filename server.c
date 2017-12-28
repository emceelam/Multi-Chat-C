#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>

#define PORT 4021
#define MAX_FD FD_SETSIZE
#define CHAT_SIZE 50
#define TIME_OUT 10
  // 10 second time out

/*
 * prototypes
 * ------------
 */
void sig_alarm_handler (int signum);
void start_end_alarm (int *sock_expiry);

/*
 * globals
 * --------
 */
int readsock;
sigjmp_buf env;
fd_set saveset;
int sock_expiry[MAX_FD];


/*
 * code begins
 * -------------
 */
int main (void) {
  // SIGALRM setup
  struct sigaction sa;
  sigfillset (&sa.sa_mask);
  sa.sa_handler = sig_alarm_handler;
  sa.sa_flags = 0;
  if (sigaction(SIGALRM, &sa, NULL) == -1) {
    perror ("sigaction() fails: ");
  }

  // init block_set signal mask: blocks some signals
  sigset_t block_set;
  sigemptyset(&block_set);
  sigaddset(&block_set, SIGALRM);
  sigprocmask (SIG_BLOCK, &block_set, NULL);
    // Defer signal handling to select() block
    // Signal blocking is actually signal deferrment.

  // init empty_set signal mask: blocks nothing
  sigset_t empty_set;
  sigemptyset(&empty_set);

  // set up listenfd socket
  int listenfd;
  struct sockaddr_in sin;
  sin.sin_family = AF_INET;
  sin.sin_port = htons(4021);
  sin.sin_addr.s_addr = htonl(INADDR_ANY);
  listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0) {
    perror ("socket() failed");
    exit(1);
  }
  printf ("listenfd sock %d\n", listenfd);

  // avoid address in use error
  // linux doesn't close out sockets immediately on program termination
  // we want to reuse if we detect if same socket has not closed.
  int on = 1;
  if (setsockopt (listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
    perror("setsockopt() fails");
    exit(1);
  }
  
  if (bind(listenfd, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
    perror ("Binding error in server!");
    exit(1);
  }
  int backlog = 5;   // max length of queue of pending connections.
  if (listen(listenfd, backlog) < 0) {
    perror ("Listen error in server!");
    exit(1);
  }

  fd_set readset;
  char chat[CHAT_SIZE];
  int connfd;
  int return_val;
  FD_ZERO(&saveset);
  FD_SET(listenfd, &saveset);
  memset (sock_expiry, 0, sizeof(sock_expiry));
  while(1) {
    memcpy (&readset, &saveset, sizeof(fd_set));
    return_val =
      pselect (
        MAX_FD,      // nfds, number of file descriptors
        &readset,    // readfds
        NULL,        // writefds
        NULL,        // exceptfds, almost never used
        NULL,        // struct timeval *timeout, NULL for always block
        &empty_set   // sigmask, allow signal processing during select
      );
    if (return_val == -1 && errno == EINTR) {
      // signal handling has interrupted pselect()
      continue;
    }

    // incoming connection
    int now = time(NULL);
    if (FD_ISSET(listenfd, &readset)) {
      connfd = accept(listenfd, NULL, NULL);
        // will not block because accept has data
      printf ("connection from socket %d\n", connfd);
      FD_SET(connfd, &saveset);
      sock_expiry[connfd] = now + TIME_OUT;

      /*
       * set no linger
       * onoff=1 and ling=0:
       *   The connection is aborted on close,
       *   and all data queued for sending is discarded.
       */
      struct linger stay;
      stay.l_onoff  = 1;
      stay.l_linger = 0;
      setsockopt(connfd, SOL_SOCKET, SO_LINGER, &stay, sizeof(struct linger));

      // set connfd to be non-blocking socket
      int val;
      val = fcntl(connfd, F_GETFL, 0);
      if (val < 0) {
        perror ("fcntl(F_GETFL) failed");
      }
      val = fcntl(connfd, F_SETFL, val | O_NONBLOCK);
      if (val < 0) {
        perror ("fcntl(F_SETFL) failed");
      }
    }
    
    // existing connections
    for (readsock = listenfd +1; readsock < MAX_FD ; readsock++) {
      
      if (!FD_ISSET(readsock, &readset)) {
        continue;
      }

      memset (chat, 0, CHAT_SIZE);
      int nread = read (readsock, chat, CHAT_SIZE);
      if (nread < 0) {
        perror ("read() failed");
        exit(1);
      }
      chat[strcspn(chat, "\r\n")] = '\0';  // chomp
      sock_expiry[readsock] = now + TIME_OUT;

      if ( nread == 0  // disconnected socket
        || strcmp(chat, "quit") == 0) // user quitting
      {
        FD_CLR (readsock, &saveset);
        close(readsock);
        sock_expiry[readsock] = 0;
        printf ("closing %d\n", readsock);
        continue;
      }
      if (strlen(chat) == 0) {
        continue;  // nothing to do
      }
      printf ("read %d: '%s'\n", readsock, chat);
      
      // write chat to all other clients
      int writesock;
      struct timeval tv;
      fd_set writeset;
      memcpy (&writeset, &saveset, sizeof(fd_set));
      tv.tv_sec  = 0;
      tv.tv_usec = 0;
      select (MAX_FD, NULL, &writeset, NULL, &tv);
        // which sockets can we write to
      for (writesock = listenfd +1; writesock < MAX_FD; writesock++) {
        if (  writesock == readsock   // don't echo to originator 
          || !FD_ISSET(writesock, &writeset))
        {
          continue;
        }

        printf ("write %d: %s\n", writesock, chat);
        if (write (writesock, chat, CHAT_SIZE) < 0) {
          perror ("write() failed");
          exit(1);
        }
      }

    }//for loop for existing connections

    start_end_alarm(sock_expiry);

  }//while(1)

  return 0;
}

void sig_alarm_handler (int signum) {
  start_end_alarm (sock_expiry);
}

// start alarms and close timed-out sockets
void start_end_alarm (int *sock_expiry) {
  int now = time(NULL);
  int min_expiry = 0;
  for (int fd=0;fd<MAX_FD;fd++) {
    int expiry = sock_expiry[fd];
    if (expiry == 0) {
      continue;
    }
    // printf ("sig_alarm_handler: %d <= %d\n", expiry, now);
    if (expiry <= now) {
      printf ("time out socket %d\n",fd);

      sock_expiry[fd] = 0;
      FD_CLR (fd, &saveset);
      close (fd);
      continue;
    }
    if (min_expiry == 0) {
      min_expiry = expiry;
      // printf ("sig_handler: min_expiry = %d\n", min_expiry);
      continue;
    }
    if (expiry < min_expiry) {
      min_expiry = expiry;
      // printf ("sig_handler: min_expiry = %d\n", min_expiry);
    }
  }

  if (min_expiry) {
    int future = min_expiry - now;
    future = future < 1 ? 1 : future;
    // printf ("sig_handler alarm(%d)\n", future);
    alarm (future);
  }
}



