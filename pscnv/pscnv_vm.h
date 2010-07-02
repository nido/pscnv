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

#ifndef __PSCNV_VM_H__
#define __PSCNV_VM_H__

#include "pscnv_vram.h"
#include "pscnv_tree.h"

#define NV50_VM_SIZE		0x10000000000ULL
#define NV50_VM_PDE_COUNT	0x800
#define NV50_VM_SPTE_COUNT	0x20000
#define NV50_VM_LPTE_COUNT	0x2000

#define NVC0_SPAGE_SHIFT	12
#define NVC0_LPAGE_SHIFT	17
#define NVC0_SPAGE_MASK         0x00fff
#define NVC0_LPAGE_MASK         0x1ffff

#define NVC0_VM_PDE_COUNT	0x2000
#define NVC0_VM_BLOCK_SIZE	0x8000000
#define NVC0_VM_BLOCK_MASK	0x7ffffff
#define NVC0_VM_SPTE_COUNT	(NVC0_VM_BLOCK_SIZE >> NVC0_SPAGE_SHIFT)
#define NVC0_VM_LPTE_COUNT	(NVC0_VM_BLOCK_SIZE >> NVC0_LPAGE_SHIFT)

#define NVC0_PDE(a)		((a) / NVC0_VM_BLOCK_SIZE)
#define NVC0_SPTE(a)		(((a) & NVC0_VM_BLOCK_MASK) >> NVC0_SPAGE_SHIFT)
#define NVC0_LPTE(a)		(((a) & NVC0_VM_BLOCK_MASK) >> NVC0_LPAGE_SHIFT)

#define NVC0_PDE_HT_SIZE 32
#define NVC0_PDE_HASH(n) (n % NVC0_PDE_HT_SIZE)

PSCNV_RB_HEAD(pscnv_vm_maptree, pscnv_vm_mapnode);

struct pscnv_pgt {
	struct list_head head;
	unsigned int pde;
	unsigned int limit; /* virtual range = NVC0_VM_BLOCK_SIZE >> limit */
	struct pscnv_vo *vo[2]; /* 128 KiB and 4 KiB page tables */
};

struct pscnv_vspace {
	int vid;
	struct drm_device *dev;
	struct mutex lock;
	int isbar;
	struct list_head ptht[NVC0_PDE_HT_SIZE];
	struct pscnv_vo *pd;
	struct pscnv_vo *pt[NV50_VM_PDE_COUNT];
	struct list_head chan_list;
	struct pscnv_vm_maptree maps;
	struct drm_file *filp;
	int engines;
	struct kref ref;

	/* for PGRAPH, don't know what they do */
	struct pscnv_vm_mapnode *obj08004;
	struct pscnv_vm_mapnode *obj0800c;
	struct pscnv_vm_mapnode *obj19848;
	struct pscnv_vo *ctxsw_vo;
	struct pscnv_vm_mapnode *ctxsw_vm;
};

struct pscnv_vm_mapnode {
	PSCNV_RB_ENTRY(pscnv_vm_mapnode) entry;
	struct pscnv_vspace *vspace;
	/* NULL means free */
	struct pscnv_vo *vo;
	uint64_t start;
	uint64_t size;
	uint64_t maxgap;
};

#define PSCNV_ENGINE_PGRAPH 0x00000001

extern int pscnv_vm_init(struct drm_device *);
extern int nvc0_vm_init(struct drm_device *);
extern int pscnv_vm_takedown(struct drm_device *);
extern struct pscnv_vspace *pscnv_vspace_new(struct drm_device *);
extern void pscnv_vspace_free(struct pscnv_vspace *);
extern int pscnv_vspace_map(struct pscnv_vspace *, struct pscnv_vo *, uint64_t start, uint64_t end, int back, struct pscnv_vm_mapnode **res);
extern int pscnv_vspace_unmap(struct pscnv_vspace *, uint64_t start);
extern int pscnv_vspace_unmap_node(struct pscnv_vm_mapnode *node);
extern int pscnv_vspace_map1(struct pscnv_vo *);
extern int pscnv_vspace_map3(struct pscnv_vo *);
extern void pscnv_vspace_ref_free(struct kref *ref);

extern void pscnv_vspace_cleanup(struct drm_device *dev, struct drm_file *file_priv);
extern int pscnv_mmap(struct file *filp, struct vm_area_struct *vma);

int pscnv_ioctl_vspace_new(struct drm_device *dev, void *data,
						struct drm_file *file_priv);
int pscnv_ioctl_vspace_free(struct drm_device *dev, void *data,
						struct drm_file *file_priv);
int pscnv_ioctl_vspace_map(struct drm_device *dev, void *data,
						struct drm_file *file_priv);
int pscnv_ioctl_vspace_unmap(struct drm_device *dev, void *data,
						struct drm_file *file_priv);

/* from nvc0_vm.c */
int nvc0_bar3_flush(struct drm_device *dev);
int nvc0_tlb_flush(struct pscnv_vspace *vs);
int nvc0_vspace_do_unmap(struct pscnv_vspace *vs,
			 uint64_t offset, uint64_t size);
int nvc0_vspace_do_map(struct pscnv_vspace *vs,
		       struct pscnv_vo *vo, uint64_t offset);
int nvc0_vm_init(struct drm_device *dev);
int nvc0_vm_takedown(struct drm_device *dev);
void nvc0_pgt_del(struct pscnv_vspace *vs, struct pscnv_pgt *pgt);

/* needs vm_mutex held */
struct pscnv_vspace *pscnv_get_vspace(struct drm_device *dev, struct drm_file *file_priv, int vid);

void pscnv_vm_trap(struct drm_device *dev);

#endif
