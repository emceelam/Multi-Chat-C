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
void start_end_alarm (int *sock_expiry, fd_set *saveset);

/*
 * globals
 * --------
 */
fd_set saveset;
int sock_expiry[MAX_FD];


/*
 * code begins
 * -------------
 */
int main (void) {
  printf ("Multi client chat server, using telnet, programmed in C.\n");
  printf ("To connect:\n");
  printf ("    telnet 127.0.0.1 %d\n", PORT);
  printf ("\n");
  printf ("Everything typed by one chat user will be copied to other chat users.\n");
  printf ("Typing 'quit' on telnet sessions will disconnect.\n");

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
  sigaddset(&empty_set, SIGUSR2); // no we don't use SIGUSR2
    // pselect() needs a non-NULL signal set. Otherwise signal set is ignored.

  // set up listenfd socket
  int listenfd;
  listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0) {
    perror ("socket() failed");
    exit(1);
  }

  // SO_REUSEADDR
  //   l
  // Unix Network Programming, p103.
  //   A common error from bind is EADDRINUSE
  // Unix Network Programming, p203.
  //   SO_REUSEADDR socket option should always be used in the server
  //   before the call to bind
  // Unix Network Programming, p210
  //   All TCP servers should specify this socket option
  int on = 1;
  if (setsockopt (listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
    perror("setsockopt() fails");
    exit(1);
  }

  // bind
  struct sockaddr_in sin;
  sin.sin_family = AF_INET;
  sin.sin_port = htons(PORT);
  sin.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(listenfd, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
    perror ("Binding error in server!");
    exit(1);
  }

  // listen
  int backlog = 5;   // max length of queue of pending connections.
  if (listen(listenfd, backlog) < 0) {
    perror ("Listen error in server!");
    exit(1);
  }
  printf ("Listening on socket %d for client connections\n", listenfd);

  // main loop
  // block for incoming socket communications
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

      connfd = accept(
        listenfd,  // listenfd accepts connections
        NULL,      // client sockaddr information, NULL to ignore
        NULL       // length of client sockaddr, NULL to ignore
      );
        // will not block because accept has data
      printf ("Connection from socket %d\n", connfd);
      FD_SET(connfd, &saveset);
      sock_expiry[connfd] = now + TIME_OUT;

      /*
       * set no linger
       * onoff=1 and ling=0:
       *   The connection is aborted on close,
       *   and all data queued for sending is discarded.
       *
       *  struct linger {
       *       int l_linger;   // how many seconds to linger for
       *       int l_onoff;    // linger active
       *   };
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
    int readsock;
    for (readsock = listenfd +1; readsock < MAX_FD ; readsock++) {
      
      if (!FD_ISSET(readsock, &readset)) {
        continue;
      }

      memset (chat, 0, CHAT_SIZE);
      int nread = read (readsock, chat, CHAT_SIZE);
      if (nread < 0) {
        fprintf (stderr,
          "read() failed on sock %d: %s\n", readsock, strerror(errno));
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
        printf ("Quit from socket %d\n", readsock);
        continue;
      }
      if (strlen(chat) == 0) {
        continue;  // nothing to do
      }
      printf ("Read socket %d: '%s'\n", readsock, chat);
      
      // write chat to all other clients
      int writesock;
      int writecnt = 0;
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

        printf ("Write socket %d: %s\n", writesock, chat);
        writecnt++;
        if (write (writesock, chat, CHAT_SIZE) < 0) {
          fprintf (stderr,
            "write() failed on sock %d: %s\n", writesock, strerror(errno));
          exit(1);
        }
      }
      if (!writecnt) {
        printf ("No other telnet sessions. Message not copied: '%s'\n", chat);
      }

    }//for loop for existing connections

    start_end_alarm(sock_expiry, &saveset);

  }//while(1)

  return 0;
}

void sig_alarm_handler (int signum) {
  start_end_alarm (sock_expiry, &saveset);
}

// start alarms and close timed-out sockets
void start_end_alarm (int *sock_expiry, fd_set *saveset) {
  int now = time(NULL);
  int min_expiry = 0;
  for (int fd=0;fd<MAX_FD;fd++) {
    int expiry = sock_expiry[fd];
    if (expiry == 0) {
      continue;
    }
    // printf ("sig_alarm_handler: %d <= %d\n", expiry, now);
    if (expiry <= now) {
      printf ("Time out socket %d. Inactivity of %d sec. Closing socket.\n",
              fd, TIME_OUT);

      sock_expiry[fd] = 0;
      FD_CLR (fd, saveset);
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



