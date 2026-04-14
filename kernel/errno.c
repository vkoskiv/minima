#include <errno.h>

static const char *error_strings[] = {
	[EPERM]   = "Not permitted",
	[EINVAL]  = "Invalid argument",
	[ENOENT]  = "No such file/directory",
	[ENOSYS]  = "Not implemented",
	[EEXIST]  = "File exists",
	[ENODEV]  = "No such device",
	[EBADF]   = "Bad file descriptor",
	[EISDIR]  = "Is a directory",
	[EIO]     = "I/O error",
	[ENODATA] = "No data available",
	[ENOTSUP] = "Not supported",
	[ENOMEM]  = "Cannot allocate memory",
	[ENOTDIR] = "Not a directory",
};

const char *strerror(int errno) {
	return error_strings[errno < 0 ? -errno : errno];
}
