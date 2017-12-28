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
#define BUFSIZE 50
#define TIMEOUT 10
  // 10 second timeout

int readsock;
sigjmp_buf env;
fd_set saveset;
int sock_timeouts[FD_SETSIZE];


void sig_alarm_handler (int signum) {
  int now = time(NULL);
  int min_timeout = 0;
  for (int fd=0;fd<FD_SETSIZE;fd++) {
    int timeout = sock_timeouts[fd];
    if (timeout == 0) {
      continue;
    }
    printf ("sig_alarm_handler: %d <= %d\n", timeout, now);
    if (timeout <= now) {
      printf ("time out socket %d\n",fd);

      sock_timeouts[fd] = 0;
      FD_CLR (fd, &saveset);
      close (fd);
      continue;
    }
    if (min_timeout == 0) {
      min_timeout = timeout;
      printf ("sig_handler: min_timeout = %d\n", min_timeout);
      continue;
    }
    if (timeout < min_timeout) {
      min_timeout = timeout;
      printf ("sig_handler: min_timeout = %d\n", min_timeout);
    }
  }

  if (min_timeout) {
    int future = min_timeout - now;
    future = future < 1 ? 1 : future;
    printf ("sig_handler alarm(%d)\n", future);
    alarm (future);
  }
}

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

  // set up passive socket
  int passive;
  struct sockaddr_in sin;
  sin.sin_family = AF_INET;
  sin.sin_port = htons(4021);
  sin.sin_addr.s_addr = htonl(INADDR_ANY);
  passive = socket(AF_INET, SOCK_STREAM, 0);
  if (passive < 0) {
    perror ("socket() failed");
    exit(1);
  }
  printf ("passive sock %d\n", passive);

  // avoid address in use error
  // linux doesn't close out sockets immediately on program termination
  // we want to reuse if we detect if same socket has not closed.
  int on = 1;
  if (setsockopt (passive, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
    perror("setsockopt() fails");
    exit(1);
  }
  
  if (bind(passive, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
    perror ("Binding error in server!");
    exit(1);
  }
  int backlog = 5;   // max length of queue of pending connections.
  if (listen(passive, backlog) < 0) {
    perror ("Listen error in server!");
    exit(1);
  }


  fd_set readset;
  char string[BUFSIZE];  
  int active;
  int return_val;
  FD_ZERO(&saveset);
  FD_SET(passive, &saveset);
  memset (sock_timeouts, 0, sizeof(sock_timeouts));
  while(1) {
    memcpy (&readset, &saveset, sizeof(fd_set));
    return_val =
      pselect (
        FD_SETSIZE,  // nfds, number of file descriptors
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
    if (FD_ISSET(passive, &readset)) {
      active = accept(passive, NULL, NULL);
        // will not block because accept has data
      printf ("accept() returns %d\n", active);
      FD_SET(active, &saveset);

      /*
       * set no linger
       * onoff=1 and ling=0:
       *   The connection is aborted on close,
       *   and all data queued for sending is discarded.
       */
      struct linger stay;
      stay.l_onoff  = 1;
      stay.l_linger = 0;
      setsockopt(active, SOL_SOCKET, SO_LINGER, &stay, sizeof(struct linger));

      // set active to be non-blocking socket
      int val;
      val = fcntl(active, F_GETFL, 0);
      if (val < 0) {
        perror ("fcntl(F_GETFL) failed");
      }
      val = fcntl(active, F_SETFL, val | O_NONBLOCK);
      if (val < 0) {
        perror ("fcntl(F_SETFL) failed");
      }
    }
    
    int now = time(NULL);
    for (readsock = passive +1; readsock < FD_SETSIZE ; readsock++) {
      
      if (!FD_ISSET(readsock, &readset)) {
        continue;
      }

      memset (string, 0, BUFSIZE);
      int nread = read (readsock, string, BUFSIZE);
      if (nread < 0) {
        perror ("read() failed");
        exit(1);
      }
      string[strcspn(string, "\r\n")] = '\0';  // chomp
      sock_timeouts[readsock] = now + TIMEOUT;

      if ( nread == 0  // disconnected socket
        || strcmp(string, "quit") == 0) // user quitting
      {
        FD_CLR (readsock, &saveset);
        close(readsock);
        sock_timeouts[readsock] = 0;
        printf ("closing %d\n", readsock);
        continue;
      }
      if (strlen(string) == 0) {
        continue;  // nothing to do
      }
      printf ("read %d: '%s'\n", readsock, string);
      
      // write string to all other clients
      int writesock;
      struct timeval timeout;
      fd_set writeset;
      memcpy (&writeset, &saveset, sizeof(fd_set));
      timeout.tv_sec  = 0;
      timeout.tv_usec = 0;
      select (FD_SETSIZE, NULL, &writeset, NULL, &timeout);  
        // which sockets can we write to
      for (writesock = passive +1; writesock < FD_SETSIZE; writesock++) {
        if (  writesock == readsock   // don't echo to originator 
          || !FD_ISSET(writesock, &writeset))
        {
          continue;
        }

        printf ("write %d: %s\n", writesock, string);
        if (write (writesock, string, BUFSIZE) < 0) {
          perror ("write() failed");
          exit(1);
        }
      }

      // set up alarm(), which will cause SIGALRM
      int fd = 0;
      int min_timeout = 0;
      for (fd=0;fd<FD_SETSIZE;fd++) {
        int timeout = sock_timeouts[fd];
        if (timeout == 0) {
          continue;
        }
        printf ("sock_timeouts[%d]: %d\n", fd, timeout);
        if (min_timeout == 0) {
          min_timeout = timeout;
          continue;
        }
        if (timeout < min_timeout) {
          min_timeout = timeout;
        }
      }
      if (min_timeout) {
        int future = min_timeout - now;
        future = future < 1 ? 1 : future;
        printf ("alarm(%d)\n", future);
        alarm(future);
      }
    }
  }

  return 0;
}

