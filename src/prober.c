/*  mcstatus-miner
 *  Copyright (C) 2013  Toon Schoenmakers
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "prober.h"

#include "debug.h"
#include "config.h"

#include <ctype.h>
#include <string.h>

#include <event2/bufferevent.h>

struct server_status {
  char* version;
  char* motd;
  unsigned short numplayers;
  unsigned short maxplayers;
};

void readcb(struct bufferevent* conn, void* arg);
void eventcb(struct bufferevent* conn, short event, void* arg);

void timer_callback(evutil_socket_t fd, short event, void* arg) {
  DEBUG(255, "timer_callback(%d, %d, %p);", fd, event, arg);
  struct server* server = arg;
  struct bufferevent* conn = bufferevent_socket_new(event_base, -1, BEV_OPT_CLOSE_ON_FREE);
  bufferevent_setcb(conn, readcb, NULL, eventcb, arg);
  bufferevent_socket_connect_hostname(conn, dns, AF_INET, server->hostname, server->port);
  bufferevent_enable(conn, EV_READ);
  static const char HEADER[] = { 0xFE, 0x01 };
  bufferevent_write(conn, HEADER, 2);
};

void readcb(struct bufferevent* conn, void* arg) {
  unsigned char buffer[1024];
  size_t len = bufferevent_read(conn, buffer, sizeof(buffer));
  if (buffer[0] == 0xFF && len > 16) {
    struct server_status status;
    char versionbuf[64];
    char *v = versionbuf;
    size_t i;
    for (i = 16; i < len; i++) {
      if (i % 2 == 0) {
        if (isascii(buffer[i])) {
          *(v++) = buffer[i];
          if (buffer[i] == 0x00)
            break;
        } else
          goto error;
      } else if (buffer[i] != 0x00)
        goto error;
    }
    status.version = versionbuf;
    DEBUG(255, "Version: %s", status.version);
    if (buffer[++i] != 0 && buffer[++i] != 0 && buffer[++i] != 0)
      goto error;
    char motdbuf[256];
    char *m = motdbuf;
    for (; i < len; i++) {
      if (i % 2 == 0) {
        if (isascii(buffer[i])) {
          *(m++) = buffer[i];
          if (buffer[i] == 0x00)
            break;
        } else
          goto error;
      } else if (buffer[i] != 0x00)
        goto error;
    }
    status.motd = motdbuf;
    DEBUG(255, "Motd: %s", status.motd);
  }
error:
  eventcb(conn, BEV_FINISHED, arg);
};

void eventcb(struct bufferevent* conn, short event, void* arg) {
  if (event & BEV_EVENT_CONNECTED)
    return;
  bufferevent_free(conn);
};