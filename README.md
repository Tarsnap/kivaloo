Kivaloo
-------

Kivaloo (pronounced "kee-va-lieu") is a collection of utilities which together
form a data store associating keys of up to 255 bytes with values of up to 255
bytes.  It was designed to satisfy the needs of the Tarsnap online backup
service for high-performance key-value storage.

At present, kivaloo comprises a block store (lbs) providing log-structured
storage within a local filesystem; a key-value store (kvlds) which manages a
log-structured B+Tree and services requests upon it from a single connection;
and a request multiplexer (mux) which accepts multiple connections and routes
requests and responses to and from a single "upstream" connection.

It is likely that other components will be added in the future to add more
features (e.g., replication) or provide alternatives (e.g., other forms of
underlying storage).


Features
--------

Kivaloo has some features which distinguish it from most other "nosql" data
stores:

* Kivaloo is durable: Writes are not acknowledged until data has been fsynced
  to disk. (The lbs utility supports a "data loss mode" which skips fsyncing
  for debugging purposes.)
* Kivaloo is strongly consistent: If operation A completes before operation B
  starts, operation B will see the results of operation A.
* Unlike most data stores based on log-structured storage, the kvlds utility
  performs background cleaning based on the I/O rate and the amount of disk
  space used; thus there is no need for separate periodic "compaction" to be
  performed.
* Due to the use of a B+Tree to store key-value pairs, kivaloo supports "RANGE"
  requests.


Performance
-----------

On an [EC2](http://aws.amazon.com/ec2/) c1.medium instance using ephemeral-disk
storage, with a 2 kB B+Tree page size and 40-byte keys and 40-byte values:

* Bulk inserts run at about 125,000 pairs per second.
* Bulk extracts run at about 30,000 pairs per second while in RAM, and about
  20,000 pairs per second from disk.
* Bulk updates run at about 110,000 pairs per second while in RAM, drop to
  about 75,000 pairs per second while pages are in the operating system disk
  cache, and then to 60,000 pairs per second when the keys being updated need
  to be loaded from disk.
* Random reads run at about 160,000 to 220,000 pairs per second while in RAM,
  drop to about 6,000 to 11,000 pairs per second while pages are in the
  operating system disk cache, and are disk-seek-limited when pages need to be
  loaded from disk.
* Random mixed (50% read, 50% update) runs at about 14,000 to 30,000 pairs per
  second while in RAM, drops to around 1,000 to 4,000 pairs per second while in
  the operating system disk cache, and is disk-seek-limited when pages need to
  be loaded from disk.
* Hot-spot read (pick a random set of 65536 consecutive keys, read them in a
  random order, and repeat — this is similar to one of Tarsnap's read access
  patterns) runs at about 220,000 pairs per second while in RAM, and about
  60,000 pairs per second from disk.

For more details, see the [kivaloo
performance](http://www.tarsnap.com/kivaloo-perf.html) page.



Download
--------

The following versions of kivaloo are available:

Version | Release date | SHA256 hash
------- | ------------ | -----------
[kivaloo 1.2.0](http://www.tarsnap.com/kivaloo/kivaloo-1.2.0.tgz) | 2011-10-11 | 7c15d63493790b5480206ed25d4093a78bb271ce8198080a5601d6155e4eeccf
[kivaloo 1.1.2](http://www.tarsnap.com/kivaloo/kivaloo-1.1.2.tgz) | 2011-09-26 | 6097a4635284f1d0cb238eca6ee067a933206d645eef38c5ab37fe545ce6cd36
[kivaloo 1.1.1](http://www.tarsnap.com/kivaloo/kivaloo-1.1.1.tgz) | 2011-06-11 | 07230b52fe0d3a21bcf90622445177444b9b11ceec296e756627f68cc8d9a7af
[kivaloo 1.1.0](http://www.tarsnap.com/kivaloo/kivaloo-1.1.0.tgz) | 2011-04-10 | e70cc7bec054c049e0f9d34ab00023438abacecfe28886792e19ae888589ca77
[kivaloo 1.0.1](http://www.tarsnap.com/kivaloo/kivaloo-1.0.1.tgz) | 2011-03-28 | 54145e602c16595e05133fb701270f2025f16bca04e45766f6635acb61e609e2
[kivaloo 1.0.0](http://www.tarsnap.com/kivaloo/kivaloo-1.0.0.tgz) | 2011-03-28 | 46abf63e8b8b75f441a822c4e1a04083fec578f4e936fe51c8203a524c75d124


Mailing list
------------

The kivaloo data store is discussed on the <kivaloo@tarsnap.com> mailing list.


