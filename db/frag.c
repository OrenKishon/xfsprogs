/*
 * Copyright (c) 2000-2001 Silicon Graphics, Inc.  All Rights Reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * 
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 * 
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 * 
 * http://www.sgi.com 
 * 
 * For further information regarding this notice, see: 
 * 
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

#include <libxfs.h>
#include <sys/time.h>
#include "bmap.h"
#include "command.h"
#include "data.h"
#include "frag.h"
#include "io.h"
#include "output.h"
#include "type.h"
#include "mount.h"
#include "malloc.h"

typedef struct extent {
	xfs_fileoff_t	startoff;
	xfs_filblks_t	blockcount;
} extent_t;

typedef	struct extmap {
	int		naents;
	int		nents;
	extent_t	ents[1];
} extmap_t;
#define	EXTMAP_SIZE(n)	\
	(offsetof(extmap_t, ents) + (sizeof(extent_t) * (n)))

static int		aflag;
static int		dflag;
static __uint64_t	extcount_actual;
static __uint64_t	extcount_ideal;
static int		fflag;
static int		lflag;
static int		qflag;
static int		Rflag;
static int		rflag;
static int		vflag;

typedef void	(*scan_lbtree_f_t)(xfs_btree_lblock_t	*block,
				   int			level,
				   extmap_t		**extmapp,
				   typnm_t		btype);

typedef void	(*scan_sbtree_f_t)(xfs_btree_sblock_t	*block,
				   int			level,
				   xfs_agf_t		*agf);

static extmap_t		*extmap_alloc(xfs_extnum_t nex);
static xfs_extnum_t	extmap_ideal(extmap_t *extmap);
static void		extmap_set_ext(extmap_t **extmapp, xfs_fileoff_t o,
				       xfs_extlen_t c);
static int		frag_f(int argc, char **argv);
static int		init(int argc, char **argv);
static void		process_bmbt_reclist(xfs_bmbt_rec_32_t *rp, int numrecs,
					     extmap_t **extmapp);
static void		process_btinode(xfs_dinode_t *dip, extmap_t **extmapp,
					int whichfork);
static void		process_exinode(xfs_dinode_t *dip, extmap_t **extmapp,
					int whichfork);
static void		process_fork(xfs_dinode_t *dip, int whichfork);
static void		process_inode(xfs_agf_t *agf, xfs_agino_t agino,
				      xfs_dinode_t *dip);
static void		scan_ag(xfs_agnumber_t agno);
static void		scan_lbtree(xfs_fsblock_t root, int nlevels,
				    scan_lbtree_f_t func, extmap_t **extmapp,
				    typnm_t btype);
static void		scan_sbtree(xfs_agf_t *agf, xfs_agblock_t root,
				    int nlevels, scan_sbtree_f_t func,
				    typnm_t btype);
static void		scanfunc_bmap(xfs_btree_lblock_t *ablock, int level,
				      extmap_t **extmapp, typnm_t btype);
static void		scanfunc_ino(xfs_btree_sblock_t *ablock, int level,
				     xfs_agf_t *agf);

static const cmdinfo_t	frag_cmd = 
	{ "frag", NULL, frag_f, 0, -1, 0,
	  "[-a] [-d] [-f] [-l] [-r]",
	  "get file fragmentation data", NULL };

static extmap_t *
extmap_alloc(
	xfs_extnum_t	nex)
{
	extmap_t	*extmap;

	if (nex < 1)
		nex = 1;
	extmap = xmalloc(EXTMAP_SIZE(nex));
	extmap->naents = nex;
	extmap->nents = 0;
	return extmap;
}

static xfs_extnum_t
extmap_ideal(
	extmap_t	*extmap)
{
	extent_t	*ep;
	xfs_extnum_t	rval;

	for (ep = &extmap->ents[0], rval = 0;
	     ep < &extmap->ents[extmap->nents];
	     ep++) {
		if (ep == &extmap->ents[0] ||
		    ep->startoff != ep[-1].startoff + ep[-1].blockcount)
			rval++;
	}
	return rval;
}

static void
extmap_set_ext(
	extmap_t	**extmapp,
	xfs_fileoff_t	o,
	xfs_extlen_t	c)
{
	extmap_t	*extmap;
	extent_t	*ent;

	extmap = *extmapp;
	if (extmap->nents == extmap->naents) {
		extmap->naents++;
		extmap = xrealloc(extmap, EXTMAP_SIZE(extmap->naents));
		*extmapp = extmap;
	}
	ent = &extmap->ents[extmap->nents];
	ent->startoff = o;
	ent->blockcount = c;
	extmap->nents++;
}

void
frag_init(void)
{
	add_command(&frag_cmd);
}

/*
 * Get file fragmentation information.
 */
