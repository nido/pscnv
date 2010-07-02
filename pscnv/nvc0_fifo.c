/*
 * Copyright (C) 2010 Christoph Bumiller.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_reg.h"
#include "pscnv_fifo.h"
#include "pscnv_chan.h"

static inline void
nvc0_fifo_init_reset(struct drm_device *dev)
{
	nv_wr32(dev, 0x200, nv_rd32(dev, 0x200) & 0xfffffeff);
	nv_wr32(dev, 0x200, nv_rd32(dev, 0x200) | 0x00000100);
}

static void
nvc0_fifo_init_intr(struct drm_device *dev)
{
	nv_wr32(dev, 0x2100, 0xffffffff); /* PFIFO_INTR */
	nv_wr32(dev, 0x2140, 0xffffffff); /* PFIFO_INTR_EN */
}

static int
nvc0_fifo_init_regs(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int i;
	uint32_t val;

	nv_wr32(dev, 0x0204, 7);
	nv_wr32(dev, 0x2204, 7);

	nv_wr32(dev, 0x12d0, 0x36); /* 0x32 on GTX 480 */

	nv_wr32(dev, NVC0_PFIFO_POLL_AREA,
		(1 << 28) | (dev_priv->fifo_vo->map1->start >> 12));

        for (i = 0; i < 12; ++i)
                nv_wr32(dev, 0x23e0 + i * 4, 0xff00);

        nv_wr32(dev, 0x23e0, 0x0400ff00);
        nv_wr32(dev, 0x23e4, 0x4d00ff00);
        for (i = 2; i < 12; ++i) {
                val = (i & 1) ? 0x0200 : 0x0100;
                nv_wr32(dev, 0x23e0 + i * 4, (val << 16) | 0xff00);
        }

	nvc0_fifo_init_intr(dev);

        nv_wr32(dev, 0x2208, 0xfffffffe);
        nv_wr32(dev, 0x220c, 0xfffffffd);
        nv_wr32(dev, 0x2210, 0xfffffffd);
        nv_wr32(dev, 0x2214, 0xfffffffd);
        nv_wr32(dev, 0x2218, 0xfffffffb);
        nv_wr32(dev, 0x221c, 0xfffffffd);

	nv_wr32(dev, 0x2a00, 0xffffffff);
	nv_wr32(dev, 0x2140, nv_rd32(dev, 0x2140) & ~0x40000000);
	nv_wr32(dev, 0x2200, nv_rd32(dev, 0x2200) | 1);
	nv_wr32(dev, 0x2628, 1);

	return 0;
}

int nvc0_fifo_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	NV_INFO(dev, "%s\n", __FUNCTION__);

	dev_priv->fifo_vo = pscnv_vram_alloc(dev, 128 * 0x1000,
					     PSCNV_VO_CONTIG, 0, 0xf1f03e95);

	if (!dev_priv->fifo_vo)
		return -ENOMEM;

	pscnv_vspace_map1(dev_priv->fifo_vo);

	if (!dev_priv->fifo_vo->map1)
		return -ENOMEM;
	dev_priv->fifo_ctl = ioremap(pci_resource_start(dev->pdev, 1) +
				     dev_priv->fifo_vo->map1->start, 128 << 12);
	if (!dev_priv->fifo_ctl)
		return -ENOMEM;

	dev_priv->playlist[0] = pscnv_vram_alloc(dev, 0x1000, PSCNV_VO_CONTIG,
						 0, 0x91a71157);
	dev_priv->playlist[1] = pscnv_vram_alloc(dev, 0x1000, PSCNV_VO_CONTIG,
						 0, 0x91a71158);

	if (!dev_priv->playlist[0] || !dev_priv->playlist[1])
		return -ENOMEM;

	pscnv_vspace_map3(dev_priv->playlist[0]);
	pscnv_vspace_map3(dev_priv->playlist[1]);
	
	nvc0_fifo_init_reset(dev);
	nvc0_fifo_init_regs(dev);

	return 0;
}

int nvc0_fifo_takedown(struct drm_device *dev)
{
	nv_wr32(dev, 0x2140, 0); /* PFIFO_INTR_EN */
	
	return 0;
}

