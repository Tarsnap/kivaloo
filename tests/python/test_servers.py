#!/usr/bin/env python3

""" Test the `Server` classes. """

import logging

import kivaloo.servers

VERBOSE = False


def multiple_lbs():
    """ Handle multiple Server_lbs objects. """
    # Create two objects; should have no errors
    lbs_a = kivaloo.servers.Server_lbs()
    lbs_b = kivaloo.servers.Server_lbs()

    assert lbs_a.get_stderr() is None
    assert lbs_b.get_stderr() is None

    # Store the pid for later comparison
    pid_old = lbs_a.get_pid_from_file()

    # These should point to the same process
    assert lbs_a.proc.pid is not None
    assert lbs_a.proc == lbs_b.proc

    # Close down the server
    lbs_a.shutdown()

    # Both objects should report no server
    assert lbs_a.proc is None
    assert lbs_b.proc is None

    # Start a new server
    lbs_c = kivaloo.servers.Server_lbs()
    assert lbs_c.get_stderr() is None

    # We should have a new server, which implies a new pid
    pid_new = lbs_a.get_pid_from_file()
    assert pid_new != pid_old

    # Clean up
    lbs_c.shutdown()


def basic_kivaloo():
    """ Launch a kivaloo server, but do nothing. """
    lbs = kivaloo.servers.Server_lbs()
    kvlds = kivaloo.servers.Server_kvlds()

    kvlds.shutdown()
    lbs.shutdown()


def nocleanup():
    """ Launch servers, but don't clean them up. """
    _lbs = kivaloo.servers.Server_lbs()
    _kvlds = kivaloo.servers.Server_kvlds()


def stderr_lbs():
    """ Check the error-reporting capability. """

    # Make a (deep) copy
    cmd_orig = list(kivaloo.servers.Server_lbs.cmd)

    # Deliberately introduce an error
    kivaloo.servers.Server_lbs.cmd.append("--badarg")

    try:
        lbs = kivaloo.servers.Server_lbs()
        lbs.shutdown()
        raise Exception("Server should not have started!")
    except kivaloo.servers.StartError as err:
        # We wanted this exception.  Now we check stderr.
        lines = str(err).splitlines()[3]
        assert "lbs: illegal option -- --badarg" in lines

    # Restore the original
    kivaloo.servers.Server_lbs.cmd = cmd_orig


def main():
    """ Run tests for the `servers` class. """
    if VERBOSE:
        logging.basicConfig(format='%(levelname)s:\t%(message)s',
                            level=logging.DEBUG)

    # Run tests
    stderr_lbs()
    nocleanup()
    multiple_lbs()
    basic_kivaloo()

    # Final shutdown of any remaining servers
    kivaloo.servers.Server_kvlds.shutdown()
    kivaloo.servers.Server_lbs.shutdown()


if __name__ == "__main__":
    main()