static int
frag_f(
	int		argc,
	char		**argv)
{
	xfs_agnumber_t	agno;
	double		answer;

	if (!init(argc, argv))
		return 0;
	for (agno = 0; agno < mp->m_sb.sb_agcount; agno++)
		scan_ag(agno);
	if (extcount_actual)
		answer = (double)(extcount_actual - extcount_ideal) * 100.0 /
			 (double)extcount_actual;
	else
		answer = 0.0;
	dbprintf("actual %llu, ideal %llu, fragmentation factor %.2f%%\n",
		extcount_actual, extcount_ideal, answer);
	return 0;
}

static int
init(
	int		argc,
	char		**argv)
{
	int		c;

	aflag = dflag = fflag = lflag = qflag = Rflag = rflag = vflag = 0;
	optind = 0;
	while ((c = getopt(argc, argv, "adflqRrv")) != EOF) {
		switch (c) {
		case 'a':
			aflag = 1;
			break;
		case 'd':
			dflag = 1;
			break;
		case 'f':
			fflag = 1;
			break;
		case 'l':
			lflag = 1;
			break;
		case 'q':
			qflag = 1;
			break;
		case 'R':
			Rflag = 1;
			break;
		case 'r':
			rflag = 1;
			break;
		case 'v':
			vflag = 1;
			break;
		default:
			dbprintf("bad option for frag command\n");
			return 0;
		}
	}
	if (!aflag && !dflag && !fflag && !lflag && !qflag && !Rflag && !rflag)
		aflag = dflag = fflag = lflag = qflag = Rflag = rflag = 1;
	extcount_actual = extcount_ideal = 0;
	return 1;
}

static void
process_bmbt_reclist(
	xfs_bmbt_rec_32_t	*rp,
	int			numrecs,
	extmap_t		**extmapp)
{
	xfs_dfilblks_t		c;
	int			f;
	int			i;
	xfs_dfiloff_t		o;
	xfs_dfsbno_t		s;

	for (i = 0; i < numrecs; i++, rp++) {
		convert_extent((xfs_bmbt_rec_64_t *)rp, &o, &s, &c, &f);
		extmap_set_ext(extmapp, (xfs_fileoff_t)o, (xfs_extlen_t)c);
	}
}

static void
process_btinode(
	xfs_dinode_t		*dip,
	extmap_t		**extmapp,
	int			whichfork)
{
	xfs_bmdr_block_t	*dib;
	int			i;
	xfs_bmbt_ptr_t		*pp;
	xfs_bmbt_rec_32_t	*rp;

	dib = (xfs_bmdr_block_t *)XFS_DFORK_PTR(dip, whichfork);
	if (INT_GET(dib->bb_level, ARCH_CONVERT) == 0) {
		rp = (xfs_bmbt_rec_32_t *)XFS_BTREE_REC_ADDR(
			XFS_DFORK_SIZE(dip, mp, whichfork),
			xfs_bmdr, dib, 1,
			XFS_BTREE_BLOCK_MAXRECS(XFS_DFORK_SIZE(dip, mp,
					whichfork),
				xfs_bmdr, 1));
		process_bmbt_reclist(rp, INT_GET(dib->bb_numrecs, ARCH_CONVERT), extmapp);
		return;
	}
	pp = XFS_BTREE_PTR_ADDR(XFS_DFORK_SIZE(dip, mp, whichfork),
		xfs_bmdr, dib, 1,
		XFS_BTREE_BLOCK_MAXRECS(XFS_DFORK_SIZE(dip, mp, whichfork),
					xfs_bmdr, 0));
	for (i = 0; i < INT_GET(dib->bb_numrecs, ARCH_CONVERT); i++)
		scan_lbtree((xfs_fsblock_t)INT_GET(pp[i], ARCH_CONVERT), INT_GET(dib->bb_level, ARCH_CONVERT), scanfunc_bmap,
			extmapp,
			whichfork == XFS_DATA_FORK ? TYP_BMAPBTD : TYP_BMAPBTA);
}

static void
process_exinode(
	xfs_dinode_t		*dip,
	extmap_t		**extmapp,
	int			whichfork)
{
	xfs_bmbt_rec_32_t	*rp;

	rp = (xfs_bmbt_rec_32_t *)XFS_DFORK_PTR(dip, whichfork);
	process_bmbt_reclist(rp, XFS_DFORK_NEXTENTS(dip, whichfork), extmapp);
}

