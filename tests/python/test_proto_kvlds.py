#!/usr/bin/env python3

""" Test the `Wire` classes. """

import logging

import kivaloo.servers
import kivaloo.proto_lbs
import kivaloo.proto_kvlds

VERBOSE = False


def kvlds_set_get(proto_kvlds):
    """ Basic set & get a key / value pair"""
    # Set a value
    key = "hello".encode()
    value = "world!".encode()
    status = proto_kvlds.set(key, value)
    assert status == 0

    # Get that value
    status, value_get = proto_kvlds.get(key)
    assert status == 0
    assert value_get == value


def kvlds_add(proto_kvlds):
    """ Add a value & get a value"""
    # Add a value
    key = "a".encode()
    value = "b".encode()
    status = proto_kvlds.add(key, value)
    assert status == 0

    # Get that value
    status, value_get = proto_kvlds.get(key)
    assert status == 0
    assert value_get == value

    # Attempt to add a value again; it should fail
    status = proto_kvlds.add(key, "b".encode())
    assert status == 1


def kvlds_set(proto_kvlds):
    """ Set a value & get a value"""
    # Add a value
    key = "c".encode()
    value = "d".encode()
    status = proto_kvlds.set(key, value)
    assert status == 0

    # Get that value
    status, value_get = proto_kvlds.get(key)
    assert status == 0
    assert value_get == value

    # Attempt to add a value again; it should be ok
    status = proto_kvlds.set(key, "b".encode())
    assert status == 0


def kvlds_range(proto_kvlds):
    """ Range """
    # Use a small size to force us to call multiple times
    maxbytes = 10

    # Special case: a 0-byte value matches both the beginning and ending
    nextkey = "".encode()
    end = "".encode()

    got = []
    while nextkey is not None:
        # Get some pairs
        status, nextkey, pairs = proto_kvlds.range(maxbytes, nextkey, end)
        assert status == 0

        range_got = []
        for key, val in pairs.items():
            range_got.append((key.decode(), val.decode()))
        got.append(range_got)

    # Check against expected values
    expected = [[('a', 'b'), ('c', 'b')], [('hello', 'world!')]]
    assert got == expected


def main():
    """ Run tests for the `proto_kvlds` class. """
    if VERBOSE:
        logging.basicConfig(format='%(levelname)s:\t%(message)s',
                            level=logging.DEBUG)

    # Make servers, and delete any previous data in the servers
    lbs = kivaloo.servers.Server_lbs()
    lbs.reset()
    kvlds = kivaloo.servers.Server_kvlds()
    proto_kvlds = kivaloo.proto_kvlds.Proto_kvlds()

    # Run tests
    kvlds_set_get(proto_kvlds)
    kvlds_add(proto_kvlds)
    kvlds_set(proto_kvlds)
    kvlds_range(proto_kvlds)

    # Final shutdown of any remaining servers
    proto_kvlds.close()
    kvlds.shutdown()
    lbs.shutdown()


if __name__ == "__main__":
    main()
