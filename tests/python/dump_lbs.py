#!/usr/bin/env python3

""" Display the contents of a kivaloo LBS server. """

import kivaloo.servers
import kivaloo.proto_lbs


def main():
    """ Dump the contents of a LBS server to stdout. """
    lbs = kivaloo.servers.Server_lbs()
    proto_lbs = kivaloo.proto_lbs.Proto_lbs()

    # Find which blocks are available
    _, next_block, last_block = proto_lbs.params2()
    if last_block == 2**64 - 1:
        print("No data in the LBS server.")
        lbs.shutdown()
        exit(0)
    print("lbs has next_block %d, last_block %d" % (next_block, last_block))

    # Print data from blocks
    print("------")
    for i in range(last_block + 1):
        status, data = proto_lbs.get(i)
        if status == 0:
            print("block %d:" % i)
            print(data)
    print("------")

    lbs.shutdown()


if __name__ == "__main__":
    main()
