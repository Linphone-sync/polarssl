#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])
AC_INIT([polarssl], [1.3.4], [polarssl_maintainer@polarssl.org])
AC_CONFIG_SRCDIR([library/ssl_cache.c])

AM_INIT_AUTOMAKE
AM_SILENT_RULES(yes)

# Checks for programs.
AC_PROG_CC

LT_INIT(win32-dll disable-static)

dnl Workaround for mingw, whose compiler doesn't check in /usr/include ...
case "$target_os" in
	*mingw*)
		if test "$prefix" = "/usr" ; then
			CPPFLAGS="$CPPFLAGS -I/usr/include"
			LDFLAGS="$LDFLAGS -L/usr/lib"
		fi
	;;
esac


# Checks for libraries.

AC_CHECK_LIB([crypto], [crypto_init])
# FIXME: Replace `main' with a function in `-lssl':
AC_CHECK_LIB([ssl_library_init], [main])

# FIXME: Replace `main' with a function in `-lz':
#AC_CHECK_LIB([z], [main])

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h float.h inttypes.h netdb.h netinet/in.h stdint.h stdlib.h string.h sys/socket.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T

# Checks for library functions.
AC_FUNC_FORK
AC_CHECK_FUNCS([alarm gethostbyname gethostname gettimeofday memmove memset select socket strcasecmp strchr strstr])

AC_CONFIG_FILES([Makefile
	include/Makefile
	include/polarssl/Makefile
	library/Makefile
])
AC_OUTPUT
