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
	nv_wr32(dev, 0x100cbc, 0x80000000 | ((vs->isbar == 3) ? 0x5 : 0x1));

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
nvc0_vspace_fill_pde(struct pscnv_vspace *vs, struct pscnv_pgt *pgt)
{
	const uint32_t size = NVC0_VM_SPTE_COUNT << (3 - pgt->limit);
	int i;
	uint32_t pde[2];

	pgt->vo[1] = pscnv_vram_alloc(vs->dev, size, PSCNV_VO_CONTIG, 0, 0x59);
	if (!pgt->vo[1])
		return -ENOMEM;

	for (i = 0; i < size; i += 4)
		nv_wv32(pgt->vo[1], i, 0);

	pde[0] = pgt->limit << 2;
	pde[1] = (pgt->vo[1]->start >> 8) | 1;

	if (vs->isbar != 3) {
		pgt->vo[0] = pscnv_vram_alloc(vs->dev, NVC0_VM_LPTE_COUNT * 8,
					      PSCNV_VO_CONTIG, 0, 0x79);
		if (!pgt->vo[0])
			return -ENOMEM;

		pscnv_vspace_map3(pgt->vo[0]);
		pscnv_vspace_map3(pgt->vo[1]);

		for (i = 0; i < NVC0_VM_LPTE_COUNT * 8; i += 4)
			nv_wv32(pgt->vo[0], i, 0);

		pde[0] |= (pgt->vo[0]->start >> 8) | 1;
	}
	nvc0_bar3_flush(vs->dev);

	nv_wv32(vs->pd, pgt->pde * 8 + 0, pde[0]);
	nv_wv32(vs->pd, pgt->pde * 8 + 4, pde[1]);

	nvc0_bar3_flush(vs->dev);
	return nvc0_tlb_flush(vs);
}

static struct pscnv_pgt *
nvc0_vspace_pgt(struct pscnv_vspace *vs, unsigned int pde)
{
	struct pscnv_pgt *pt;
	struct list_head *pts = &vs->ptht[NVC0_PDE_HASH(pde)];

	BUG_ON(pde >= NVC0_VM_PDE_COUNT);

	list_for_each_entry(pt, pts, head)
		if (pt->pde == pde)
			return pt;

	NV_DEBUG(vs->dev, "creating new page table: %i[%u]\n", vs->vid, pde);

	pt = kzalloc(sizeof(struct pscnv_pgt), GFP_KERNEL);
	if (!pt)
		return NULL;
	pt->pde = pde;
	pt->limit = 0;

	if (nvc0_vspace_fill_pde(vs, pt)) {
		kfree(pt);
		return NULL;
	}

	list_add_tail(&pt->head, pts);
	return pt;
}

void
nvc0_pgt_del(struct pscnv_vspace *vs, struct pscnv_pgt *pgt)
{
	pscnv_vram_free(pgt->vo[1]);
	if (pgt->vo[0])
		pscnv_vram_free(pgt->vo[0]);
	list_del(&pgt->head);

	nv_wv32(vs->pd, pgt->pde * 8 + 0, 0);
	nv_wv32(vs->pd, pgt->pde * 8 + 4, 0);

	kfree(pgt);
}

int
nvc0_vspace_do_unmap(struct pscnv_vspace *vs, uint64_t offset, uint64_t size)
{
	uint32_t space;

	for (; size; offset += space) {
		struct pscnv_pgt *pt;
		int i, pte;

		pt = nvc0_vspace_pgt(vs, NVC0_PDE(offset));
		space = NVC0_VM_BLOCK_SIZE - (offset & NVC0_VM_BLOCK_MASK);
		if (space > size)
			space = size;
		size -= space;

		pte = NVC0_SPTE(offset);
		for (i = 0; i < (space >> NVC0_SPAGE_SHIFT) * 8; i += 4)
			nv_wv32(pt->vo[1], pte * 8 + i, 0);

		if (!pt->vo[0])
			continue;

		pte = NVC0_LPTE(offset);
		for (i = 0; i < (space >> NVC0_LPAGE_SHIFT) * 8; i += 4)
			nv_wv32(pt->vo[0], pte * 8 + i, 0);
	}
	nvc0_bar3_flush(vs->dev);
	return nvc0_tlb_flush(vs);
}

