import sys

path = "kvlds/btree.c"
with open(path, "r") as f:
    code = f.read()

old = """	/*
	 * Instruct the backing store to free everything older than the
	 * oldest leaf node accessible via the B+Tree root.
	 */
	if (proto_lbs_request_free(T->LBS, T->root_shadow->oldestleaf,
	    callback_free_done, NULL))
		goto err0;"""

new = """	/*
	 * Instruct the backing store to free everything older than the
	 * oldest leaf node accessible via the B+Tree root.
	 */
	if (T->root_shadow->oldestleaf > T->nextblk) {
		warn0("Root oldestleaf beyond end of block store; refusing to FREE");
		/* Schedule another FREE instead of exiting or calling proto_lbs_request_free */
	} else {
		if (proto_lbs_request_free(T->LBS, T->root_shadow->oldestleaf,
		    callback_free_done, NULL))
			goto err0;
	}"""

if old in code:
    code = code.replace(old, new)
    with open(path, "w") as f:
        f.write(code)
    print("Patched kvlds/btree.c successfully")
else:
    print("Failed to find text in kvlds/btree.c")

