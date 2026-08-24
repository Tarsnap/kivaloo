import sys

path = "lib/util/kvlds.c"
with open(path, "r") as f:
    code = f.read()

old = """done:
	/* Success! */
	return (0);"""

new = """done:
	/* If we are done sending and have no requests in flight, we are done. */
	if ((C->eof || C->failed) && (C->inflight == 0))
		C->done = 1;

	/* Success! */
	return (0);"""

if old in code:
    code = code.replace(old, new)
    with open(path, "w") as f:
        f.write(code)
    print("Patched lib/util/kvlds.c successfully")
else:
    print("Failed to find text in lib/util/kvlds.c")

