# NAME

multi_chat - A socket-based multi-client chat

# SYNOPSIS

open terminal

    make all
    ./server

from other terminal

    telnet 127.0.0.1 4021

from yet another terminal

    telnet 127.0.0.1 4021

start typing text in your telnet sessions

# DESCRIPTION

This is a socket-based multi-client chat that echoes one user's chat
to other user's chat telnet sessions.

# FEATURES

* Multiple telnet chat sessions
    * select()
    * non-blocking read/write sockets
* Time out inactive sockets
    * signal handling (SIGALRM)
    * sigprocmask()
    * sigaction()

# AUTHOR

Lambert Lum
![email address](http://sjsutech.com/small_email.png)

# COPYRIGHT AND LICENSE

This software is copyright (c) 2017 by Lambert Lum

This is free software; you can redistribute it and/or modify it under the same terms as the Perl 5 programming language system itself.
