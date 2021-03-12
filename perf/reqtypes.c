#include <stdint.h>

#include "proto_dynamodb_kv.h"
#include "proto_kvlds.h"
#include "proto_lbs.h"
#include "proto_s3.h"

#include "reqtypes.h"

/**
 * reqtypes_lookup(id):
 * Return the symbolic name associated with a kivaloo request type.
 */
const char *
reqtypes_lookup(uint32_t id)
{

	switch (id) {
#define PROTO(x)				\
	case PROTO_ ## x:			\
		return (#x)
	PROTO(DDBKV_PUT);
	PROTO(DDBKV_ICAS);
	PROTO(DDBKV_CREATE);
	PROTO(DDBKV_GET);
	PROTO(DDBKV_GETC);
	PROTO(DDBKV_DELETE);
	PROTO(KVLDS_PARAMS);
	PROTO(KVLDS_SET);
	PROTO(KVLDS_CAS);
	PROTO(KVLDS_ADD);
	PROTO(KVLDS_MODIFY);
	PROTO(KVLDS_DELETE);
	PROTO(KVLDS_CAD);
	PROTO(KVLDS_GET);
	PROTO(KVLDS_RANGE);
	PROTO(LBS_PARAMS);
	PROTO(LBS_PARAMS2);
	PROTO(LBS_GET);
	PROTO(LBS_APPEND);
	PROTO(LBS_FREE);
	PROTO(S3_PUT);
	PROTO(S3_GET);
	PROTO(S3_RANGE);
	PROTO(S3_HEAD);
	PROTO(S3_DELETE);
	default:
		return ("UNKNOWN");
	}
}
