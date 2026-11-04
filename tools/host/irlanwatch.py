#!/usr/bin/env python3
"""What is being beamed on the network: the datagrams of the LAN medium
(`newton --ir-lan`, hal/host/HostIRChip.cpp, docs/comms/README.md "Beaming
over the network") heard by a bystander, one line for each.

    python tools/host/irlanwatch.py [seconds] [--interface ADDRESS]... [--port N] [--bytes N]
                                    [--send-test]

Joins the group 239.255.78.119 on the interfaces named (the one with the
default route if none is), and prints for each datagram when it came (seconds from the start), the address it
came from, the sender's instance and sequence number, the instance it is
pointed at (0: whoever hears it), how many bytes it carries and the first
of them with their modulation (A: Sharp's ASK, I: IrDA).  At the end it
prints how many each sender sent, and the gaps seen in its sequence
numbers - a datagram this machine did not hear.

The watcher takes part in nothing: it is how to tell, when one newton
looks for a receiver and the other hears nothing, whether the sender's
datagrams reach the network.  --send-test sends one datagram of the
medium's form with no bytes in it every second, to show whether what this
machine sends is heard elsewhere (a newton with NEWTON_TRACE_IR prints
nothing for it, but a second watcher does).

Needs only Python 3.
"""

import argparse
import socket
import struct
import time

GROUP = "239.255.78.119"
HEADER = 18


def default_interfaces():
    found = set()
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("192.0.2.1", 9))			# (no packet: only the routing table is asked)
        found.add(s.getsockname()[0])
        s.close()
    except OSError:
        pass
    return sorted(found) or ["0.0.0.0"]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("seconds", type=float, nargs="?", default=30)
    parser.add_argument("--interface", action="append", default=[])
    parser.add_argument("--port", type=int, default=3681)
    parser.add_argument("--bytes", type=int, default=12, help="how many of a datagram's bytes to print")
    parser.add_argument("--send-test", action="store_true")
    args = parser.parse_args()
    interfaces = args.interface or default_interfaces()

    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("", args.port))
    for address in interfaces:
        request = socket.inet_aton(GROUP) + socket.inet_aton(address)
        s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, request)
    s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
    s.settimeout(0.25)
    print("irlanwatch: %s:%d on %s" % (GROUP, args.port, ", ".join(interfaces)), flush=True)

    start = time.time()
    next_test = start
    test_sequence = 0
    senders = {}
    while time.time() - start < args.seconds:
        if args.send_test and time.time() >= next_test:
            test_sequence += 1
            next_test += 1
            datagram = b"NwIR\x01\x00" + struct.pack(">III", 0x7e57, test_sequence, 0)
            for address in interfaces:
                s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(address))
                s.sendto(datagram, (GROUP, args.port))
        try:
            data, sender = s.recvfrom(2048)
        except socket.timeout:
            continue
        except OSError:
            continue
        at = time.time() - start
        if len(data) < HEADER or data[:4] != b"NwIR":
            print("%8.3f %-15s not the medium's: %d bytes" % (at, sender[0], len(data)), flush=True)
            continue
        instance, sequence, to = struct.unpack(">III", data[6:18])
        pairs = data[HEADER:]
        seen = senders.setdefault(instance, {"address": sender[0], "count": 0, "last": None, "missed": 0})
        if seen["last"] is not None and sequence == seen["last"]:
            continue						# (the same datagram, out of another interface)
        if seen["last"] is not None and sequence > seen["last"] + 1:
            seen["missed"] += sequence - seen["last"] - 1
        seen["last"] = sequence
        seen["count"] += 1
        shown = " ".join("%s%02x" % ("I" if pairs[i] else "A", pairs[i + 1])
                         for i in range(0, min(len(pairs) - 1, 2 * args.bytes), 2))
        print("%8.3f %-15s %08x #%-6d to %08x %4d bytes %s" % (at, sender[0], instance, sequence, to, len(pairs) // 2, shown), flush=True)

    for instance, seen in senders.items():
        print("irlanwatch: %08x at %s: %d heard, %d missed" % (instance, seen["address"], seen["count"], seen["missed"]))
    if not senders:
        print("irlanwatch: nothing heard")


if __name__ == "__main__":
    main()
