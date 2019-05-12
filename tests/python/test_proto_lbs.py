#!/usr/bin/env python3

""" Test the `Proto_lbs` class. """

import logging
import struct

import kivaloo.servers
import kivaloo.proto_lbs

VERBOSE = False

# For simplicity, these tests will store a single integer in each block
MSG_PAD_BYTES = kivaloo.servers.LBS_BLOCKSIZE - 4*1


def make_msg(num):
    """ Encodes a single number into a python 'bytes' object. """
    block = struct.pack('>%dI%dx' % (1, MSG_PAD_BYTES), num)
    return block


def split_msg(block):
    """ Decodes a single number from a python 'bytes' object. """
    num, = struct.unpack_from('>I', block)
    return num


def lbs_getinfo():
    """ Get information from lbs. """
    kivaloo.servers.Server_lbs()
    proto_lbs = kivaloo.proto_lbs.Proto_lbs()

    # Get data
    blocksize, next_block = proto_lbs.params()
    blocksize2, next_block2, last_block2 = proto_lbs.params2()

    # Sanity check data
    assert blocksize == blocksize2
    assert next_block == next_block2
    assert last_block2 < next_block or last_block2 == 2**64-1

    # Not strictly necessary, but it shouldn't hurt
    proto_lbs.close()


def lbs_append_get_free():
    """ Append and get data from lbs. """
    kivaloo.servers.Server_lbs()
    proto_lbs = kivaloo.proto_lbs.Proto_lbs()

    # Get data
    _, next_block = proto_lbs.params()

    # Append a number
    data = 4
    block = make_msg(data)
    status, next_block2 = proto_lbs.append(1, next_block, block)
    assert status == 0
    assert next_block < next_block2

    # Get a value
    status, data2 = proto_lbs.get(next_block)
    assert status == 0

    # Check the value
    val = split_msg(data2)
    assert val == data

    # Free that value
    status = proto_lbs.free(next_block)
    assert status == 0

    # Try to get it again; it may or may not exist
    status, data2 = proto_lbs.get(next_block)
    if status == 0:
        # If we got a value, it should be correct
        assert split_msg(data2) == data
    elif status == 1:
        # The server already freed that block; that's ok
        assert data2 is None
    else:
        raise Exception("Invalid status!")


def lbs_non_binary_data():
    """ Test trying to append non-binary data. """
    kivaloo.servers.Server_lbs()
    proto_lbs = kivaloo.proto_lbs.Proto_lbs()
    _, next_block = proto_lbs.params()

    # Try to submit non-binary data
    try:
        block = 2
        proto_lbs.append(1, next_block, block)
        raise Exception("Should not have been able to append this")
    except kivaloo.proto_lbs.NotBinaryData:
        pass


def main():
    """ Run tests for the `Proto_lbs` classes. """
    if VERBOSE:
        logging.basicConfig(format='%(levelname)s:\t%(message)s',
                            level=logging.DEBUG)

    lbs_getinfo()
    lbs_append_get_free()
    lbs_non_binary_data()

    # Final shutdown of any remaining servers
    kivaloo.servers.Server_kvlds.shutdown()
    kivaloo.servers.Server_lbs.shutdown()


if __name__ == "__main__":
    main()
