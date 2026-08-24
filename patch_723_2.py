import sys

path = "kvlds-undump/main.c"
with open(path, "r") as f:
    code = f.read()

old_key = """		/*
		 * len is uint8_t, so it will be 0..255 (inclusive).
		 * Read that many bytes from stdin; we don't accept eof here
		 * because that would indicate a buffer underrun in buf.
		 */
		if (fread(&buf, len, 1, stdin) != 1)"""
new_key = """		/*
		 * len is uint8_t, so it will be 0..255 (inclusive).
		 * Read that many bytes from stdin; we don't accept eof here
		 * because that would indicate a buffer underrun in buf.
		 */
		if ((len > 0) && (fread(&buf, len, 1, stdin) != 1))"""

old_val = """		/* Read value from stdin (same security rationale as above). */
		if (fread(&len, 1, 1, stdin) != 1)
			goto err1;
		if (fread(&buf, len, 1, stdin) != 1)
			goto err1;"""
new_val = """		/* Read value from stdin (same security rationale as above). */
		if (fread(&len, 1, 1, stdin) != 1)
			goto err1;
		if ((len > 0) && (fread(&buf, len, 1, stdin) != 1))
			goto err1;"""

code = code.replace(old_key, new_key)
code = code.replace(old_val, new_val)

with open(path, "w") as f:
    f.write(code)
print("Patched kvlds-undump/main.c successfully")
