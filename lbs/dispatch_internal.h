#ifndef _DISPATCH_INTERNAL_H_
#define _DISPATCH_INTERNAL_H_

#include <stdint.h>

/* Opaque types. */
struct netbuf_read;
struct netbuf_write;
struct proto_lbs_request;
struct storage_state;

/* Linked list structure for queue of pending block reads. */
struct readq {
	struct readq * next;		/* Next pending read. */
	uint64_t reqID;			/* Packet ID of GET request. */
	uint64_t blkno;			/* Requested block #. */
};

/* State of the work dispatcher. */
struct dispatch_state {
	/* Thread management. */
	struct workctl ** workers;	/* #0--(nreaders-1) are readers. */
					/* #(nreaders) is the writer. */
					/* #(nreaders+1) is the deleter. */
	size_t nreaders;		/* Number of reader threads. */
	int writer_busy;		/* Is the writer thread busy? */
	int deleter_busy;		/* Is the deleter thread busy? */
	size_t nreaders_idle;		/* How many readers are idle... */
	size_t * readers_idle;		/* ... and what are their #s? */

	/* Storage management. */
	size_t blocklen;		/* Block length. */
	struct storage_state * sstate;	/* Back-end storage state. */

	/* Work done dispatch-poking. */
	int spair[2];			/* Read from [0], write to [1]. */
	size_t wakeupID;		/* Thread ID being read. */
	void * wakeup_cookie;		/* Thread ID read cookie. */

	/* Connection management. */
	int accepting;			/* We are waiting for a connection. */
	int sconn;			/* The current connection. */
	struct netbuf_write * writeq;	/* Buffered writer. */
	struct netbuf_read * readq;	/* Buffered reader. */
	void * read_cookie;		/* Request read cookie. */
	size_t npending;		/* # responses we owe. */

	/* Pending work. */
	struct readq * readq_head;	/* Queue of pending reads. */
	struct readq ** readq_tail;	/* Location of terminating NULL. */
};

/**
 * dispatch_response_send(dstate, thread):
 * Using the dispatch state ${dstate}, send a response for the work which was
 * just completed by thread ${thread}.
 */
int dispatch_response_send(struct dispatch_state *, struct workctl *);

/**
 * dispatch_request_params(dstate, R):
 * Handle and free a PARAMS request.
 */
int dispatch_request_params(struct dispatch_state *,
    struct proto_lbs_request *);

/**
 * dispatch_request_params2(dstate, R):
 * Handle and free a PARAMS2 request.
 */
int dispatch_request_params2(struct dispatch_state *,
    struct proto_lbs_request *);

/**
 * dispatch_request_get(dstate, R):
 * Handle and free a GET request (queue it if necessary).
 */
int dispatch_request_get(struct dispatch_state *,
    struct proto_lbs_request *);

/**
 * dispatch_request_pokereadq(dstate):
 * Launch queued GET(s) if possible.
 */
int dispatch_request_pokereadq(struct dispatch_state *);

/**
 * dispatch_request_append(dstate, R):
 * Handle and free a APPEND request.
 */
int dispatch_request_append(struct dispatch_state *,
    struct proto_lbs_request *);

/**
 * dispatch_request_free(dstate, R):
 * Handle and free a FREE request.
 */
int dispatch_request_free(struct dispatch_state *,
    struct proto_lbs_request *);

#endif /* !_DISPATCH_INTERNAL_H_ */
