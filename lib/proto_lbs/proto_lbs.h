#ifndef _PROTO_LBS_H_
#define _PROTO_LBS_H_

#include <stdint.h>

#include "netbuf.h"

#include "wire.h"

/**
 * proto_lbs_request_params(Q, callback, cookie):
 * Send a PARAMS request via the request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed, blklen, blkno)
 * upon request completion, where failed is 0 on success and 1 on failure,
 * blklen is the block size, and blkno is the next block #.
 */
int proto_lbs_request_params(struct wire_requestqueue *,
    int (*)(void *, int, size_t, uint64_t), void *);

/**
 * proto_lbs_request_get(Q, blkno, blklen, callback, cookie):
 * Send a GET request to read block ${blkno} of length ${blklen} via the
 * request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed, status, buf)
 * upon request completion, where failed is 0 on success and 1 on failure,
 * status is 0 if the block has been read and 1 if the block does not exist,
 * and buf contains the block data.
 */
int proto_lbs_request_get(struct wire_requestqueue *, uint64_t, size_t,
    int (*)(void *, int, int, const uint8_t *), void *);

/**
 * proto_lbs_request_append_blks(Q, nblks, blkno, blklen, bufv,
 *     callback, cookie):
 * Send an APPEND request to write ${nblks} ${blklen}-byte blocks, starting
 * at position ${blkno}, with data from ${bufv[0]} ... ${bufv[nblks - 1]} to
 * the request queue ${Q}.  Invoke
 *    ${callback}(${cookie}, failed, status, blkno)
 * upon request completion, where failed is 0 on success and 1 on failure,
 * status is 0 if the append completed and 1 otherwise, and blkno is the
 * next available block number. 
 */
int proto_lbs_request_append_blks(struct wire_requestqueue *,
    uint32_t, uint64_t, size_t, const uint8_t * const *,
    int (* callback)(void *, int, int, uint64_t), void *);

/**
 * proto_lbs_request_append(Q, nblks, blkno, blklen, buf, callback, cookie):
 * Send an APPEND request to write ${nblks} ${blklen}-byte blocks, starting
 * at position ${blkno}, with data from ${buf} to the request queue ${Q}.
 * Invoke
 *    ${callback}(${cookie}, failed, status, blkno)
 * upon request completion, where failed is 0 on success and 1 on failure,
 * status is 0 if the append completed and 1 otherwise, and blkno is the
 * next available block number. 
 */
int proto_lbs_request_append(struct wire_requestqueue *,
    uint32_t, uint64_t, size_t, const uint8_t *,
    int (*)(void *, int, int, uint64_t), void *);

/**
 * proto_lbs_request_free(Q, blkno, callback, cookie):
 * Send a FREE request to free blocks numbred less than ${blkno} to the
 * request queue ${Q}.  Invoke
 *     ${callback}(${cookie}, failed)
 * upon request completion, where failed is 0 on success and 1 on failure.
 */
int proto_lbs_request_free(struct wire_requestqueue *, uint64_t,
    int (*)(void *, int), void *);

/* Packet types. */
#define PROTO_LBS_PARAMS	0
#define PROTO_LBS_GET		1
#define PROTO_LBS_APPEND	2
#define PROTO_LBS_FREE		3
#define PROTO_LBS_NONE		((uint32_t)(-1))

/* LBS request structure. */
struct proto_lbs_request {
	uint64_t ID;
	uint32_t type;
	union proto_lbs_request_data {
		struct proto_lbs_request_params {
			/*
			 * We don't have any parameters here; but make the
			 * structure non-empty to avoid compiler warnings.
			 * Since we're inside a union with larger structs
			 * this doesn't waste any memory.
			 */
			int dummy;		/* Dummy variable. */
		} params;
		struct proto_lbs_request_get {
			uint64_t blkno;		/* Block # to read. */
		} get;
		struct proto_lbs_request_append {
			uint32_t nblks;		/* # of blocks to write. */
			uint32_t blklen;	/* Block length. */
			uint64_t blkno;		/* First block # to write. */
			uint8_t * buf;		/* Data to write. */
		} append;
		struct proto_lbs_request_free {
			uint64_t blkno;		/* First block # to keep. */
		} free;
	} r;
};

/**
 * proto_lbs_request_read(R, req):
 * Read a packet from the reader ${R} and parse it as an LBS request.  Return
 * the parsed request via ${req}.  If no request is available, return with
 * ${req}->type == PROTO_LBS_NONE.
 */
int proto_lbs_request_read(struct netbuf_read *, struct proto_lbs_request *);

/**
 * proto_lbs_response_params(Q, ID, blklen, blkno):
 * Send a PARAMS response with ID ${ID} to the write queue ${Q} indicating
 * that the block size is ${blklen} bytes and the next available block # is
 * ${blkno}.
 */
int proto_lbs_response_params(struct netbuf_write *, uint64_t,
    uint32_t, uint64_t);

/**
 * proto_lbs_response_get(Q, ID, status, blklen, buf):
 * Send a GET response with ID ${ID} to the write queue ${Q} with status code
 * ${status} and ${blklen} bytes of data from ${buf} if ${status} is zero.
 */
int proto_lbs_response_get(struct netbuf_write *, uint64_t,
    uint32_t, uint32_t, const uint8_t *);

/**
 * proto_lbs_response_append(Q, ID, status, blkno):
 * Send an APPEND response with ID ${ID} to the write queue ${Q} with status
 * code ${status} and next block number ${blkno} if ${status} is zero.
 */
int proto_lbs_response_append(struct netbuf_write *, uint64_t,
    uint32_t, uint64_t);

/**
 * proto_lbs_response_free(Q, ID):
 * Send a FREE response with ID ${ID} to the write queue ${Q}.
 */
int proto_lbs_response_free(struct netbuf_write *, uint64_t);

#endif /* !_PROTO_LBS_H_ */
