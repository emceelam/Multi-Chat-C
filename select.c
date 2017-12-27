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

#define PORT 4021
#define BUFSIZE 50

int readsock;
sigjmp_buf env;
fd_set saveset;


int main (void) {
  // init block_set signal mask
  sigset_t block_set;
  sigemptyset(&block_set);
  sigdelset(&block_set, SIGALRM);
  sigprocmask (SIG_BLOCK, &block_set, NULL);
    // Defer signal processing to select() block
    // Signal blocking is actually signal deferrment.

  // init empty_set signal mask
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
  FD_ZERO(&saveset);
  FD_SET(passive, &saveset);
  while(1) {
    memcpy (&readset, &saveset, sizeof(fd_set));
    pselect (
      FD_SETSIZE,  // nfds, number of file descriptors
      &readset,    // readfds
      NULL,        // writefds
      NULL,        // exceptfds, almost never used
      NULL,        // struct timeval *timeout, NULL for always block
      &empty_set   // sigmask, allow signal processing during select
    );
    if (FD_ISSET(passive, &readset)) {
      active = accept(passive, NULL, NULL);
        // will not block because accept has data
      FD_SET(active, &saveset);
      printf ("adding socket %d\n", active);

      /*
       * set no linger
       * onoff=1 and ling=0:
       *   The connection is aborted on close,
       *   and all data queued for sending is discarded.
       * 
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

      if ( nread == 0  // disconnected socket
        || strcmp(string, "quit") == 0) // user quitting
      {
        FD_CLR (readsock, &saveset);
        close(readsock);
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
    }
  }
 
  return 0;
}