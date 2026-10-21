"""Run the production RTSP server/worker on POSIX sockets with fake camera IO."""
import json
import socket
import subprocess
import sys
import time


class Client:
    def __init__(self):
        self.socket = socket.create_connection(("127.0.0.1", 8554), timeout=5)
        self.file = self.socket.makefile("rb")
        self.sequence = 0
        self.session = None

    def close(self):
        self.file.close()
        self.socket.close()

    def message(self):
        first = self.file.read(1)
        if not first:
            raise EOFError("RTSP disconnected")
        if first == b"$":
            header = self.file.read(3)
            assert len(header) == 3
            length = int.from_bytes(header[1:], "big")
            payload = self.file.read(length)
            assert len(payload) == length
            return (header[0], payload)
        line = first + self.file.readline()
        assert line.startswith(b"RTSP/1.0 "), line
        headers = {}
        while True:
            field = self.file.readline()
            if field == b"\r\n":
                break
            assert field, "Missing RTSP header terminator"
            key, value = field.decode().split(":", 1)
            headers[key.lower()] = value.strip()
        body = self.file.read(int(headers.get("content-length", "0")))
        if "session" in headers:
            self.session = headers["session"].split(";", 1)[0]
        return (int(line.split()[1]), headers, body)

    def request(self, method, extra="", expected=200, fragmented=False):
        self.sequence += 1
        uri = "rtsp://127.0.0.1:8554/stream"
        if method == "SETUP":
            uri += "/trackID=0"
        session = f"Session: {self.session}\r\n" if self.session else ""
        data = (f"{method} {uri} RTSP/1.0\r\nCSeq: {self.sequence}\r\n"
                f"{session}{extra}\r\n").encode()
        if fragmented:
            self.socket.sendall(data[:7])
            time.sleep(.01)
            self.socket.sendall(data[7:])
        else:
            self.socket.sendall(data)
        for _ in range(2000):
            message = self.message()
            if len(message) == 3:
                assert message[0] == expected, message
                assert int(message[1]["cseq"]) == self.sequence
                return message
        raise AssertionError("No RTSP response")

    def frame(self):
        offsets = []
        timestamp = None
        for _ in range(2000):
            channel, packet = self.message()
            if channel != 0:
                continue
            assert packet[0] == 0x80 and packet[1] & 0x7f == 26
            current = int.from_bytes(packet[4:8], "big")
            timestamp = current if timestamp is None else timestamp
            assert current == timestamp
            offsets.append(int.from_bytes(packet[13:16], "big"))
            if packet[1] & 0x80:
                assert offsets[0] == 0 and offsets == sorted(offsets)
                return
        raise AssertionError("No complete RTP frame")


def start(binary, fixture, expire=False):
    process = subprocess.Popen([binary, fixture] + (["expire"] if expire else []),
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    assert process.stdout.readline().strip() == "READY", process.stderr.read()
    return process


def command(process, text):
    process.stdin.write(text + "\n")
    process.stdin.flush()
    return json.loads(process.stdout.readline())


def stop(process):
    process.terminate()
    _, error = process.communicate(timeout=10)
    assert "AddressSanitizer" not in error and "runtime error:" not in error, error


def main():
    binary, fixture = sys.argv[1:]
    process = start(binary, fixture)
    try:
        client = Client()
        try:
            client.request("OPTIONS", fragmented=True)
            assert b"JPEG/90000" in client.request("DESCRIBE")[2]
            client.request("PLAY", expected=454)
            client.request("SETUP", "Transport: RTP/AVP;multicast;client_port=4000-4001\r\n", expected=461)
            client.request("SETUP", "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n")
            # Simulate a competing HTTP stream using the real owner queue.
            assert command(process, "begin")["result"] == 0
            client.request("PLAY", expected=453)
            assert command(process, "finish")["result"] == 0
            client.request("PLAY")
            client.frame()
            assert command(process, "begin")["result"] == 0x103
            assert command(process, "snapshot")["snapshot"]
            assert command(process, "status")["streaming"]
            client.frame()
            client.request("GET_PARAMETER")
            client.request("PAUSE")
            client.request("PLAY")
            client.frame()
            client.request("TEARDOWN")
        finally:
            client.close()
        # Independent decoder verifies JPEG reconstruction, transport and SDP
        # rather than merely checking our own packetizer against itself.
        for transport in ("tcp", "udp", "tcp"):
            result = subprocess.run([
                "ffmpeg", "-hide_banner", "-loglevel", "error", "-rtsp_transport", transport,
                "-i", "rtsp://127.0.0.1:8554/stream", "-frames:v", "3", "-f", "framemd5", "-"],
                capture_output=True, text=True, timeout=20)
            assert result.returncode == 0, result.stderr
            assert len([line for line in result.stdout.splitlines() if line and not line.startswith("#")]) == 3
        # Abrupt disconnect must release ownership so the next PLAY succeeds.
        for _ in range(2):
            client = Client()
            client.request("SETUP", "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n")
            client.request("PLAY")
            client.frame()
            client.close()
        client = Client()
        client.request("SETUP", "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n")
        client.request("PLAY")
        client.frame()
        assert command(process, "stop")["stopping"]
        try:
            while True:
                client.message()
        except EOFError:
            pass
        finally:
            client.close()
        assert not command(process, "status")["streaming"]
        assert command(process, "snapshot")["snapshot"]
        print("PASS: shared snapshots, competing stream ownership and asynchronous Stop")
        print("PASS: production RTSP sockets, fragmentation, SDP, pause/resume, teardown, reconnect, FFmpeg TCP/UDP decoding with injected UDP ENOMEM")
    finally:
        stop(process)
    process = start(binary, fixture, expire=True)
    try:
        client = Client()
        client.request("SETUP", "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n")
        client.request("PLAY")
        started = time.monotonic()
        try:
            while True:
                client.message()
        except EOFError:
            assert 1 < time.monotonic() - started < 8, "Auto-off timing outside test bound"
        finally:
            client.close()
        assert command(process, "status")["timed_out"]
        assert not command(process, "status")["streaming"]
        print("PASS: RTSP device deadline closes stream independently of HTTP polling")
    finally:
        stop(process)


if __name__ == "__main__":
    main()
