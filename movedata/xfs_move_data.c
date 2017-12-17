#include <xfs/xfs.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <attr/xattr.h>

#define DError printf

static int xfs_bulkstat_single(int fd, xfs_ino_t *lastip, xfs_bstat_t *ubuffer)
{
	xfs_fsop_bulkreq_t bulkreq;

	bulkreq.lastip = (__u64 *)lastip;
	bulkreq.icount = 1;
	bulkreq.ubuffer = ubuffer;
	bulkreq.ocount = NULL;
	int ret = ioctl(fd, XFS_IOC_FSBULKSTAT_SINGLE, &bulkreq);

	return ret;
}

static int xfs_stat(int fd, xfs_bstat_t *statp)
{
	struct stat64 tstatbuf;

	if (fstat64(fd, &tstatbuf) < 0)
	{
		DError("XFSUtils: stat64 : %s\n", strerror(errno));
		return -1;
	}

	xfs_ino_t ino = tstatbuf.st_ino;

	if (xfs_bulkstat_single(fd, &ino, statp) < 0)
	{
		DError("XFSUtils: xfs_bulkstat_single: %s\n", strerror(errno));
		return 1;
	}

	return 0;
}

static int copy_xattr(int fd_src, int fd_dst)
{
	char *list, *value = NULL;
	ssize_t listlen, valuelen = 0;
	const char *name;

	listlen = flistxattr(fd_src, NULL, 0);

	if (listlen < 0)
	{
		printf("flistxattr: %s", strerror(errno));
		return -1;
	}

	list = malloc(listlen);

	listlen = flistxattr(fd_src, list, listlen);

	if (listlen < 0)
	{
		printf("flistxattr: %s", strerror(errno));
		return -1;
	}

	for (name = list; name < list + listlen; name += strlen(name) + 1)
	{
		ssize_t curlen = fgetxattr(fd_src, name, NULL, 0);

		if (curlen < 0)
		{
			printf("fgetxattr: %s", strerror(errno));
			return -1;
		}

		if (curlen > valuelen)
		{
			value = realloc(value, curlen);
			valuelen = curlen;
		}

		valuelen = fgetxattr(fd_src, name, value, sizeof(value));

		if (valuelen < 0)
		{
			printf("fgetxattr: %s", strerror(errno));
			return -1;
		}

		if (fsetxattr(fd_dst, name, value, valuelen, 0) < 0)
		{
			printf("fsetxattr: %s", strerror(errno));
			return -1;
		}
	}

	return 0;
}

static int xfs_swapext(int fd, xfs_swapext_t *sx)
{
	return ioctl(fd, XFS_IOC_SWAPEXT, sx);
}

static int init_source(const char *src, xfs_bstat_t *statp)
{
	int fd;

	if ((fd = open(src, O_EXCL|O_RDWR|O_DIRECT, 0666)) < 0)
	{
		DError("XFSUtils: open %s: %s\n", src, strerror(errno));
		return -1;
	}

	if (fsync(fd) < 0)
       	{
		printf("XFSUtils: fsync %s : %s\n", src, strerror(errno));
		close(fd);
		return -1;
	}		

	if (xfs_stat(fd, statp) < 0)
	{
		close(fd);
		return -1;
	}

	return fd;
}

static int init_dest(const char *dst, const xfs_bstat_t bstat)
{
	int fd;

	if ((fd = open(dst, O_EXCL|O_RDWR|O_DIRECT, 0666)) < 0)
	{
		DError("XFSUtils: open %s: %s\n", dst, strerror(errno));
		return -1;
	}

	printf("%d\n", fd);

	if (ftruncate(fd, bstat.bs_size) < 0)
       	{
		DError("XFSUtils: ftruncate %s: %s\n", dst, strerror(errno));
		close(fd);
		return -1;
	}
	printf("%d\n", fd);

	return fd;
}

static int swapext(const char *src, const char *dst)
{
	int fd_dst, fd_src; 
	int ret = -1;
	xfs_bstat_t bstat;
	xfs_swapext_t sx;
	int srval;

	if ((fd_src = init_source(src, &bstat)) < 0)
	{
		return -1;
	}

	if ((fd_dst = init_dest(dst, bstat)) < 0)
	{
		close(fd_src);
		return -1;
	}
	printf("%d %d\n", fd_src, fd_dst);

	copy_xattr(fd_src, fd_dst);

	/* In xfs_fsr fdtarget is a file to be defragged and the fdtmp
	 * is a temp defragged copy to be swapped back to fdtarget.
	 * What's important is that sx_stat holds the stat of fdtarget.
	 */
	sx.sx_stat = bstat; /* struct copy */
	sx.sx_version = XFS_SX_VERSION;
	sx.sx_fdtarget = fd_src;
	sx.sx_fdtmp = fd_dst;
	sx.sx_offset = 0;
	sx.sx_length = bstat.bs_size;

	/* Swap the extents */
	srval = xfs_swapext(fd_src, &sx);

	if (srval < 0)
	{
		if (errno == ENOTSUP)
	       	{
			DError("XFSUtils: %s: file type not supported\n", src);
		}
	       	else if (errno == EFAULT)
	       	{
			/* The file has changed since we started the copy */
			DError("XFSUtils: %s: file modified\n", src);
		}
	       	else if (errno == EBUSY)
	       	{
			/* Timestamp has changed or mmap'ed file */
			DError("XFSUtils: %s: file busy\n", src);
		}
	       	else 
		{
			DError("XFSUtils: XFS_IOC_SWAPEXT failed: %s: %s\n",
				  src, strerror(errno));
		}
		goto out;
	}

	ret = 0;

out:
	close(fd_dst);
	close(fd_src);

	return ret;
}

int main(int argc, char **argv)
{
	if (argc < 3)
	{
		printf("usage\n");
		return 1;
	}

	const char *src = argv[1];
	const char *dst = argv[2];

	swapext(src, dst);

	return 0;
}
