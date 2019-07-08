#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/select.h>
#include <errno.h>

void sig_alarm_handler (int signum);

int main () {
  // SIGALRM setup
  struct sigaction sa;
  sigfillset (&sa.sa_mask);
  sa.sa_handler = sig_alarm_handler;
  sa.sa_flags = 0;
  if (sigaction(SIGALRM, &sa, NULL) == -1) {
    perror ("sigaction() fails: ");
  }

  puts("setting a 1 second alarm amongst a 10 second sleep");
  alarm (1);
  sleep(10);

  // block sigalrm
  sigset_t block_set;
  sigemptyset(&block_set);
  sigaddset(&block_set, SIGALRM);
  sigprocmask (SIG_BLOCK, &block_set, NULL);
    // Defer signal handling to select() block
    // Signal blocking is actually signal deferrment.
  
  puts ("");
  puts ("Block SIGALRM. Setting 1 second alarm amongst a 10 second sleep");
  alarm (1);
  sleep(10);

  puts ("");
  puts ("Still block SIGALRM. Setting 1 second alarm amongst a select timeout of 10 second");

  alarm (1);  
  
  // init empty_set signal mask: blocks nothing
  sigset_t empty_set;
  sigemptyset(&empty_set);
  sigaddset(&empty_set, SIGUSR2); // no we don't use SIGUSR2
    // pselect() needs a non-NULL signal set.
    // Otherwise signal set is ignored.

  struct timespec tv;
  tv.tv_sec  = 10;    // sleep for 10 seconds
  tv.tv_nsec =  0;
  int return_val =
    pselect (
      0,          // nfds, number of file descriptors
      NULL,       // readfds
      NULL,       // writefds
      NULL,       // exceptfds, almost never used
      &tv,        // struct timeval *timeout, NULL for always block
      &empty_set  // sigmask, allow signal processing during select
    );
  if (return_val == -1 && errno == EINTR) {
    puts ("select() has been interrupted by a signal");
  }
}



void sig_alarm_handler (int signum) {
  puts ("sigalarm triggered");
}
