/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2010 PathScale Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef __PSCNV_CHAN_H__
#define __PSCNV_CHAN_H__

#include "pscnv_vm.h"
#include "pscnv_ramht.h"
#include <linux/kref.h>

#define NV50_CHAN_PD	0x1400
#define NV84_CHAN_PD	0x0200

struct pscnv_chan {
	int cid;
	struct pscnv_vspace *vspace;
	int isbar;
	struct list_head vspace_list;
	struct pscnv_vo *vo;
	spinlock_t instlock;
	int instpos;
	struct pscnv_ramht ramht;
	uint32_t ramfc;
	struct pscnv_vo *cache;
	struct drm_file *filp;
	int engines;
	struct pscnv_vo *grctx;
	struct pscnv_vm_mapnode *grctx_vm;
	struct kref ref;
};

extern struct pscnv_chan *pscnv_chan_new(struct pscnv_vspace *);
extern void pscnv_chan_free(struct pscnv_chan *);
extern int pscnv_chan_iobj_new(struct pscnv_chan *, uint32_t size);
extern int pscnv_chan_dmaobj_new(struct pscnv_chan *, uint32_t type, uint64_t start, uint64_t size);

extern void pscnv_chan_cleanup(struct drm_device *dev, struct drm_file *file_priv);
extern int pscnv_chan_mmap(struct file *filp, struct vm_area_struct *vma);
struct pscnv_chan *pscnv_get_chan(struct drm_device *dev, struct drm_file *file_priv, int cid);

extern int nvc0_chan_init_grctx(struct drm_device *dev, struct pscnv_chan *chan);

int pscnv_ioctl_chan_new(struct drm_device *dev, void *data,
						struct drm_file *file_priv);
int pscnv_ioctl_chan_free(struct drm_device *dev, void *data,
						struct drm_file *file_priv);
int pscnv_ioctl_obj_vdma_new(struct drm_device *dev, void *data,
						struct drm_file *file_priv);

#endif
