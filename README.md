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

Implementation notes
--------------------
- Signal handling hasn't been implemented yet.
- Persistent HTTP support has been half-implemented. For now, though, it closes
the connections after a single connection
- Chunked encoding is implemented
- Large parts of this were coded in Monster-Fueled all-nighters, so I apologize
if any of the commenting contains profanity or is incomprehensible. I did my
best to edit it, but I may have missed something.
- The GitHub repo for this project is available at
https://github.com/Lucretiel/NPproject2
