/*
 * Copyright 2010 Christoph Bumiller.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_reg.h"
#include "pscnv_vram.h"
#include "pscnv_vm.h"
#include "pscnv_chan.h"

int
nvc0_tlb_flush(struct pscnv_vspace *vs)
{
	struct drm_device *dev = vs->dev;
	uint32_t val;

	BUG_ON(!vs->pd);

	NV_DEBUG(dev, "nvc0_tlb_flush 0x%010llx\n", vs->pd->start);

	val = nv_rd32(dev, 0x100c80);

	nv_wr32(dev, 0x100cb8, vs->pd->start >> 8);
	nv_wr32(dev, 0x100cbc, 0x80000000 | (vs->isbar ? 0x5 : 0x1));

	if (!nv_wait(0x100c80, ~0, val)) {
		NV_ERROR(vs->dev, "tlb flush timed out\n");
		return -EBUSY;
	}
	return 0;
}

int
nvc0_bar3_flush(struct drm_device *dev)
{
	nv_wr32(dev, 0x70000, 1);
	if (!nv_wait(0x70000, ~0, 0)) {
		NV_ERROR(dev, "RAMIN flush timed out\n");
		return -EBUSY;
	}
	return 0;
}

static int
nvc0_vspace_fill_pde(struct pscnv_vspace *vs, unsigned int pde);

static struct pscnv_ptab *
nvc0_vspace_ptab(struct pscnv_vspace *vs, unsigned int pde)
{
	struct pscnv_ptab *pt;
	struct list_head *pts = &vs->ptht[NVC0_PDE_HASH(pde)];

	BUG_ON(pde >= ((1ULL << 40) / (128ULL << 20)));

	list_for_each_entry(pt, pts, head)
		if (pt->pde == pde)
			return pt;

	NV_DEBUG(vs->dev, "creating new page table: %i[%u]\n", vs->vid, pde);

	pt = kzalloc(sizeof(struct pscnv_ptab), GFP_KERNEL);
	if (!pt)
		return NULL;
	pt->pde = pde;
	list_add_tail(&pt->head, pts);

	if (nvc0_vspace_fill_pde(vs, pde))
		return NULL;
	return pt;
}

static int
nvc0_vspace_fill_pde(struct pscnv_vspace *vs, unsigned int pde)
{
	struct pscnv_ptab *pt = nvc0_vspace_ptab(vs, pde);
	int i;

	if (!pt)
		return -ENOMEM;

	if (!vs->isbar) {
		pt->vo[0] = pscnv_vram_alloc(vs->dev, NVC0_VM_LPTE_COUNT * 8,
					     PSCNV_VO_CONTIG, 0, 0x77779999);
		if (!pt->vo[0])
			return -ENOMEM;
	}

	pt->vo[1] = pscnv_vram_alloc(vs->dev, NVC0_VM_SPTE_COUNT * 8,
				     PSCNV_VO_CONTIG, 0, 0x55559999);
	if (!pt->vo[1])
		return -ENOMEM;

	if (!vs->isbar) {
		pscnv_vspace_map3(pt->vo[0]);
		pscnv_vspace_map3(pt->vo[1]);
	}

	if (pt->vo[0]) {
		for (i = 0; i < NVC0_VM_LPTE_COUNT * 8; i += 4)
			nv_wv32(pt->vo[0], i, 0);
		nvc0_bar3_flush(vs->dev);
	}

	for (i = 0; i < NVC0_VM_SPTE_COUNT * 8; i += 4)
		nv_wv32(pt->vo[1], i, 0);
	nvc0_bar3_flush(vs->dev);

	if (vs->isbar)
		nv_wv32(vs->pd, pde * 8 + 0, 0x0);
	else
		nv_wv32(vs->pd, pde * 8 + 0, (pt->vo[0]->start >> 8) | 1);
	nv_wv32(vs->pd, pde * 8 + 4, (pt->vo[1]->start >> 8) | 1);

	nvc0_bar3_flush(vs->dev);
	return nvc0_tlb_flush(vs);
}

int
nvc0_vspace_do_unmap(struct pscnv_vspace *vs, uint64_t offset, uint64_t size)
{
	const uint64_t end = offset + size;
	uint32_t space;

	for (; offset < end; offset += space) {
		struct pscnv_ptab *pt;
		int i, pte;
		const int pde = offset / NVC0_VM_BLOCK_SIZE;

		pt = nvc0_vspace_ptab(vs, pde);
		space = NVC0_VM_BLOCK_SIZE - (offset & NVC0_VM_BLOCK_MASK);

		pte = (offset & NVC0_VM_BLOCK_MASK) >> NVC0_SPAGE_SHIFT;
		for (i = 0; i < (space >> NVC0_SPAGE_SHIFT) * 8; i += 4)
			nv_wv32(pt->vo[1], pte * 8 + i, 0);

		if (!pt->vo[0])
			continue;

		pte = (offset & NVC0_VM_BLOCK_MASK) >> NVC0_LPAGE_SHIFT;
		for (i = 0; i < (space >> NVC0_LPAGE_SHIFT) * 8; i += 4)
			nv_wv32(pt->vo[0], pte * 8 + i, 0);
	}
	nvc0_bar3_flush(vs->dev);
	return nvc0_tlb_flush(vs);
}

int
nvc0_vspace_do_map(struct pscnv_vspace *vs,
		   struct pscnv_vo *vo, uint64_t offset)
{
	struct pscnv_vram_region *reg;
	int pfl;

	pfl = 1;
	if (!vs->isbar && (vo->flags & PSCNV_VO_NOUSER))
		pfl |= 2;

	NV_DEBUG(vs->dev, "nvc0_vspace_do_map(%p, 0x%010llx)\n", vs, offset);

	list_for_each_entry(reg, &vo->regions, local_list) {
		uint32_t psh, psz;
		int s;
		uint64_t phys = reg->start, phys_end = reg->start + reg->size;

		s = ((reg->size | offset | phys) & NVC0_LPAGE_MASK) ? 1 : 0;
		if (vs->isbar)
			s = 1;
	        psh = s ? NVC0_SPAGE_SHIFT : NVC0_LPAGE_SHIFT;
		psz = 1 << psh;

		NV_DEBUG(vs->dev,
			 "VS %i 0x%010llx <-> (0x%08llx, %llx) (%x, %u)\n",
			 vs->vid, offset, phys, reg->size, vo->tile_flags, psh);

		for (phys = reg->start; phys < phys_end; phys += psz) {
			struct pscnv_ptab *pt;
			int pte = (offset & NVC0_VM_BLOCK_MASK) >> psh;
			int pde = offset / NVC0_VM_BLOCK_SIZE;

			pt = nvc0_vspace_ptab(vs, pde);

			nv_wv32(pt->vo[s], pte * 8 + 0, (phys >> 8) | pfl);
			nv_wv32(pt->vo[s], pte * 8 + 4, vo->tile_flags);

			offset += psz;
		}
	}
	nvc0_bar3_flush(vs->dev);
	return nvc0_tlb_flush(vs);
}

static int
nvc0_vm_init_bar3(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_vspace *barvm = pscnv_vspace_new(dev);
	struct pscnv_chan *barch;
	struct pscnv_ptab *pt;

	if (!barvm)
		return -ENOMEM;
	barvm->isbar = 1;
	dev_priv->bar3_vm = barvm;

	barch = pscnv_chan_new(barvm);
	if (!barch) {
		pscnv_vspace_free(barvm);
		return -ENOMEM;
	}
	dev_priv->bar3_ch = barch;

	nv_wv32(barch->vo, 0x200, barvm->pd->start);
	nv_wv32(barch->vo, 0x204, barvm->pd->start >> 32);
	nv_wv32(barch->vo, 0x208, dev_priv->ramin_size - 1);
	nv_wv32(barch->vo, 0x20c, 0);

	nv_wr32(dev, NVC0_PBUS_BAR3_CHAN,
		(0xc << 28) | (barch->vo->start >> 12));
	nvc0_bar3_flush(dev);

	pt = nvc0_vspace_ptab(barvm, 0);
	if (!pt) {
		NV_ERROR(dev, "failed to allocate RAMIN page table\n");
		return -ENOMEM;
	}
	pscnv_vspace_map3(pt->vo[1]);

	return 0;
}

static int
nvc0_vm_init_bar1(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct pscnv_vspace *barvm = pscnv_vspace_new(dev);
	struct pscnv_chan *barch;

	if (!barvm)
		return -ENOMEM;
	barvm->isbar = 0; /* interpret as isbar3 for now */
	dev_priv->bar1_vm = barvm;

	barch = pscnv_chan_new(barvm);
	if (!barch) {
		pscnv_vspace_free(barvm);
		return -ENOMEM;
	}
	dev_priv->bar1_ch = barch;

	nv_wv32(barch->vo, 0x200, barvm->pd->start);
	nv_wv32(barch->vo, 0x204, barvm->pd->start >> 32);
	nv_wv32(barch->vo, 0x208, dev_priv->fb_size - 1);
	nv_wv32(barch->vo, 0x20c, (dev_priv->fb_size - 1) >> 32);

	nv_wr32(dev, NVC0_PBUS_BAR1_CHAN,
		(0x8 << 28) | (barch->vo->start >> 12));
	nvc0_bar3_flush(dev);

	nv_wr32(dev, 0x1140, 0x8c0001fe);
	nv_wr32(dev, 0x88484, nv_rd32(dev, 88484) | 0x10);
	nv_wr32(dev, 0x1c00, 0x42);

	return 0;
}

int
nvc0_vm_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int ret;

	mutex_init(&dev_priv->vm_mutex);

	dev_priv->vm_ramin_base = 0;

	ret = nvc0_vm_init_bar3(dev);
	if (ret) {
		NV_ERROR(dev, "failed to setup BAR 3 (RAMIN)\n");
		return ret;
	}

	ret = nvc0_vm_init_bar1(dev);
	if (ret) {
		NV_ERROR(dev, "failed to setup BAR 1 (FB)\n");
		return ret;
	}

	NV_INFO(dev, "VM init successful.\n");
	return 0;
}

int
nvc0_vm_takedown(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	dev_priv->bar1_vm = dev_priv->bar3_vm = NULL;
	dev_priv->bar1_ch = dev_priv->bar3_ch = NULL;

	nv_wr32(dev, NVC0_PBUS_BAR1_CHAN, 0);
	nv_wr32(dev, NVC0_PBUS_BAR3_CHAN, 0);

	pscnv_chan_free(dev_priv->bar1_ch);
	pscnv_chan_free(dev_priv->bar3_ch);
	pscnv_vspace_free(dev_priv->bar1_vm);
	pscnv_vspace_free(dev_priv->bar3_vm);

	return 0;
}
