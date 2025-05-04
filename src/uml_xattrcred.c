#define _GNU_SOURCE
#include <stdio.h>
#include <sys/xattr.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/resource.h>
#include <stdint.h>
#include <unistd.h>

#define print_error_on_cwd(format, ...)						\
	{									\
		char *cwd = get_current_dir_name();				\
		if (cwd) {							\
			fprintf(stderr, format, cwd, __VA_ARGS__);		\
			free(cwd);						\
		}								\
		else {								\
			fprintf(stderr, format, "<unknown>", __VA_ARGS__);	\
		}								\
	}

void set_xattr(const char *filename)
{
	struct statx info;
	unsigned int mask = STATX_MODE | STATX_GID | STATX_UID;
	if (statx(AT_FDCWD, filename, AT_SYMLINK_NOFOLLOW, mask, &info)) {
		int saved_errno = errno;
		print_error_on_cwd("Failed to stat %s/%s: %s\n", filename, strerror(saved_errno));
		return;
	}
	if ((info.stx_mask & mask) != mask) {
		print_error_on_cwd("Failed to stat %s/%s: Filesystem didn't return full UID,GID,mode info\n", filename);
		return;
	}
	/* 10 digits uid, 10 digits gid, null byte and ',' */
	char umlcred[22];
	int cred_size = snprintf(umlcred, sizeof(umlcred), "%u,%u", info.stx_uid, info.stx_gid);
	if (cred_size >= sizeof(umlcred)) {
		print_error_on_cwd("On %s/%s got an unexpected size of umlcred attribute (%i > %i)\nThis shouldn't have happened. Aborting...\n"
			"(Was there some significant change in UID/GID since the time this program was written...?)\n", filename, cred_size+1, (int)sizeof(umlcred));
		exit(1);
	}
	char mode_string[] = "0000";	/* mode is 16-bit, but permissions only 12 bits, and we won't virtualize file format -> max 4 digits in octal */
	int i = sizeof(mode_string) - 1;
	info.stx_mode &= ~S_IFMT;
	while (info.stx_mode > 0 && i > 0) {
		mode_string[i] = (info.stx_mode % 8) + '0';
		info.stx_mode = info.stx_mode / 8;
		i--;
	}
	if (!i && info.stx_mode > 0) {
		print_error_on_cwd("On %s/%s got permissions in mode represented as more than 4 octal digits (got %i)\nThis shouldn't have happened. Aborting...\n"
			"(Was there some significant change in mode since the time this program was written...?)\n", filename, 4+i);
		exit(1);
	}
	if (setxattr(filename, "user.umlcred", umlcred, cred_size+1, 0)) {
		int saved_errno = errno;
		print_error_on_cwd("user.umlcred setxattr failed on %s/%s: %s\n", filename, strerror(saved_errno));
		return;
	}
	if (setxattr(filename, "user.umlmode", mode_string, sizeof(mode_string), 0)) {
		int saved_errno = errno;
		print_error_on_cwd("user.umlmode setxattr failed on %s/%s: %s\n", filename, strerror(saved_errno));
		return;
	}
}

void do_set_recursive_xattr(const char *path)
{
	if (chdir(path)) {
		int saved_errno = errno;
		print_error_on_cwd("Failed to enter %s/%s: %s\n", path, strerror(saved_errno));
		return;
	}

	DIR *curdir = opendir(".");
	if (!curdir) {
		int saved_errno = errno;
		print_error_on_cwd("Failed to open %s: %s\n", strerror(saved_errno));
		goto out;
	}
	struct dirent *d;
	errno = 0;
	unsigned long i = 0;
	while ((d = readdir(curdir))) {
		i++;
		if (d->d_type == DT_DIR) {
			if (!strcmp(d->d_name, "..")) {
				i--;
				continue;
			}
			if (!strcmp(d->d_name, "."))
				set_xattr(d->d_name);
			else
				do_set_recursive_xattr(d->d_name);
			continue;
		}
		if (d->d_type == DT_REG) {
			set_xattr(d->d_name);
			continue;
		}
		const char *type;
		switch (d->d_type) {
			case DT_BLK:
				type = "Block device";
				break;
			case DT_CHR:
				type = "Character device";
				break;
			case DT_FIFO:
				type = "Pipe";
				break;
			case DT_LNK:
				type = "Symlink";
				break;
			case DT_SOCK:
				type = "Socket";
				break;
			default:
				type = "Unknown type";
		}
		print_error_on_cwd("Skipping file %s/%s [%s]\n", d->d_name, type);
	}
	if (errno) {
		int saved_errno = errno;
		print_error_on_cwd("From %s processed %lu entries before failing to read: %s\n", i, strerror(saved_errno));
	}

	closedir(curdir);
out:
	if (chdir("..")) {
		int saved_errno = errno;
		perror("chdir(\"..\")");
		exit(saved_errno);
	}
}

void set_files_limit()
{
	struct rlimit64 rlim;
	rlim.rlim_cur = RLIM_INFINITY;
	rlim.rlim_max = RLIM_INFINITY;
	if (setrlimit64(RLIMIT_NOFILE, &rlim)) {
		if (getrlimit64(RLIMIT_NOFILE, &rlim)) {
			printf("[WARNING]: The maximum directory tree depth that will be processed without errors is: <unknown>\n"
				"It's equal to RLIMIT_NOFILE-3 that we can't fetch: %s\n", strerror(errno));
			return;
		}
		/*
		 * Assume we can't raise hard limit because we lack the capability
		 * Raise just the soft limit to match the hard limit
		 */
		rlim64_t effective_lim = rlim.rlim_cur;
		rlim.rlim_cur = rlim.rlim_max;
		if (!setrlimit64(RLIMIT_NOFILE, &rlim))
			effective_lim = rlim.rlim_max;
		printf("[WARNING]: The maximum directory tree depth that will be processed without errors is: %lu\n", effective_lim);
	}
}

int main(int argc, char *argv[])
{
	if (argc < 2) {
		fprintf(stderr, "Usage: uml_xattrcred <directory>\n");
		return 1;
	}
	if (!strcmp(argv[1], "--help")) {
		printf("Usage: uml_xattrcred <directory>\nConverts regular mode and ownership into extended attribute format used by UML (User-Mode Linux) hostfs.\n"
			"For details, check Documentation/virt/uml/user_mode_linux_howto_v2.rst\n");
		return 1;
	}
	set_files_limit();
	do_set_recursive_xattr(argv[1]);
	return 0;
}
