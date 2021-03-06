#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nv50_chan.h"
#include "pscnv_chan.h"
#include "nv50_vm.h"

int nv50_chan_new (struct pscnv_chan *ch) {
	struct pscnv_vspace *vs = ch->vspace;
	struct drm_nouveau_private *dev_priv = vs->dev->dev_private;
	uint64_t size;
	uint32_t chan_pd;
	int i;
	/* determine size of underlying VO... for normal channels,
	 * allocate 64kiB since they have to store the objects
	 * heap. for the BAR fake channel, we'll only need two objects,
	 * so keep it minimal
	 */
	if (!ch->isbar)
		size = 0x10000;
	else if (dev_priv->chipset == 0x50)
		size = 0x6000;
	else
		size = 0x5000;
	ch->vo = pscnv_vram_alloc(vs->dev, size, PSCNV_VO_CONTIG,
			0, (ch->isbar ? 0xc5a2ba7 : 0xc5a2f1f0));
	if (!ch->vo)
		return -ENOMEM;

	if (!vs->isbar)
		dev_priv->vm->map_kernel(ch->vo);

	if (dev_priv->chipset == 0x50)
		chan_pd = NV50_CHAN_PD;
	else
		chan_pd = NV84_CHAN_PD;
	for (i = 0; i < NV50_VM_PDE_COUNT; i++) {
		if (nv50_vs(vs)->pt[i]) {
			nv_wv32(ch->vo, chan_pd + i * 8 + 4, nv50_vs(vs)->pt[i]->start >> 32);
			nv_wv32(ch->vo, chan_pd + i * 8, nv50_vs(vs)->pt[i]->start | 0x3);
		} else {
			nv_wv32(ch->vo, chan_pd + i * 8, 0);
		}
	}
	ch->instpos = chan_pd + NV50_VM_PDE_COUNT * 8;

	if (!ch->isbar) {
		int i;
		ch->ramht.vo = ch->vo;
		ch->ramht.bits = 9;
		ch->ramht.offset = nv50_chan_iobj_new(ch, 8 << ch->ramht.bits);
		for (i = 0; i < (8 << ch->ramht.bits); i += 8)
			nv_wv32(ch->ramht.vo, ch->ramht.offset + i + 4, 0);

		if (dev_priv->chipset == 0x50) {
			ch->ramfc = 0;
		} else {
			/* actually, addresses of these two are NOT relative to
			 * channel struct on NV84+, and can be anywhere in VRAM,
			 * but we stuff them inside the channel struct anyway for
			 * simplicity. */
			ch->ramfc = nv50_chan_iobj_new(ch, 0x100);
			ch->cache = pscnv_vram_alloc(vs->dev, 0x1000, PSCNV_VO_CONTIG,
					0, 0xf1f0cace);
			if (!ch->cache) {
				pscnv_vram_free(ch->vo);
				return -ENOMEM;
			}
		}
	}
	dev_priv->vm->bar_flush(vs->dev);
	return 0;
}

void nv50_chan_init (struct pscnv_chan *ch) {
	struct drm_device *dev = ch->vspace->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	if (dev_priv->chipset != 0x50) {
		nv_wr32(dev, 0x2600 + ch->cid * 4, (ch->vo->start + ch->ramfc) >> 8);
	} else {
		nv_wr32(dev, 0x2600 + ch->cid * 4, ch->vo->start >> 12);
	}
}

int
nv50_chan_iobj_new(struct pscnv_chan *ch, uint32_t size) {
	/* XXX: maybe do this "properly" one day?
	 *
	 * Why we don't implement _del for instance objects:
	 *  - Usually, bounded const number of them is allocated
	 *    for any given channel, and the used set doesn't change
	 *    much during channel's lifetime
	 *  - Since instance objects are stored inside the main
	 *    VO of the channel, the storage will be freed on channel
	 *    close anyway
	 *  - We cannot easily tell what objects are currently in use
	 *    by PGRAPH and maybe other execution engines -- the user
	 *    could cheat us. Caching doesn't help either.
	 */
	int res;
	size += 0xf;
	size &= ~0xf;
	spin_lock(&ch->instlock);
	if (ch->instpos + size > ch->vo->size) {
		spin_unlock(&ch->instlock);
		return 0;
	}
	res = ch->instpos;
	ch->instpos += size;
	spin_unlock(&ch->instlock);
	return res;
}

/* XXX: we'll possibly want to break down type and/or add mysterious flags5
 * when we know more. */
int
nv50_chan_dmaobj_new(struct pscnv_chan *ch, uint32_t type, uint64_t start, uint64_t size) {
	struct drm_device *dev = ch->vspace->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint64_t end = start + size - 1;
	int res = nv50_chan_iobj_new (ch, 0x10);
	if (!res)
		return 0;
	nv_wv32(ch->vo, res + 0x00, type);
	nv_wv32(ch->vo, res + 0x04, end);
	nv_wv32(ch->vo, res + 0x08, start);
	nv_wv32(ch->vo, res + 0x0c, (end >> 32) << 24 | (start >> 32));
	dev_priv->vm->bar_flush(dev);
	return res;
}

