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

#include "config.h"
#include "prober.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#define DEFAULT_PORT 25565
#define DEFAULT_INTERVAL 60 /* This is in seconds */

static struct config {
  struct server* servers;
} global_config;

struct evdns_base* dns = NULL;

struct event_base* event_base = NULL;

int parse_config(char* filename) {
  FILE* f = fopen(filename, "r");
  if (!f) {
    fprintf(stderr, "Error '%s' while opening '%s'.\n", strerror(errno), filename);
    return 0;
  }
  bzero(&global_config, sizeof(struct config));
  char linebuffer[BUFSIZ];
  unsigned int line_count = 0;
  struct server* server = NULL;
  while (fgets(linebuffer, sizeof(linebuffer), f)) {
    line_count++;
    if (linebuffer[0] == '#' || linebuffer[1] == 1)
      continue;
    char key[BUFSIZ];
    char value[BUFSIZ];
    if (sscanf(linebuffer, "%[a-z_] = %[^\t\n]", key, value) == 2) {
      if (strcmp(key, "hostname") == 0) {
        if (server == NULL) {
          global_config.servers = malloc(sizeof(struct server));
          bzero(global_config.servers, sizeof(struct server));
          server = global_config.servers;
        } else {
          server->next = malloc(sizeof(struct server));
          server = server->next;
        }
        server->port = DEFAULT_PORT;
        server->interval = DEFAULT_INTERVAL;
        server->hostname = strdup(value);
      } else if (server && strcmp(key, "format") == 0) {
        if (server->format == NULL) {
          server->format = malloc(sizeof(char*) * 2);
          bzero(server->format, sizeof(char*) * 2);
          server->format[0] = strdup(value);
        } else {
          size_t i = 0;
          while (server->format[++i]);
          server->format = realloc(server->format, sizeof(char*) * (i + 2));
          server->format[i] = strdup(value);
          server->format[++i] = NULL;
        }
      } else if (server && strcmp(key, "port") == 0) {
        errno = 0;
        long port = strtol(value, NULL, 10);
        if ((errno == ERANGE || (port == LONG_MAX || port == LONG_MIN)) || (errno != 0 && port == 0) || port < 0 || port > 65535) {
          fprintf(stderr, "Error at line %d. Port %ld is out of range.\n", line_count, port);
          return 0;
        }
        server->port = (unsigned short) port;
      } else if (server && strcmp(key, "interval") == 0) {
        errno = 0;
        long interval = strtol(value, NULL, 10);
        if ((errno == ERANGE || (interval == LONG_MAX || interval == LONG_MIN)) || (errno != 0 && interval == 0) || interval < 0) {
          fprintf(stderr, "Error at line %d. %ld is invalid.\n", line_count, interval);
          return 0;
        }
        server->interval = interval;
      }
      else if (strcmp(key, "unbuffered") == 0)
        setvbuf(stdout, NULL, _IONBF, 0);
    }
  }
  return line_count;
};

void dispatch_config(struct event_base* base) {
  dns = evdns_base_new(base, 1);
  struct server* node = global_config.servers;
  while (node) {
    struct timeval tv;
    tv.tv_sec = node->interval;
    tv.tv_usec = 0;
    node->timer = event_new(base, -1, EV_PERSIST, timer_callback, node);
    event_add(node->timer, &tv);
    node = node->next;
  };
};

void dispatch_once(struct event_base* base) {
  dns = NULL; /* Using a blocking dns here else it'll never quit :/ FIXME */
  struct server* node = global_config.servers;
  while (node) {
    timer_callback(-1, 1, node);
    node = node->next;
  };
};