static void
process_fork(
	xfs_dinode_t	*dip,
	int		whichfork)
{
	extmap_t	*extmap;
	int		nex;

	nex = XFS_DFORK_NEXTENTS(dip, whichfork);
	if (!nex)
		return;
	extmap = extmap_alloc(nex);
	switch (XFS_DFORK_FORMAT(dip, whichfork)) {
	case XFS_DINODE_FMT_EXTENTS:
		process_exinode(dip, &extmap, whichfork);
		break;
	case XFS_DINODE_FMT_BTREE:
		process_btinode(dip, &extmap, whichfork);
		break;
	}
	extcount_actual += extmap->nents;
	extcount_ideal += extmap_ideal(extmap);
	xfree(extmap);
}

static void
process_inode(
	xfs_agf_t		*agf,
	xfs_agino_t		agino,
	xfs_dinode_t		*dip)
{
	__uint64_t		actual;
	xfs_dinode_core_t	*dic;
	__uint64_t		ideal;
	xfs_ino_t		ino;
	int			skipa;
	int			skipd;

	dic = &dip->di_core;
	ino = XFS_AGINO_TO_INO(mp, INT_GET(agf->agf_seqno, ARCH_CONVERT), agino);
	switch (dic->di_mode & IFMT) {
	case IFDIR:
		skipd = !dflag;
		break;
	case IFREG:
		if (!rflag && (dic->di_flags & XFS_DIFLAG_REALTIME))
			skipd = 1;
		else if (!Rflag &&
			 (ino == mp->m_sb.sb_rbmino ||
			  ino == mp->m_sb.sb_rsumino))
			skipd = 1;
		else if (!qflag &&
			 (ino == mp->m_sb.sb_uquotino ||
			  ino == mp->m_sb.sb_gquotino))
			skipd = 1;
		else
			skipd = !fflag;
		break;
	case IFLNK:
		skipd = !lflag;
		break;
	default:
		skipd = 1;
		break;
	}
	actual = extcount_actual;
	ideal = extcount_ideal;
	if (!skipd)
		process_fork(dip, XFS_DATA_FORK);
	skipa = !aflag || !XFS_DFORK_Q(dip);
	if (!skipa)
		process_fork(dip, XFS_ATTR_FORK);
	if (vflag && (!skipd || !skipa))
		dbprintf("inode %lld actual %lld ideal %lld\n",
			ino, extcount_actual - actual, extcount_ideal - ideal);
}

static void
scan_ag(
	xfs_agnumber_t	agno)
{
	xfs_agf_t	*agf;
	xfs_agi_t	*agi;

	push_cur();
	set_cur(&typtab[TYP_AGF],
		XFS_AG_DADDR(mp, agno, XFS_AGF_DADDR(mp)),
		XFS_FSS_TO_BB(mp, 1), DB_RING_IGN, NULL);
	if ((agf = iocur_top->data) == NULL) {
		dbprintf("can't read agf block for ag %u\n", agno);
		pop_cur();
		return;
	}
	push_cur();
	set_cur(&typtab[TYP_AGI],
		XFS_AG_DADDR(mp, agno, XFS_AGI_DADDR(mp)),
		XFS_FSS_TO_BB(mp, 1), DB_RING_IGN, NULL);
	if ((agi = iocur_top->data) == NULL) {
		dbprintf("can't read agi block for ag %u\n", agno);
		pop_cur();
		pop_cur();
		return;
	}
	scan_sbtree(agf,
		INT_GET(agi->agi_root, ARCH_CONVERT),
		INT_GET(agi->agi_level, ARCH_CONVERT),
		scanfunc_ino, TYP_INOBT);
	pop_cur();
	pop_cur();
}

static void
scan_lbtree(
	xfs_fsblock_t	root,
	int		nlevels,
	scan_lbtree_f_t	func,
	extmap_t	**extmapp,
	typnm_t		btype)
{
	push_cur();
	set_cur(&typtab[btype], XFS_FSB_TO_DADDR(mp, root), blkbb, DB_RING_IGN,
		NULL);
	if (iocur_top->data == NULL) {
		dbprintf("can't read btree block %u/%u\n",
			XFS_FSB_TO_AGNO(mp, root),
			XFS_FSB_TO_AGBNO(mp, root));
		return;
	}
	(*func)(iocur_top->data, nlevels - 1, extmapp, btype);
	pop_cur();
}

