NPproject2
==========

An HTTP proxy server in C that supports filtering based on server domain name.
http://www.cs.rpi.edu/~goldsd/spring2014-csci4220-projects.php

Nathan West

File Summary
------------

- `config.h`: This file contains compile-time config options, such as maximum
buffer sizes and debug print options.
- `filters.*`: These files implement global filter storage and matching
- `print_thread.*`: These files implement a global print thread and message
queue, which is responsible for printing each received message to a line, one
at a time. It allows efficient, thread-safe printing.
- `server_listener.*`: This is the main server implementation. It listens on
a socket and spawns threads to handle incomming connections.
- `stat_tracking.*`: These files implement global, thead safe stat tracking.
For simplicity, it also keeps its own copies of the filters.
- `http_worker_thread.*`: These files define the HTTP worker threads, which are
spawned once per connection and are responsible for actually forwarding the
request and response.
- `http_manager_thread.*`: These files define the global HTTP manager thread,
which is responsible for spawning and cleaning up the individual workers. It is
primarily used to wait for all threads to finish during a clean exit.
- `http.h`: This is the primary HTTP API. It defines all the structures that
HTTP messages are parsed into, as well as functions to read, write, clear, and
manipulate these messages.
	- `http_clear.c`: This file implements cleaning the HTTP structures- free
	memory, zero relevant fields, etc.
	- `http_manip.c`: This file implements the various functions for
	manipulating HTTP messages. While the structure is designed to be used "as
	is" (ie, simply assign a string to the domain, etc), these functions provide
	convenience for things like setting Content-Length, searching for headers,
	and getting standard response phrases.
	- `http_write.c`: This file implements writing HTTP messages.
	- `http_read.c`: This file implements reading, parsing, and validating HTTP
	messages.
- `ReadableRegex/*`: This is a library I wrote during this project to simplify
the creating of regular expressions, using string literal concatenation
- `EasyString/*`: This is a library I wrote during this project to make strings
more manageable. It defines the String type, which is a string with ownership
semantics, and the StringRef type, which stores a char* and a length. The
library provides numerous utilities for manipulating strings, including copying,
moving, slicing, appending, and comparison.

Compile notes
-------------
- Please compile with `-std=gnu99` for the `getaddrinfo` function. I don't know
why this is needed, as `getaddrinfo` is standard POSIX, but it's needed.
- Compile with `-pthread`
- Make sure to compile the `*.c` files in the subdirectories.
- I've included the auto-generated makefiles produced by my IDE in the `Debug`
and `Release` directories. They work fine from the command line in Ubuntu 12.04
- Set `DEBUG_PRINT` to 1 in `config.h` to see extended debug output. This will
also show the cleanup actions taking place when you quit with `SIGUSR2`.

Implementation notes
--------------------

- Persistent HTTP support has been half-implemented. For now, though, it closes
the connections after a single connection.
- Large parts of this were coded in Monster-Fueled all-nighters, so I apologize
if any of the commenting contains profanity or is incomprehensible. I did my
best to edit it, but I may have missed something.
- The GitHub repo for this project is available at
https://github.com/Lucretiel/NPproject2
- Make sure to compile all of the .c files in the subdirectories, as well
- I'm 90% sure the Malformed Request line errors you see every so often are
HTTPS requests. Malformed Request Line basically means that the regex I use to
match the request line failed to match. Right now there's no easy way to test
this, because I don't preserve the original client request bytes anywhere.
Attempting a normal HTTPS request results in the same Malformed Request Line
error.
- Everyone spent the whole last 2 weeks telling me I was putting way too much
work into this, so I started cutting corners. This is why you get that
particular usage message.
- This code should be portable to any unix-style OS. It's all standard C, but
I used a few gcc `__attribute__` extensions. These extensions are supported by
clang 3.3+ and gcc 4.0+ (standard Ubuntu 12.04 uses gcc 4.6).

Test Notes
----------

- My most heavily tested sites were `www.example.com`, `www.reddit.com`,
and `wikipedia.org`. Testing was performed in firefox on Ubuntu 12.04 in a
VMware Fusion Virtual Machine. HTTPS doesn't work, but otherwise browsing was
very consistently normal, if a little slower. reddit in particular was a very
smooth experience-all thumbnails and other things load and work fine. Wikipedia
had more trouble, as I don't think that CSS was being delivered properly, but
the text itself loaded and browsed fine.
