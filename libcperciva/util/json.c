#include <stdint.h>
#include <string.h>

#include "json.h"

static const uint8_t * skip_value(const uint8_t *, const uint8_t *);

/* Advance past whitespace, if any. */
static const uint8_t *
skip_ws(const uint8_t * buf, const uint8_t * end)
{

	/* Skip " \t\r\n". */
	while (buf < end) {
		if ((buf[0] != 0x09) && (buf[0] != 0x0A) &&
		    (buf[0] != 0x0D) && (buf[0] != 0x20))
			break;
		buf++;
	}

	/* Return the first non-whitespace character or the buffer end. */
	return (buf);
}

/* Advance past literal. */
static const uint8_t *
skip_literal(const uint8_t * buf, const uint8_t * end)
{

	/* MUST be "false", "null", or "true". */
	if (((end - buf) >= 5) && (memcmp(buf, "false", 5) == 0))
		return (&buf[5]);
	if (((end - buf) >= 4) && (memcmp(buf, "null", 4) == 0))
		return (&buf[4]);
	if (((end - buf) >= 4) && (memcmp(buf, "true", 4) == 0))
		return (&buf[4]);

	/* We do not have a literal here; return a pointer to the end. */
	return (end);
}

/* Advance past string. */
static const uint8_t *
skip_string(const uint8_t * buf, const uint8_t * end)
{
	uint8_t ch;

	/* Advance past leading '"'. */
	buf++;

	/* Scan until we find a terminating '"' or run out of input. */
	while (buf < end) {
		ch = *buf++;
		if (ch == '"')
			break;
		if (ch == '\\') {
			if (buf == end)
				break;
			ch = *buf++;
			if (ch == 'u') {
				if (end - buf < 4)
					break;
				buf += 4;
			}
		}
	}

	/* Return our current position. */
	return (buf);
}

/* Advance past number. */
static char numchars[] = "+-0123456789.eE";
static const uint8_t *
skip_number(const uint8_t * buf, const uint8_t * end)
{

	/*
	 * In valid JSON, any sequence of (unquoted) characters which
	 * individually can be found in a number must collectively be a
	 * number -- so we eat those until we run out.
	 */
	while (buf < end) {
		if (strchr(numchars, buf[0]) == NULL)
			break;
		buf++;
	}

	/* Return our current position. */
	return (buf);
}

/* Advance past array. */
static const uint8_t *
skip_array(const uint8_t * buf, const uint8_t * end)
{
	const uint8_t * stack[1024];
	int depth = 0;

	/* Advance past the opening '[' and following whitespace. */
	buf++;
	buf = skip_ws(buf, end);

	/* Is this an empty array? */
	if (buf == end)
		return (end);
	if (buf[0] == ']')
		return (&buf[1]);

	/* Skip entries until we get to the end. */
	do {
		/* Skip a value. */
		buf = skip_value(buf, end);

		/* Skip optional whitespace. */
		buf = skip_ws(buf, end);

		/* Are we at the end? */
		if (buf == end)
			return (end);
		if (buf[0] == ']')
			return (&buf[1]);

		/* Otherwise we should have a comma. */
		if (*buf++ != ',')
			return (end);
	} while (1);

	/* NOTREACHED */
}

/* Advance past object. */
static const uint8_t *
skip_object(const uint8_t * buf, const uint8_t * end)
{
	const uint8_t * stack[1024];
	int depth = 0;

	/* Advance past the opening '{' and following whitespace. */
	buf++;
	buf = skip_ws(buf, end);

	/* Is this an empty object? */
	if (buf == end)
		return (end);
	if (buf[0] == '}')
		return (&buf[1]);

	/* Skip entries until we get to the end. */
	do {
		/* Skip a string and optional whitespace. */
		buf = skip_string(buf, end);
		buf = skip_ws(buf, end);

		/* We should have a colon next. */
		if (buf == end)
			return (end);
		if (*buf++ != ':')
			return (end);

		/* Skip a whitespace, a value, and more whitespace. */
		buf = skip_ws(buf, end);
		buf = skip_value(buf, end);
		buf = skip_ws(buf, end);

		/* Are we at the end? */
		if (buf == end)
			return (end);
		if (buf[0] == '}')
			return (&buf[1]);

		/* Otherwise we should have a comma. */
		if (*buf++ != ',')
			return (end);
	} while (1);

	/* NOTREACHED */
}

