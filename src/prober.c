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

#include <jansson.h>

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
int print_player(json_t* player, char* format, char* buf, size_t buflen);

void timer_callback(evutil_socket_t fd, short event, void* arg) {
  DEBUG(255, "timer_callback(%d, %d, %p);", fd, event, arg);
  struct server* server = arg;
  struct bufferevent* conn = bufferevent_socket_new(event_base, -1, BEV_OPT_CLOSE_ON_FREE);
  static const struct timeval timeout = { 10, 0 };
  bufferevent_set_timeouts(conn, &timeout, &timeout);
  bufferevent_socket_connect_hostname(conn, dns, AF_INET, server->hostname, server->port);
  bufferevent_setcb(conn, readcb, NULL, eventcb, arg);
  bufferevent_enable(conn, EV_READ);
  static const unsigned char HEADER[] = { 0x1E, 0x00 };
  bufferevent_write(conn, HEADER, (sizeof(HEADER) / sizeof(unsigned char)));
  u_int16_t hostlen = strlen(server->hostname);
  int i;
  for (i = sizeof(hostlen) - 1; i >= 0; i--)
    bufferevent_write(conn, &((unsigned char*)&hostlen)[i], 1);
  bufferevent_write(conn, server->hostname, hostlen);
  for (i = sizeof(server->port) - 1; i >= 0; i--)
    bufferevent_write(conn, &((unsigned char*)&server->port)[i], 1);
  static const unsigned char CLOSING[] = { 0x01, 0x01, 0x00 };
  bufferevent_write(conn, CLOSING, (sizeof(CLOSING) / sizeof(unsigned char)));
};

void readcb(struct bufferevent* conn, void* arg) {
  char buffer[BUFSIZ];
  bzero(buffer, sizeof(buffer));
  size_t len = bufferevent_read(conn, buffer, sizeof(buffer));
  char* start_json = NULL;
  size_t i;
  for (i = 0; i < len; i++) {
    if (buffer[i] == '{') {
      start_json = &buffer[i];
      break;
    }
  }
  if (start_json != NULL) {
    char buf[BUFSIZ];
    size_t j;
    struct server* server = arg;
    DEBUG(255, "raw json: %s", start_json);
    json_t* json = json_loads(start_json, 0, NULL);
    struct server_status status;
    json_t* json_description = json_object_get(json, "description");
    if (json_description)
      status.motd = (char*) json_string_value(json_description);
    json_t* json_version = json_object_get(json, "version");
    if (json_version) {
      json_t* version_name = json_object_get(json_version, "name");
      if (version_name)
        status.version = (char*) json_string_value(version_name);
    }
    json_t* json_players = json_object_get(json, "players");
    if (json_players) {
      json_t* max = json_object_get(json_players, "max");
      if (max)
        status.maxplayers = json_integer_value(max);
      json_t* online = json_object_get(json_players, "online");
      if (online)
        status.numplayers = json_integer_value(online);
      if (server->players) {
        json_t* json_sample = json_object_get(json_players, "sample");
        if (json_sample) {
          for (i = 0; i < json_array_size(json_sample); i++) {
            json_t* player = json_array_get(json_sample, i);
            if (player) {
              for (j = 0; server->players[j]; j++) {
                if (print_player(player, server->players[j], buf, sizeof(buf)))
                  printf("%s\n", buf);
              }
            }
          }
        }
      }
    }
    status.server = server;
    for (j = 0; server->format[j]; j++) {
      if (print_status(&status, server->format[j], buf, sizeof(buf)))
        printf("%s\n", buf);
    }
    json_decref(json);
  }
  eventcb(conn, BEV_FINISHED, arg);
};

void eventcb(struct bufferevent* conn, short event, void* arg) {
  DEBUG(255, "eventcb(%p, %d, %p);", conn, event, arg);
  if (event != BEV_EVENT_CONNECTED)
    bufferevent_free(conn);
};

#define APPEND(/*char* */ str) { \
      char* tmp = str; \
      while (*tmp && buf < end) \
        *buf++ = *tmp++; \
    }

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

int print_player(json_t* player, char* format, char* buf, size_t buflen) {
  char* s = buf;
  char* end = s + buflen;
  char* f;
  json_t* name = json_object_get(player, "name");
  json_t* id = json_object_get(player, "id");
  if (name && id) {
    for (f = format; *f != '\0'; f++) {
      if (*f == '%') {
        f++;
        char key[33];
        if (sscanf(f, "%32[a-z]", key) == 1) {
          if (strcmp(key, "name") == 0) {
            APPEND((char*) json_string_value(name));
          } else if (strcmp(key, "id") == 0) {
            APPEND((char*) json_string_value(id));
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
  }
  return 0;
};