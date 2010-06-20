/*
 * Copyright (C) 2007 Ben Skeggs.
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

int
nvc0_mc_init(struct drm_device *dev)
{
	uint32_t PMC_units;

	nv_wr32(dev, NV03_PMC_INTR_EN_0, 0);

	NV_INFO(dev, "PCI ID: 0x%08x\n", nv_rd32(dev, NVC0_PBUS_PCI+0x00));
	NV_INFO(dev, "BAR[0]: 0x%08x\n", nv_rd32(dev, NVC0_PBUS_PCI+0x10));
	NV_INFO(dev, "BAR[1]: 0x%08x\n", nv_rd32(dev, NVC0_PBUS_PCI+0x14));
	NV_INFO(dev, "BAR[2]: 0x%08x\n", nv_rd32(dev, NVC0_PBUS_PCI+0x18));
	NV_INFO(dev, "BAR[3]: 0x%08x\n", nv_rd32(dev, NVC0_PBUS_PCI+0x1c));
	NV_INFO(dev, "BAR[4]: 0x%08x\n", nv_rd32(dev, NVC0_PBUS_PCI+0x20));

	nv_wr32(dev, NVC0_PBUS_PCI+0x4, nv_rd32(dev, NVC0_PBUS_PCI+0x4) & ~4);

	nv_wr32(dev, NV03_PMC_ENABLE, 0xe010203d); /* PMEDIA, PFB, PDISPLAY */
	nv_wr32(dev, NV03_PMC_ENABLE, 0xffffffff);
	PMC_units = nv_rd32(dev, NV03_PMC_ENABLE);

	NV_INFO(dev, "PMC units: 0x%08x\n", PMC_units);

	nv_wr32(dev, NVC0_PBUS_PCI+0x4, nv_rd32(dev, NVC0_PBUS_PCI+0x4) | 4);
	nv_wr32(dev, NVC0_PBUS_PCI+0xc,
		nv_rd32(dev, NVC0_PBUS_PCI+0xc) | 0xf800);

	nv_wr32(dev, 0x01100, nv_rd32(dev, 0x01100) | 8);
	nv_wr32(dev, 0x01140, 0x8c0001fe);

	nv_wr32(dev, 0x70010, 1);
	nv_rd32(dev, 0x70010);

	nv_wr32(dev, 0x100c10, 0x12ae630);
	nv_wr32(dev, 0x100c80, nv_rd32(dev, 0x100c80) & ~1);
	nv_wr32(dev, 0x100b44, nv_rd32(dev, 0x100b44) | 0x00010001);

	return 0;
}

int
nv50_mc_init(struct drm_device *dev)
{
	nv_wr32(dev, NV03_PMC_ENABLE, 0xFFFFFFFF);
	nv_wr32(dev, 0x1140, 0xFFFFFFFF);
	return 0;
}

void nv50_mc_takedown(struct drm_device *dev)
{
}
