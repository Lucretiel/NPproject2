/*
 * server_listener.h
 *
 *  Created on: Mar 4, 2014
 *      Author: nathan
 *
 *  The core server functions. Listens on the port, accepts connections, asks
 *  the manager to spawn workers. It also manages signals, and handles launching
 *  the print and manager threads.
 */

#pragma once

#include <stdint.h>

int serve_forever(uint16_t port);
