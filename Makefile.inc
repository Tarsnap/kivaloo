# Used by Makefile code which generates POSIX Makefiles
.for X in ${SRCS}
source-${X:.c=.o}: ${X}
	@echo $>
cflags-${X:.c=.o}:
.endfor

# Used to track the subdirectory depth (other directories append to this)
SUBDIR_DEPTH	=	..

# Expand ${IDIRS} before it is used
IDIRS	:=	${IDIRS}

# Defaults for all kivaloo code
WARNS	?=	3

# Use POSIX standard
CFLAGS	+=	-D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700

# Make logic (bits which are separated out in individual Makefiles).
CFLAGS	+=	${IDIRS}
LDADD	+=	${LDADD_REQ}

# No man pages here
MAN	=
