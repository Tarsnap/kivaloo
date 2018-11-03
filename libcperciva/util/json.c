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
	return (buf);
}

/* Advance past array. */
static const uint8_t *
skip_array(const uint8_t * buf, const uint8_t * end)
{

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
			return(end);
	} while(1);

	/* NOTREACHED */
}

/* Advance past object. */
static const uint8_t *
skip_object(const uint8_t * buf, const uint8_t * end)
{

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
			return(end);
	} while(1);

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
			case '\\':
				ch = '\\';
				break;
			case '/':
				ch = '/';
				break;
			case 'b':
				ch = 0x08;
				break;
			case 'f':
				ch = 0x0C;
				break;
			case 'n':
				ch = 0x0A;
				break;
			case 'r':
				ch = 0x0D;
				break;
			case 't':
				ch = 0x09;
				break;
			case 'u':
				if (end - buf < 4)
					return (end);
				*foundit = 0;	/* Assume non-matching. */
				buf += 4;
				break;
			default:
				/* Invalid JSON. */
				*foundit = 0;
				return (end);
			}
		}

		/* Did this character match? */
		if (ch != s[0])
			*foundit = 0;

		/* Advance in the target if we haven't hit the end. */
		if (*s)
			s++;
	} while (1);

	/* NOTREACHED */
}

/* Helper for scanning for a character. */
#define SCAN(buf, end, ch) do {			\
	buf = skip_ws(buf, end);		\
	if (buf == end)				\
		return (end);			\
	if (*buf++ != ch)			\
		return (end);			\
} while (0);

/**
 * json_find(buf, end, s):
 * If there is a valid JSON object which starts at ${buf} and ends before or
 * at ${end} and said object contains a name/value pair with name ${s},
 * return a pointer to the associated value.  Otherwise, return ${end}.
 */
const uint8_t *
json_find(const uint8_t * buf, const uint8_t * end, const char * s)
{
	int foundit;

	/* After optional whitespace there should be a '{'. */
	SCAN(buf, end, '{');

	/* Scan the object looking for the child we want. */
	do {
		/*
		 * After optional whitespace we should have a '"' (unless
		 * the object is empty, in which case the key we're looking
		 * for is not present).
		 */
		SCAN(buf, end, '"');

		/* Is this the string we want? */
		buf = match_str(buf, end, s, &foundit);

		/* After optional whitespace we should have a ':'. */
		SCAN(buf, end, ':');

		/* Skip whitespace looking for the associated value. */
		buf = skip_ws(buf, end);

		/* Return the value if this is the one we wanted. */
		if (foundit)
			return (buf);

		/* Skip this JSON object. */
		buf = skip_value(buf, end);

		/*
		 * After optional whitespace we should have a ','.  (Or we
		 * could hit the closing '}' of the object, but that would
		 * mean that we don't have the key we're looking for anyway.)
		 */
		SCAN(buf, end, ',');
	} while (1);

	/* NOTREACHED */
}