static void
scan_sbtree(
	xfs_agf_t	*agf,
	xfs_agblock_t	root,
	int		nlevels,
	scan_sbtree_f_t	func,
	typnm_t		btype)
{
	xfs_agnumber_t	seqno = INT_GET(agf->agf_seqno, ARCH_CONVERT);

	push_cur();
	set_cur(&typtab[btype], XFS_AGB_TO_DADDR(mp, seqno, root),
		blkbb, DB_RING_IGN, NULL);
	if (iocur_top->data == NULL) {
		dbprintf("can't read btree block %u/%u\n", seqno, root);
		return;
	}
	(*func)(iocur_top->data, nlevels - 1, agf);
	pop_cur();
}

static void
scanfunc_bmap(
	xfs_btree_lblock_t	*ablock,
	int			level,
	extmap_t		**extmapp,
	typnm_t			btype)
{
	xfs_bmbt_block_t	*block = (xfs_bmbt_block_t *)ablock;
	int			i;
	xfs_bmbt_ptr_t		*pp;
	xfs_bmbt_rec_32_t	*rp;

	if (level == 0) {
		rp = (xfs_bmbt_rec_32_t *)
			XFS_BTREE_REC_ADDR(mp->m_sb.sb_blocksize, xfs_bmbt,
			block, 1, mp->m_bmap_dmxr[0]);
		process_bmbt_reclist(rp, INT_GET(block->bb_numrecs, ARCH_CONVERT), extmapp);
		return;
	}
	pp = XFS_BTREE_PTR_ADDR(mp->m_sb.sb_blocksize, xfs_bmbt, block, 1,
		mp->m_bmap_dmxr[0]);
	for (i = 0; i < INT_GET(block->bb_numrecs, ARCH_CONVERT); i++)
		scan_lbtree(INT_GET(pp[i], ARCH_CONVERT), level, scanfunc_bmap, extmapp, btype);
}

static void
scanfunc_ino(
	xfs_btree_sblock_t	*ablock,
	int			level,
	xfs_agf_t		*agf)
{
	xfs_agino_t		agino;
	xfs_inobt_block_t	*block = (xfs_inobt_block_t *)ablock;
	xfs_agnumber_t		seqno = INT_GET(agf->agf_seqno, ARCH_CONVERT);
	int			i;
	int			j;
	int			off;
	xfs_inobt_ptr_t		*pp;
	xfs_inobt_rec_t		*rp;

	if (level == 0) {
		rp = XFS_BTREE_REC_ADDR(mp->m_sb.sb_blocksize, xfs_inobt, block,
			1, mp->m_inobt_mxr[0]);
		for (i = 0; i < INT_GET(block->bb_numrecs, ARCH_CONVERT); i++) {
			agino = INT_GET(rp[i].ir_startino, ARCH_CONVERT);
			off = XFS_INO_TO_OFFSET(mp, agino);
			push_cur();
			set_cur(&typtab[TYP_INODE],
				XFS_AGB_TO_DADDR(mp, seqno,
						 XFS_AGINO_TO_AGBNO(mp, agino)),
				XFS_FSB_TO_BB(mp, XFS_IALLOC_BLOCKS(mp)),
				DB_RING_IGN, NULL);
			if (iocur_top->data == NULL) {
				dbprintf("can't read inode block %u/%u\n",
					seqno, XFS_AGINO_TO_AGBNO(mp, agino));
				continue;
			}
			for (j = 0; j < XFS_INODES_PER_CHUNK; j++) {
                                xfs_dinode_t            *dip;
                                xfs_dinode_core_t       tdic;
                                
                                dip=(xfs_dinode_t *)((char *)iocur_top->data + ((off + j) << mp->m_sb.sb_inodelog));
                            
                                /* convert the core, then copy it back into the inode */
	                        libxfs_xlate_dinode_core( (xfs_caddr_t)
					&dip->di_core, &tdic, 1, ARCH_CONVERT );
	                        memcpy(&dip->di_core, &tdic, sizeof(xfs_dinode_core_t));
        
				if (XFS_INOBT_IS_FREE(&rp[i], j, ARCH_CONVERT))
					continue;
				process_inode(agf, agino + j,
					(xfs_dinode_t *)((char *)iocur_top->data + ((off + j) << mp->m_sb.sb_inodelog)));
			}
			pop_cur();
		}
		return;
	}
	pp = XFS_BTREE_PTR_ADDR(mp->m_sb.sb_blocksize, xfs_inobt, block, 1,
		mp->m_inobt_mxr[1]);
	for (i = 0; i < INT_GET(block->bb_numrecs, ARCH_CONVERT); i++)
		scan_sbtree(agf, INT_GET(pp[i], ARCH_CONVERT), level, scanfunc_ino, TYP_INOBT);
}