void nvc0_fifo_playlist_update(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int i, n;
	struct pscnv_vo *vo;

	NV_INFO(dev, "%s\n", __FUNCTION__);

	dev_priv->cur_playlist = !dev_priv->cur_playlist;
	vo = dev_priv->playlist[dev_priv->cur_playlist];

	for (i = 0, n = 0; i < 128; ++i) {
		if (nv_rd32(dev, NVC0_PFIFO_CTX_STAT(i)) & 1) {
			nv_wv32(vo, n * 4 + 0, i);
			nv_wv32(vo, n * 4 + 4, 0x4);

			NV_INFO(dev, "playlist%i[%x] <- (%i, 0x4)\n",
				dev_priv->cur_playlist, n * 4, i);
			++n;
		}
	}
	nvc0_bar3_flush(dev);

	nv_wr32(dev, 0x2270, vo->start >> 12);
	nv_wr32(dev, 0x2274, 0x1f00000 | n);

	if (!nv_wait(0x227c, (1 << 20), 0))
		NV_WARN(dev, "WARNING: PFIFO 227c = 0x%08x\n",
			nv_rd32(dev, 0x227c));
}

void nvc0_fifo_channel_enable(struct drm_device *dev, struct pscnv_chan *ch)
{
	const uint64_t inst = ch->vo->start >> 12;

	nv_wr32(dev, NVC0_PFIFO_CTX_INST(ch->cid), (0xc << 28) | inst);
	nv_wr32(dev, NVC0_PFIFO_CTX_STAT(ch->cid), 0x1f0001);

	nvc0_fifo_playlist_update(dev);

	NV_INFO(dev, "FIFO %i enabled, status = 0x%08x\n", ch->cid,
		nv_rd32(dev, NVC0_PFIFO_CTX_STAT(ch->cid)));
}

void nvc0_fifo_channel_disable(struct drm_device *dev, struct pscnv_chan *ch)
{
	/* bit 28: active,
	 * bit 12: loaded,
	 * bit  0: enabled
	 */
	uint32_t status;

	if (!nv_wait(NVC0_PFIFO_CTX_STAT(ch->cid), (1 << 28), 0))
		NV_WARN(dev, "WARNING: FIFO %i still busy\n", ch->cid);

	status = nv_rd32(dev, NVC0_PFIFO_CTX_STAT(ch->cid));
	nv_wr32(dev, NVC0_PFIFO_CTX_STAT(ch->cid), status & ~1);

	nv_wr32(dev, 0x2634, ch->cid);
	if (!nv_wait(0x2634, ~0, ch->cid))
		NV_WARN(dev, "WARNING: 2634 = 0x%08x\n", nv_rd32(dev, 0x2634));

	nvc0_fifo_playlist_update(dev);

	nv_wr32(dev, 0x25a8, nv_rd32(dev, 0x25a8));
	nv_wr32(dev, NVC0_PFIFO_CTX_INST(ch->cid), 0);
}

#define nvchan_wr32(chan, ofst, val)					\
	dev_priv->fifo_ctl[((chan)->cid * 0x1000 + ofst) / 4] = val

