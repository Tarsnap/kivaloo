/* Legal batched updates against the real KVLDS protocol client. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "events.h"
#include "kivaloo.h"
#include "kvldskey.h"
#include "proto_kvlds.h"
#include "warnp.h"
static int done, failed;
static size_t pending;
static int cb(void *unused, int err) {
    (void)unused; failed |= err;
    assert(pending); if (--pending == 0) done = 1;
    return 0;
}
static int got(void *unused, int err, struct kvldskey *v) {
    unsigned char expect[128]; (void)unused; memset(expect, 'C', sizeof(expect));
    if (err || !v || v->len != sizeof(expect) || memcmp(v->buf, expect, sizeof(expect))) failed=1;
    kvldskey_free(v); assert(pending); if (--pending==0) done=1; return 0;
}
static void batch(struct wire_requestqueue *q, int first, int last, int value) {
    struct kvldskey *k, *v; char key[16]; unsigned char data[128];
    memset(data, value, sizeof(data)); v=kvldskey_create(data,sizeof(data)); assert(v);
    done=failed=0; pending=(size_t)(last-first);
    for (int i=first;i<last;i++) { snprintf(key,sizeof(key),"key%03d",i); k=kvldskey_create((uint8_t *)key,strlen(key)); assert(k);
        assert(proto_kvlds_request_set(q,k,v,cb,NULL)==0); kvldskey_free(k); }
    kvldskey_free(v); assert(events_spin(&done)==0); assert(!failed);
}
int main(int argc,char **argv) {
    struct wire_requestqueue *q; struct kivaloo_cookie *kc; FILE *f;
    WARNP_INIT; assert(argc==3); setvbuf(stdout,NULL,_IONBF,0);
    kc=kivaloo_open(argv[1],&q); assert(kc);
    batch(q,0,64,'A'); puts("seeded 64 real key/value pairs");
    batch(q,63,64,'B'); puts("updated one leaf to create reclaimable pages");
    f=fopen(argv[2],"wb"); assert(f); assert(fclose(f)==0);
    batch(q,0,64,'C'); puts("all 64 updates acknowledged after delayed LBS response");
    done=failed=0; pending=64;
    for(int i=0;i<64;i++) { char key[16]; struct kvldskey *k;
        snprintf(key,sizeof(key),"key%03d",i); k=kvldskey_create((uint8_t *)key,strlen(key)); assert(k);
        assert(proto_kvlds_request_get(q,k,got,NULL)==0); kvldskey_free(k); }
    assert(events_spin(&done)==0); assert(!failed); puts("all 64 exact values verified");
    kivaloo_close(kc); puts("client disconnected; daemon should shut down cleanly"); return 0;
}
