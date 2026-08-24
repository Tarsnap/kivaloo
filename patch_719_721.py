import sys

path = "kvlds/btree_node.c"
with open(path, "r") as f:
    code = f.read()

old = """		/* Parse the page. */
		if (deserialize(N, buf, R->pagelen)) {
			warn0("Cannot deserialize page");
			goto err2;
		}

		/* If this was a root, parse global tree data. */
		if (N->root) {"""

new = """		/* Parse the page. */
		if (deserialize(N, buf, R->pagelen)) {
			warn0("Cannot deserialize page");
			goto err2;
		}

		/* Validate node metadata. */
		if (N->root && (N != R->T->root_dirty) && (N != R->T->root_shadow)) {
			warn0("Non-root page has root bit set");
			goto err2;
		}
		if (N->p_shadow && (N->height >= N->p_shadow->height)) {
			warn0("Child page height is not less than parent's height");
			goto err2;
		}
		if (N->p_dirty && (N->height >= N->p_dirty->height)) {
			warn0("Child page height is not less than parent's height");
			goto err2;
		}

		/* If this was a root, parse global tree data. */
		if (N->root) {"""

if old in code:
    code = code.replace(old, new)
    with open(path, "w") as f:
        f.write(code)
    print("Patched kvlds/btree_node.c successfully")
else:
    print("Failed to find text in kvlds/btree_node.c")

