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

#include <time.h>
#include <ctype.h>
#include <string.h>

#include <event2/bufferevent.h>

struct server_status {
  char* version;
  char* motd;
  unsigned short numplayers;
  unsigned short maxplayers;
  struct server* server;
};

void readcb(struct bufferevent* conn, void* arg);
void eventcb(struct bufferevent* conn, short event, void* arg);

int print_status(struct server_status* status, char* format, char* buf, size_t buflen);

void timer_callback(evutil_socket_t fd, short event, void* arg) {
  DEBUG(255, "timer_callback(%d, %d, %p);", fd, event, arg);
  struct server* server = arg;
  struct bufferevent* conn = bufferevent_socket_new(event_base, -1, BEV_OPT_CLOSE_ON_FREE);
  static const struct timeval timeout = { 10, 0 };
  bufferevent_set_timeouts(conn, &timeout, &timeout);
  bufferevent_socket_connect_hostname(conn, dns, AF_INET, server->hostname, server->port);
  bufferevent_setcb(conn, readcb, NULL, eventcb, arg);
  bufferevent_enable(conn, EV_READ);
  if (server->long_request == 0) {
    static const unsigned char HEADER[] = { 0xFE, 0x01 };
    bufferevent_write(conn, HEADER, (sizeof(HEADER) / sizeof(unsigned char)));
  } else if (server->long_request == 1) {
    static const unsigned char HEADER[] = { 0xFE, 0x01, 0xFA, 0x00, 0x0B, 0x00, 0x4D, 0x00, 0x43, 0x00
                                          , 0x7C, 0x00, 0x50, 0x00, 0x69, 0x00, 0x6E, 0x00, 0x67, 0x00
                                          , 0x48, 0x00, 0x6F, 0x00, 0x73, 0x00, 0x74, 0x00 };
    bufferevent_write(conn, HEADER, (sizeof(HEADER) / sizeof(unsigned char)));
    size_t hostlen = strlen(server->hostname);
    unsigned short datlen = 7 + (hostlen * 2);
    bufferevent_write(conn, &datlen, 2);
    static const unsigned char PROTOCOL_VERSION[] = { 74 };
    bufferevent_write(conn, PROTOCOL_VERSION, 1);
    bufferevent_write(conn, &hostlen, 2);
    size_t i;
    for (i = 0; i < hostlen; i++) {
      static const unsigned char null[] = { 0x00 };
      bufferevent_write(conn, &server->hostname[i], 1);
      bufferevent_write(conn, null, 1);
    }
    bufferevent_write(conn, &server->port, 4);
  }
};

void readcb(struct bufferevent* conn, void* arg) {
  unsigned char buffer[1024];
  size_t len = bufferevent_read(conn, buffer, sizeof(buffer));
  if (buffer[0] == 0xFF && len > 16) {
    struct server_status status;
    char versionbuf[64];
    char *v = versionbuf;
    char *mv = versionbuf + sizeof(versionbuf);
    size_t i;
    for (i = 16; i < len; i++) {
      if (i % 2 == 0) {
        if (v > mv)
          goto error;
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
    char *mm = motdbuf + sizeof(motdbuf);
    unsigned char special = 0;
    for (; i < len; i++) {
      if (i % 2 == 0) {
        if (m > mm)
          goto error;
        if (isascii(buffer[i])) {
          if (special)
            special = 0;
          else
            *(m++) = buffer[i];
          if (buffer[i] == 0x00)
            break;
        } else if (buffer[i] == 0xa7)
          special = 1;
        else
          goto error;
      } else if (buffer[i] != 0x00)
        goto error;
    }
    status.motd = motdbuf;
    DEBUG(255, "Motd: %s", status.motd);
    if (buffer[++i] != 0 && buffer[++i] != 0 && buffer[++i] != 0)
      goto error;
    status.numplayers = 0;
    for (; i < len; i++) {
      if (i % 2 == 0) {
        if (isdigit(buffer[i]))
          status.numplayers = (status.numplayers * 10) + (buffer[i] - '0');
        else if (buffer[i] == 0x00)
          break;
        else
          goto error;
      } else if (buffer[i] != 0x00)
        goto error;
    }
    DEBUG(255, "Number of online players: %d", status.numplayers);
    if (buffer[++i] != 0 && buffer[++i] != 0 && buffer[++i] != 0)
      goto error;
    status.maxplayers = 0;
    for (; i < len; i++) {
      if (i % 2 == 0) {
        if (isdigit(buffer[i]))
          status.maxplayers = (status.maxplayers * 10) + (buffer[i] - '0');
        else if (buffer[i] == 0x00)
          break;
        else
          goto error;
      } else if (buffer[i] != 0x00)
        goto error;
    }
    DEBUG(255, "Maximum amount of players: %d", status.maxplayers);
    struct server* server = arg;
    status.server = server;
    char buf[BUFSIZ];
    size_t j;
    for (j = 0; server->format[j]; j++) {
      print_status(&status, server->format[j], buf, sizeof(buf));
      printf("%s\n", buf);
    }
  }
  return;
error:
  eventcb(conn, BEV_FINISHED, arg);
};

void eventcb(struct bufferevent* conn, short event, void* arg) {
  if (event != BEV_EVENT_CONNECTED)
    bufferevent_free(conn);
};

#define APPEND(/*char* */ str) \
    while (*str && buf < end) \
      *buf++ = *str++;

int print_status(struct server_status* status, char* format, char* buf, size_t buflen) {
  char* s = buf;
  char* end = s + buflen;
  char* f;
  for (f = format; *f != '\0'; f++) {
    if (*f == '%') {
      f++;
      char key[33];
      if (sscanf(f, "%32[a-z]", key) == 1) {
        if (strcmp(key, "hostname") == 0) {
          APPEND(status->server->hostname);
        } else if (strcmp(key, "version") == 0) {
          APPEND(status->version);
        } else if (strcmp(key, "motd") == 0) {
          APPEND(status->motd);
        } else if (strcmp(key, "numplayers") == 0) {
          snprintf(buf, end - buf, "%d", status->numplayers);
          while (*buf != '\0')
            buf++;
        } else if (strcmp(key, "maxplayers") == 0) {
          snprintf(buf, end - buf, "%d", status->maxplayers);
          while (*buf != '\0')
            buf++;
        } else if (strcmp(key, "time") == 0) {
          snprintf(buf, end - buf, "%ld", time(NULL));
          while (*buf != '\0')
            buf++;
        }
      }
      f += strlen(key) - 1;
    } else if (buf < end) {
      if (*f == '\\') {
        switch (*(++f)) {
        case 'n':
          *buf++ = '\n';
          break;
        case 'r':
          *buf++ = '\r';
          break;
        case 't':
          *buf++ = '\t';
          break;
        default:
          *buf++ = '\\';
          *buf++ = *f;
          break;
        }
      } else
        *buf++ = *f;
    }
  };
  *buf = '\0';
  return buf - s;
};