static inline void
write_pt(struct pscnv_vo *pt, int pte, int count, uint64_t phys,
	 int psz, uint32_t pfl0, uint32_t pfl1)
{
	int i;
	uint32_t a = (phys >> 8) | pfl0;
	uint32_t b = pfl1;

	psz >>= 8;

	for (i = pte * 8; i < (pte + count) * 8; i += 8, a += psz) {
		nv_wv32(pt, i + 4, b);
		nv_wv32(pt, i + 0, a);
	}
}

int
nvc0_vspace_do_map(struct pscnv_vspace *vs,
		   struct pscnv_vo *vo, uint64_t offset)
{
	uint32_t pfl0, pfl1;
	struct pscnv_vram_region *reg;

	pfl0 = 1;
	if (!vs->isbar && (vo->flags & PSCNV_VO_NOUSER))
		pfl0 |= 2;

	pfl1 = vo->tile_flags << 4;

	NV_DEBUG(vs->dev, "nvc0_vspace_do_map(%p, 0x%010llx)\n", vs, offset);

	list_for_each_entry(reg, &vo->regions, local_list) {
		uint32_t psh, psz;
		uint64_t phys = reg->start, size = reg->size;

		int s = ((size | offset | phys) & NVC0_LPAGE_MASK) ? 1 : 0;
		if (vs->isbar == 3)
			s = 1;
	        psh = s ? NVC0_SPAGE_SHIFT : NVC0_LPAGE_SHIFT;
		psz = 1 << psh;

		NV_DEBUG(vs->dev,
			 "VS %i 0x%010llx <-> (0x%08llx, %llx) (%x, %u)\n",
			 vs->vid, offset, phys, reg->size, vo->tile_flags, psh);

		while (size) {
			struct pscnv_pgt *pt;
			int pte, count;
			uint32_t space;

			space = NVC0_VM_BLOCK_SIZE -
				(offset & NVC0_VM_BLOCK_MASK);
			if (space > size)
				space = size;
			size -= space;

			pte = (offset & NVC0_VM_BLOCK_MASK) >> psh;
			count = space >> psh;
			pt = nvc0_vspace_pgt(vs, NVC0_PDE(offset));

			write_pt(pt->vo[s], pte, count, phys, psz, pfl0, pfl1);

			offset += space;
			phys += space;
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
	struct pscnv_pgt *pt;

	if (!barvm)
		return -ENOMEM;
	barvm->isbar = 3;
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

	pt = nvc0_vspace_pgt(barvm, 0);
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
	barvm->isbar = 1;
	dev_priv->bar1_vm = barvm;

	barch = pscnv_chan_new(barvm);
	if (!barch) {
		pscnv_vspace_free(barvm);
		return -ENOMEM;
	}
	dev_priv->bar1_ch = barch;

	pscnv_vspace_map3(barch->vo);

	nv_wv32(barch->vo, 0x200, barvm->pd->start);
	nv_wv32(barch->vo, 0x204, barvm->pd->start >> 32);
	nv_wv32(barch->vo, 0x208, dev_priv->fb_size - 1);
	nv_wv32(barch->vo, 0x20c, (dev_priv->fb_size - 1) >> 32);

	nv_wr32(dev, NVC0_PBUS_BAR1_CHAN,
		(0x8 << 28) | (barch->vo->start >> 12));
	nvc0_bar3_flush(dev);

	nv_wr32(dev, 0x1140, 0x8c0001fe);
	nv_wr32(dev, 0x88484, nv_rd32(dev, 0x88484) | 0x10);
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
	struct pscnv_vspace *vs1 = dev_priv->bar1_vm;
	struct pscnv_vspace *vs3 = dev_priv->bar3_vm;
	struct pscnv_chan *ch1 = dev_priv->bar1_ch;
	struct pscnv_chan *ch3 = dev_priv->bar3_ch;

	dev_priv->bar1_vm = dev_priv->bar3_vm = NULL;
	dev_priv->bar1_ch = dev_priv->bar3_ch = NULL;

	nv_wr32(dev, NVC0_PBUS_BAR1_CHAN, 0);
	nv_wr32(dev, NVC0_PBUS_BAR3_CHAN, 0);

	pscnv_chan_free(ch1);
	pscnv_vspace_free(vs1);
	pscnv_chan_free(ch3);
	pscnv_vspace_free(vs3);

	return 0;
}
