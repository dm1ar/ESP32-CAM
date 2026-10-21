#!/usr/bin/env python3
"""Independent on-device ICE/DTLS/SCTP and original-JPEG acceptance check.

Run with build/webrtc-test-env/bin/python tools/check_webrtc.py http://<DHCP-IP>
Requires aiortc and Pillow. Does not record credentials or modify boot defaults.
"""
import argparse
import asyncio
import io
import json
import struct
import urllib.error
import urllib.request

from aiortc import RTCPeerConnection, RTCConfiguration, RTCSessionDescription
from PIL import Image
from aioice.mdns import create_mdns_hostname, create_mdns_protocol


def request(base, path, data=None, content_type="application/sdp"):
    req = urllib.request.Request(base + path, data=data,
                                 headers={"Content-Type": content_type})
    with urllib.request.urlopen(req, timeout=25) as response:
        return response.read()


async def cycle(base, count, obfuscate):
    pc = RTCPeerConnection(RTCConfiguration(iceServers=[]))
    channel = pc.createDataChannel("jpeg", ordered=True)
    complete = asyncio.get_running_loop().create_future()
    frame_id, total, image, frames = None, 0, bytearray(), 0
    owns = False
    mdns = None

    @channel.on("open")
    def opened():
        channel.send("next")

    @channel.on("message")
    def message(packet):
        nonlocal frame_id, total, image, frames
        try:
            assert isinstance(packet, bytes) and 17 <= len(packet) <= 4112
            magic, ident, size, offset = struct.unpack("!4I", packet[:16])
            assert magic == 0x4A504731 and 0 < size <= 524288
            if offset == 0:
                frame_id, total, image = ident, size, bytearray()
            assert ident == frame_id and size == total and offset == len(image)
            image.extend(packet[16:])
            assert len(image) <= total
            if len(image) == total:
                with Image.open(io.BytesIO(image)) as decoded:
                    decoded.load()
                    print(f"  frame {frames + 1}: {decoded.size}, {total} bytes")
                frames += 1
                if frames >= count:
                    if not complete.done():
                        complete.set_result(frames)
                    return
            channel.send("next")
        except Exception as error:
            if not complete.done():
                complete.set_exception(error)

    try:
        await pc.setLocalDescription(await pc.createOffer())
        offer = pc.localDescription.sdp
        if obfuscate:
            mdns = await create_mdns_protocol()
            addresses = {}
            lines = []
            for line in offer.splitlines():
                if line.startswith("a=candidate:"):
                    fields = line.split()
                    address = fields[4]
                    if ":" in address:
                        continue
                    if address not in addresses:
                        addresses[address] = create_mdns_hostname()
                        await mdns.publish(addresses[address], address)
                    fields[4] = addresses[address]
                    line = " ".join(fields)
                lines.append(line)
            assert addresses, "No IPv4 host candidate for mDNS test"
            offer = "\r\n".join(lines) + "\r\n"
            print(f"  published {len(addresses)} browser-style mDNS host candidate(s)")
        answer = await asyncio.to_thread(request, base, "/webrtc/offer",
                                         offer.encode())
        owns = True
        await pc.setRemoteDescription(RTCSessionDescription(answer.decode(), "answer"))
        await asyncio.wait_for(complete, timeout=60)
        status = json.loads(await asyncio.to_thread(request, base, "/status"))
        assert status["webrtc_state"] == "streaming", status
        snapshot = await asyncio.to_thread(request, base, "/capture")
        with Image.open(io.BytesIO(snapshot)) as decoded:
            decoded.load()
        print("  concurrent snapshot decoded; internal heap:",
              status["internal_heap_free_bytes"], "largest:",
              status["internal_heap_largest_bytes"])
        # A competing video request must not take or clean up this owner.
        try:
            await asyncio.to_thread(request, base, "/webrtc/offer",
                                    offer.encode())
            raise AssertionError("second WebRTC owner was accepted")
        except urllib.error.HTTPError as error:
            assert error.code == 409, error.code
    finally:
        if owns:
            await asyncio.to_thread(request, base, "/webrtc/close", b"")
        await pc.close()
        if mdns is not None:
            await mdns.close()
    await asyncio.sleep(1)
    status = json.loads(await asyncio.to_thread(request, base, "/status"))
    assert not status["streaming"] and status["camera_error_code"] == 0, status
    assert status["webrtc_state"] == "idle", status


async def main(base, frames, obfuscate):
    for index in range(2):
        print(f"WebRTC connection {index + 1}")
        await cycle(base.rstrip("/"), frames, obfuscate)
    print("PASS: real ICE/DTLS/SCTP, JPEG decoding, snapshots, ownership and reconnect")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url")
    parser.add_argument("--frames", type=int, default=5)
    parser.add_argument("--mdns", action="store_true", help="Publish and offer .local ICE names as browsers do")
    args = parser.parse_args()
    if args.frames < 1:
        parser.error("--frames must be positive")
    asyncio.run(main(args.url, args.frames, args.mdns))
