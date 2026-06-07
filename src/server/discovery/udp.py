"""UDP discovery server.

Runs two background threads:
  1. Broadcaster: sends "MAG_SERVER <ip> <port>" to 255.255.255.255:<udp_port>
     every 5 seconds.
  2. Listener: receives datagrams on <udp_port>. Responds to "MAG_WHO" with a
     unicast "MAG_SERVER <ip> <port>" back to the sender. All other messages
     (including those from other clients) are silently ignored.
"""

import socket
import threading
import time

from server.logging import get_logger

_LOG = get_logger(__name__)
_BROADCAST_INTERVAL = 5.0


def _local_ip() -> str:
    """Best-effort: connect a UDP socket to a public address to find the
    local IP that the OS would use for LAN traffic."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


class UDPDiscovery:
    def __init__(self, udp_port: int, http_port: int) -> None:
        self._udp_port = udp_port
        self._http_port = http_port
        self._ip = _local_ip()
        self._announcement = f"MAG_SERVER {self._ip} {self._http_port}".encode()
        self._stop = threading.Event()
        self._broadcaster = threading.Thread(target=self._broadcast_loop, daemon=True)
        self._listener = threading.Thread(target=self._listen_loop, daemon=True)

    @property
    def server_ip(self) -> str:
        return self._ip

    def start(self) -> None:
        self._broadcaster.start()
        self._listener.start()
        _LOG.info("UDP discovery started on %s:%d", self._ip, self._udp_port)

    def stop(self) -> None:
        self._stop.set()

    def _broadcast_loop(self) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            while not self._stop.is_set():
                try:
                    sock.sendto(self._announcement, ("255.255.255.255", self._udp_port))
                    _LOG.debug("Broadcast: %s", self._announcement.decode())
                except OSError as e:
                    _LOG.warning("Broadcast error: %s", e)
                self._stop.wait(_BROADCAST_INTERVAL)
        finally:
            sock.close()

    def _listen_loop(self) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.settimeout(1.0)
        try:
            sock.bind(("", self._udp_port))
            while not self._stop.is_set():
                try:
                    data, addr = sock.recvfrom(256)
                except socket.timeout:
                    continue
                msg = data.decode(errors="ignore").strip()
                if msg == "MAG_WHO":
                    try:
                        sock.sendto(self._announcement, addr)
                        _LOG.debug("Replied MAG_SERVER to %s", addr)
                    except OSError as e:
                        _LOG.warning("Reply error: %s", e)
        finally:
            sock.close()