int nvc0_fifo_create(struct drm_device *dev, struct pscnv_chan *chan,
		     struct drm_pscnv_fifo_init_ib *req)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int i, ret;
	uint64_t fifo_regs = dev_priv->fifo_vo->start + (chan->cid << 12);

	NV_INFO(dev, "%s\n", __FUNCTION__);

	if (req->ib_order > 29)
		return -EINVAL;

	for (i = 0x40; i <= 0x50; i += 4)
		nvchan_wr32(chan, i, 0);
	for (i = 0x58; i <= 0x60; i += 4)
		nvchan_wr32(chan, i, 0);
	nvchan_wr32(chan, 0x88, 0);
	nvchan_wr32(chan, 0x8c, 0);

	for (i = 0; i < 0x1000; i += 4)
		nv_wv32(chan->vo, i, 0);

	nv_wv32(chan->vo, 0x200, chan->vspace->pd->start);
	nv_wv32(chan->vo, 0x204, chan->vspace->pd->start >> 32);
	nv_wv32(chan->vo, 0x208, 0xffffffff);
	nv_wv32(chan->vo, 0x20c, 0xff);
	nvc0_bar3_flush(dev);

	nv_wv32(chan->vo, 0x08, fifo_regs);
	nv_wv32(chan->vo, 0x0c, fifo_regs >> 32);

	nv_wv32(chan->vo, 0x48, req->ib_start); /* IB */
	nv_wv32(chan->vo, 0x4c,
		(req->ib_start >> 32) | (req->ib_order << 16));
	nv_wv32(chan->vo, 0x10, 0xface);
	nv_wv32(chan->vo, 0x54, 0x2);
	nv_wv32(chan->vo, 0x9c, 0x100);
	nv_wv32(chan->vo, 0x84, 0x20400000);
	nv_wv32(chan->vo, 0x94, 0x30000001);
	nv_wv32(chan->vo, 0xa4, 0x1f1f1f1f);
	nv_wv32(chan->vo, 0xa8, 0x1f1f1f1f);
	nv_wv32(chan->vo, 0xac, 0x1f);
	nv_wv32(chan->vo, 0x30, 0xfffff902);
	/* nv_wv32(chan->vo, 0xb8, 0xf8000000); */ /* previously omitted */
	nv_wv32(chan->vo, 0xf8, 0x10003080);
	nv_wv32(chan->vo, 0xfc, 0x10000010);
	nvc0_bar3_flush(dev);

	nvc0_fifo_channel_enable(dev, chan);

	ret = nvc0_chan_init_grctx(dev, chan);
	if (ret)
		return ret;

	return 0;
}

void nvc0_fifo_chan_free(struct pscnv_chan *ch)
{
	nvc0_fifo_channel_disable(ch->vspace->dev, ch);
}

static const char *pgf_unit_str(int unit)
{
	switch (unit) {
	case 0: return "PGRAPH";
	case 3: return "PEEPHOLE";
	case 4: return "FB BAR";
	case 5: return "RAMIN BAR";
	case 7: return "PUSHBUF";
	default:
		break;
	}
	return "(unknown unit)";
}

static const char *pgf_cause_str(uint32_t flags)
{
	switch (flags & 0xf) {
	case 0x0: return "PDE not present";
	case 0x2: return "PTE not present";
	case 0x3: return "LIMIT exceeded";
	case 0x6: return "PTE set read-only";
	default:
		break;
	}
	return "unknown cause";
}

void nvc0_pfifo_page_fault(struct drm_device *dev, int unit)
{
	uint64_t virt;
	uint32_t chan, flags;

	chan = nv_rd32(dev, 0x2800 + unit * 0x10) << 12;
	virt = nv_rd32(dev, 0x2808 + unit * 0x10);
	virt = (virt << 32) | nv_rd32(dev, 0x2804 + unit * 0x10);
	flags = nv_rd32(dev, 0x280c + unit * 0x10);

	NV_INFO(dev, "%s PAGE FAULT at 0x%010llx (%c, %s)\n",
		pgf_unit_str(unit), virt,
		(flags & 0x80) ? 'w' : 'r', pgf_cause_str(flags));
}

void nvc0_pfifo_irq_handler(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t status;

	spin_lock(&dev_priv->pfifo_lock);
	status = nv_rd32(dev, 0x2100);

	if (status & 0x10000000) {
		uint32_t bits = nv_rd32(dev, 0x259c);
		uint32_t units = bits;

		while (units) {
			int i = ffs(units) - 1;
			units &= ~(1 << i);
			nvc0_pfifo_page_fault(dev, i);
		}
		nv_wr32(dev, 0x259c, bits); /* ack */
	}

	if (status & 0x00000100) {
		uint32_t ibpk[2];
		uint32_t data = nv_rd32(dev, 0x400c4);

		ibpk[0] = nv_rd32(dev, 0x40110);
		ibpk[1] = nv_rd32(dev, 0x40114);

		NV_INFO(dev, "PFIFO FUCKUP: DATA = 0x%08x\n"
			"IB PACKET = 0x%08x 0x%08x\n", data, ibpk[0], ibpk[1]);
	}

	if (status & 0xeffffeff) {
		NV_INFO(dev, "unknown PFIFO INTR: 0x%08x\n",
			status & 0xeffffeff);
	}

	/* disable interrupts */
	nv_wr32(dev, 0x2140, 0);
	spin_unlock(&dev_priv->pfifo_lock);
}
