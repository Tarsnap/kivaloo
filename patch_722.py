import sys

path = "libcperciva/http/http.c"
with open(path, "r") as f:
    code = f.read()

old = """		/* Otherwise, check that it's not too big. */
		if (clen > H->res_bodylen_max - H->res.bodylen)
			return (toobig(H));
		if (clen > SIZE_MAX - 2)
			return (toobig(H));"""

new = """		/* Otherwise, check that it's not too big. */
		if (clen > SIZE_MAX - 2)
			return (toobig(H));
		if (clen + 2 > H->res_bodylen_max - H->res.bodylen)
			return (toobig(H));"""

if old in code:
    code = code.replace(old, new)
    with open(path, "w") as f:
        f.write(code)
    print("Patched libcperciva/http/http.c successfully")
else:
    print("Failed to find text in libcperciva/http/http.c")

