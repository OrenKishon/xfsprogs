#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "xfs.h"

#define NBSTAT 		4069

static void bulkstat_scan(
	char			*fsdir)
{
	xfs_fsop_bulkreq_t	bulkreq;
	xfs_bstat_t		*buf;
	__u64			last = 0;
	__s32			count;
	int			fsfd, sts;
	static int total; 

	fsfd = open(fsdir, O_RDONLY);
	if (fsfd < 0) {
		perror(fsdir);
		return;
	}

	buf = (xfs_bstat_t *)calloc(NBSTAT, sizeof(xfs_bstat_t));
	if (!buf) {
		perror("calloc");
		close(fsfd);
		return;
	}

	bulkreq.lastip = &last;
	bulkreq.icount = NBSTAT;
	bulkreq.ubuffer = buf;
	bulkreq.ocount = &count;

	while ((sts = xfsctl(fsdir, fsfd, XFS_IOC_FSBULKSTAT, &bulkreq)) == 0) {
		if (count == 0)
			break;
		total += count;
	}
	if (sts < 0)
		perror("XFS_IOC_FSBULKSTAT");

	printf("total number of inodes = %d\n", total);

	free(buf);
	close(fsfd);
}

int main(int argc, char *argv[])
{
	if (argc < 2)
	{
		printf("No path arg\n");
		return 1;
	}

	bulkstat_scan(argv[1]);

	return 0;
}

