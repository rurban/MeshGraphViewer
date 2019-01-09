#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <inttypes.h>
#include <sys/select.h>
#include <signal.h>
#include <getopt.h>
#include <sys/time.h>

#include "webserver.h"
#include "files.h" // will be created by Makefile
#include "utils.h"
#include "main.h"


static const char *help_text = "\n"
  " --webserver-port <port>	Port for the build-in webserver. Set to 0 to disable webserver. Default: 8080\n"
  " --webserver-path <path>	Root folder for the build-in webserver. Default: internal\n"
  " --help				Display this help\n";

// Run state
static int g_is_running;

// Current time
time_t g_now = 0;
char* g_json_file = NULL;


static void unix_signal_handler(int signo) {
  // exit on second stop request
  if (g_is_running == 0) {
    exit(1);
  }

  g_is_running = 0;

  printf("Shutting down...\n");
}

static void setup_signal_handlers() {
  struct sigaction sig_stop;
  struct sigaction sig_term;

  // STRG+C aka SIGINT => Stop the program
  sig_stop.sa_handler = unix_signal_handler;
  sig_stop.sa_flags = 0;
  if ((sigemptyset(&sig_stop.sa_mask) == -1) || (sigaction(SIGINT, &sig_stop, NULL) != 0)) {
    fprintf(stderr, "Failed to set SIGINT handler: %s", strerror(errno));
    exit(1);
  }

  // SIGTERM => Stop the program gracefully
  sig_term.sa_handler = unix_signal_handler;
  sig_term.sa_flags = 0;
  if ((sigemptyset(&sig_term.sa_mask) == -1) || (sigaction(SIGTERM, &sig_term, NULL) != 0)) {
    fprintf(stderr, "Failed to set SIGTERM handler: %s", strerror(errno));
    exit(1);
  }
}

enum {
  oJsonFile,
  oWriteOutFiles,
  oWebserverPort,
  oWebserverPath,
  oHelp
};

static struct option options[] = {
  {"json-file", required_argument, 0, oJsonFile},
  {"write-out-files", required_argument, 0, oWriteOutFiles},
  {"webserver-port", required_argument, 0, oWebserverPort},
  {"webserver-path", required_argument, 0, oWebserverPath},
  {"help", no_argument, 0, oHelp},
  {0, 0, 0, 0}
};

int write_out_files(const char *path) {
  struct content *c;
  int rc;

  c = g_content;
  while (c) {
    // create dirname part
    rc = create_path(c->path);
    if (rc == EXIT_FAILURE) {
      return EXIT_FAILURE;
    }

    // create basename file
    rc = create_file(c->path, c->data, c->size);
      if (rc == EXIT_FAILURE) {
      return EXIT_FAILURE;
    }

    c += 1;
  }

  return EXIT_SUCCESS;
}

int file_exists(const char path[]) {
  return access(path, F_OK) != -1;
}

int main(int argc, char **argv) {
  int webserver_port = 8080;
  const char *webserver_path = NULL;
  struct timeval tv;
  fd_set rset;
  fd_set wset;
  fd_set xset;
  int maxfd;
  int index;
  int rc;
  int i;

  i = 1;
  while (i) {
    index = 0;
    int c = getopt_long(argc, argv, "", options, &index);

    switch (c) {
    case oJsonFile:
      g_json_file = strdup(optarg);
      break;
    case oWriteOutFiles:
      write_out_files(optarg);
      break;
    case oWebserverPort:
      webserver_port = atoi(optarg);
      break;
    case oWebserverPath:
      webserver_path = optarg;
      break;
    case oHelp:
      printf("%s", help_text);
      return 0;
    case -1:
      // End of options reached
      for (i = optind; i < argc; i++) {
        fprintf(stderr, "Unknown option: %s\n", argv[i]);
        return 1;
      }
      i = 0;
      break;
    //case '?':
    //  return 1;
    default:
      return EXIT_FAILURE;
    }
  }

  if (webserver_port < 0) {
    fprintf(stderr, "Invalid webserver port\n");
    return EXIT_FAILURE;
  }

  if (webserver_path && !file_exists(webserver_path)) {
    fprintf(stderr, "Invalid webserver path: %s\n", webserver_path);
    return EXIT_FAILURE;
  }

  printf("Webserver port: %d\n", webserver_port);
  printf("Webserver path: %s\n", webserver_path ? webserver_path : "internal");

  setup_signal_handlers();

  rc = webserver_start(webserver_path, webserver_port);
  if (rc == EXIT_FAILURE) {
    return EXIT_FAILURE;
  }

  g_is_running = 1;
  while (g_is_running) {
    g_now = time(NULL);

    // Make select block for at most 1 second
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    FD_ZERO(&rset);
    FD_ZERO(&wset);
    FD_ZERO(&xset);

    maxfd = 0;
/*
    for (i = 0; i < g_pcap_num; i++) {
      int fd = pcap_get_selectable_fd(g_pcap[i]);
      FD_SET(fd, &rset);
      if (fd > maxfd) {
        maxfd = fd;
      }
    }
*/
    webserver_before_select(&rset, &wset, &xset, &maxfd);

    if (select(maxfd + 1, &rset, &wset, &xset, &tv) < 0) {
      if( errno == EINTR ) {
        continue;
      }

      //fprintf(stderr, "select() %s\n", strerror(errno));
      return EXIT_FAILURE;
    }

    webserver_after_select();
  }

  return EXIT_SUCCESS;
}
