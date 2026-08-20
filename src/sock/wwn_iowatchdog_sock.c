/*
 * Path B socket client: talk to libwwn_watchdogd_hook.dylib inside watchdogd.
 */
#include "../common/wwn_iowatchdog.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

int wwn_sock_present(void) {
  struct stat st;
  return (stat(WWN_IOW_SOCK_PATH, &st) == 0 && S_ISSOCK(st.st_mode)) ? 1 : 0;
}

int wwn_sock_cmd(const char *cmd, char *reply, size_t reply_len) {
  if (!cmd || !reply || reply_len == 0)
    return -1;
  reply[0] = '\0';
  int s = socket(AF_UNIX, SOCK_STREAM, 0);
  if (s < 0)
    return -1;
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, WWN_IOW_SOCK_PATH, sizeof(addr.sun_path) - 1);
  if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    int e = errno;
    close(s);
    errno = e;
    return -1;
  }
  size_t clen = strlen(cmd);
  char sendbuf[64];
  if (clen + 2 > sizeof(sendbuf)) {
    close(s);
    return -1;
  }
  snprintf(sendbuf, sizeof(sendbuf), "%s\n", cmd);
  if (send(s, sendbuf, strlen(sendbuf), 0) < 0) {
    close(s);
    return -1;
  }
  ssize_t n = recv(s, reply, reply_len - 1, 0);
  close(s);
  if (n < 0)
    return -1;
  reply[n] = '\0';
  return 0;
}
