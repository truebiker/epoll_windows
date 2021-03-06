#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#include <assert.h>
#include <stdio.h>

#include "epoll.h"


#define MAX_PINGS 100

static const char PING[] = "PING";
static const int NUM_PINGERS = 1;
static const DWORD RUN_TIME = 10000;

int x = 0;

int main(int argc, char* argv[])
{
  epoll_t epoll_hnd;
  int r;
  u_long one = 1;
  struct addrinfo hints;
  struct addrinfo *addrinfo;
  struct sockaddr_in addr;
  DWORD ticks_start, ticks_last;
  long long pings = 0, pings_sent = 0;
  int i;
  SOCKET srv;
  struct epoll_event ev;
  uint64_t stop_event = 0;
  int should_stop = 0;


  epoll_hnd = epoll_create();
  assert(epoll_hnd && epoll_hnd != INVALID_HANDLE_VALUE);

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_protocol = IPPROTO_UDP;

  r = getaddrinfo("localhost", NULL, &hints, &addrinfo);
  assert(r == 0);
  assert(addrinfo->ai_addrlen <= sizeof addr);

  memset(&addr, 0, sizeof addr);
  memcpy(&addr, addrinfo->ai_addr, addrinfo->ai_addrlen);
  addr.sin_port = htons(9123);

  freeaddrinfo(addrinfo);

  printf("resolved\n");

  srv = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  r = ioctlsocket(srv, FIONBIO, &one);
  assert(r == 0);

  r = bind(srv, (struct sockaddr *)&addr, sizeof addr);
  assert(r == 0);

  ev.events = EPOLLIN | EPOLLERR;
  ev.data.sock = srv;
  r = epoll_ctl(epoll_hnd, EPOLL_CTL_ADD, srv, &ev);
  assert(r == 0);

  for (i = 0; i < NUM_PINGERS; i++)
  {
    SOCKET sock;
    struct epoll_event ev;

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    r = ioctlsocket(sock, FIONBIO, &one);
    assert(r == 0);

    r = connect(sock, (struct sockaddr*) &addr, sizeof addr);
    /* Unlike unix, windows sets the error to EWOULDBLOCK when the connection
     * is being established in the background.
     */
    assert(r == 0 || WSAGetLastError() == WSAEWOULDBLOCK);

    ev.events = EPOLLOUT | EPOLLERR;
    ev.data.sock = sock;
    r = epoll_ctl(epoll_hnd, EPOLL_CTL_ADD, sock, &ev);
    assert(r == 0);
  }

  ticks_start = GetTickCount();
  ticks_last = ticks_start;
  stop_event = 1;  /* NOTE: comment this to disable stop event */

  for (;!should_stop;)
  {
    int i, count;
    struct epoll_event events[16];
    DWORD ticks;

    ticks = GetTickCount();

    if (ticks >= ticks_last + 1000)
    {
      printf("%lld pings (%f per sec), %lld sent\n", pings, (double) pings / (ticks - ticks_start) * 1000, pings_sent);
      ticks_last = ticks;

      if (stop_event == 0 && ticks - ticks_start > RUN_TIME)
      {
        should_stop = 1;
        continue;
      }
    }

    count = epoll_wait(epoll_hnd, events, 15, 1000);
    assert(count >= 0);

    for (i = 0; i < count; i++)
    {
      int revents = events[i].events;

      if (revents & EPOLLEVENT)
      {
        uint64_t event = events[i].data.u64;

        if (event == stop_event)
        {
          printf("STOP event occured\n");
          should_stop = 1;
          continue;
        }

        assert(0);
      }

      if (revents & EPOLLERR)
      {
        SOCKET sock = events[i].data.sock;
        int r;
        int err = -1;
        int err_len = sizeof err;

        r = getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&err, &err_len);
        assert(r == 0);
        fprintf(stderr, "Socket error: %d\n", err);

        r = epoll_ctl(epoll_hnd, EPOLL_CTL_DEL, sock, NULL);
        assert(r == 0);
        continue;
      }

      if (revents & EPOLLIN)
      {
        SOCKET sock = events[i].data.sock;
        char buf[1024];
        WSABUF wsa_buf;
        DWORD flags, bytes;
        int r;

        wsa_buf.buf = buf;
        wsa_buf.len = sizeof buf;

        flags = 0;
        r = WSARecv(sock, &wsa_buf, 1, &bytes, &flags, NULL, NULL);
        assert(r >= 0);
        assert(bytes == sizeof PING);

        pings++;

        if (stop_event && pings > MAX_PINGS)
          epoll_event_signal(epoll_hnd, stop_event);

        continue;
      }

      if (revents & EPOLLOUT)
      {
        SOCKET sock = events[i].data.sock;
        WSABUF wsa_buf;
        DWORD bytes;
        int r;

        wsa_buf.buf = (char *)PING;
        wsa_buf.len = sizeof PING;
        r = WSASend(sock, &wsa_buf, 1, &bytes, 0, NULL, NULL);
        assert(r >= 0);
        assert(bytes == sizeof PING);

        pings_sent++;
        continue;
      }

      assert(0);
    }
  }

  r = epoll_close(epoll_hnd);
  assert(r == 0);

  /*close(srv);*/
}