/* Advance past a JSON value. */
static const uint8_t *
skip_value(const uint8_t * buf, const uint8_t * end)
{

	/* If there's nothing here, return. */
	if (buf == end)
		return (end);

	/* Handle different types of objects. */
	switch (buf[0]) {
	case 'f':
	case 'n':
	case 't':
		/* This must be a literal.  Skip it. */
		return (skip_literal(buf, end));
	case '"':
		/* This must be a string.  Skip it. */
		return (skip_string(buf, end));
	case '[':
		/* This must be an array.  Skip it. */
		return (skip_array(buf, end));
	case '{':
		/* This must be an object.  Skip it. */
		return (skip_object(buf, end));
	default:
		/* Could this plausibly be a number? */
		if (strchr(numchars, buf[0]) != NULL)
			return (skip_number(buf, end));

		/* We don't have a valid JSON value.  Return. */
		return (end);
	}
}

/* Advance to the end of the string.  Check if it matches. */
static const uint8_t *
match_str(const uint8_t * buf, const uint8_t * end, const char * s,
    int * foundit)
{
	char ch;

	/* The string matches... unless we notice that it doesn't match. */
	*foundit = 1;

	/* Scan through the string recording if it ever doesn't match. */
	do {
		if (buf == end)
			return (end);
		ch = (char)(*buf++);

		/* Have we hit the end of the string? */
		if (ch == '"') {
			if (s[0] != '\0')
				*foundit = 0;
			return (buf);
		}

		/* Escape character? */
		if (ch == '\\') {
			if (buf == end)
				return (end);
			switch (*buf++) {
			case '"':
				ch = '"';
				break;
			case '/':
				ch = '/';
				break;
			case '\\':
				ch = '\\';
				break;
			case 'b':
				ch = '\b';
				break;
			case 'f':
				ch = '\f';
				break;
			case 'n':
				ch = '\n';
				break;
			case 'r':
				ch = '\r';
				break;
			case 't':
				ch = '\t';
				break;
			case 'u':
				/*
				 * We don't support unicode escapes, so
				 * just skip the 4 hex digits.
				 */
				if (end - buf < 4)
					return (end);
				buf += 4;
				ch = 0;
				break;
			default:
				/* Invalid escape sequence. */
				return (end);
			}
		}

		/* Does this character match? */
		if (ch != *s++)
			*foundit = 0;
	} while (1);
}

/*
 * json_find(buf, end, s):
 * If there is a valid JSON object which starts at ${buf} and ends before or
 * at ${end} and said object contains a name/value pair with name ${s},
 * return a pointer to the associated value.  Otherwise, return ${end}.
 */
const uint8_t *
json_find(const uint8_t * buf, const uint8_t * end, const char * s)
{
	int foundit;

	/* Advance past the opening '{' and following whitespace. */
	if ((buf == end) || (*buf != '{'))
		return (end);
	buf++;
	buf = skip_ws(buf, end);

	/* Is this an empty object? */
	if (buf == end)
		return (end);
	if (*buf == '}')
		return (end);

	/* Skip entries until we get to the end. */
	do {
		/* Skip a string and optional whitespace. */
		buf = match_str(buf, end, s, &foundit);
		buf = skip_ws(buf, end);

		/* We should have a colon next. */
		if (buf == end)
			return (end);
		if (*buf++ != ':')
			return (end);

		/* Skip a whitespace. */
		buf = skip_ws(buf, end);

		/* If we found the string, return the value. */
		if (foundit)
			return (buf);

		/* Skip the value and more whitespace. */
		buf = skip_value(buf, end);
		buf = skip_ws(buf, end);

		/* Are we at the end? */
		if (buf == end)
			return (end);
		if (*buf == '}')
			return (end);

		/* Otherwise we should have a comma. */
		if (*buf++ != ',')
			return (end);
	} while (1);

	/* NOTREACHED */
}
