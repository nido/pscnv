/*
 * Copyright 2005 Stephane Marchesin
 * Copyright 2008 Stuart Bennett
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
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <linux/swab.h>
#include <linux/slab.h>
#include "drmP.h"
#include "drm.h"
#include "drm_sarea.h"
#include "drm_crtc_helper.h"
#include <linux/vgaarb.h>
#include <linux/vga_switcheroo.h>

#include "nouveau_drv.h"
#include "pscnv_drm.h"
#include "nouveau_reg.h"
#include "nv50_display.h"
#include "pscnv_vm.h"
#include "pscnv_chan.h"

static unsigned int
nouveau_vga_set_decode(void *priv, bool state)
{
	struct drm_device *dev = priv;
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (dev_priv->chipset >= 0x40)
		nv_wr32(dev, 0x88054, state);
	else
		nv_wr32(dev, 0x1854, state);

	if (state)
		return VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM |
		       VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
	else
		return VGA_RSRC_NORMAL_IO | VGA_RSRC_NORMAL_MEM;
}

static void nouveau_switcheroo_set_state(struct pci_dev *pdev,
					 enum vga_switcheroo_state state)
{
	pm_message_t pmm = { .event = PM_EVENT_SUSPEND };
	if (state == VGA_SWITCHEROO_ON) {
		printk(KERN_ERR "VGA switcheroo: switched nouveau on\n");
		nouveau_pci_resume(pdev);
	} else {
		printk(KERN_ERR "VGA switcheroo: switched nouveau off\n");
		nouveau_pci_suspend(pdev, pmm);
	}
}

static bool nouveau_switcheroo_can_switch(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	bool can_switch;

	spin_lock(&dev->count_lock);
	can_switch = (dev->open_count == 0);
	spin_unlock(&dev->count_lock);
	return can_switch;
}

int
nouveau_card_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int ret;
	int i;

	NV_DEBUG(dev, "prev state = %d\n", dev_priv->init_state);

	if (dev_priv->init_state == NOUVEAU_CARD_INIT_DONE)
		return 0;

	NV_INFO(dev, "Initializing card...\n");

	vga_client_register(dev->pdev, dev, NULL, nouveau_vga_set_decode);
	vga_switcheroo_register_client(dev->pdev, nouveau_switcheroo_set_state,
				       nouveau_switcheroo_can_switch);


	/* Initialise internal driver API hooks */
	dev_priv->init_state = NOUVEAU_CARD_INIT_FAILED;
	spin_lock_init(&dev_priv->irq_lock);

	/* Parse BIOS tables / Run init tables if card not POSTed */
	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		ret = nouveau_bios_init(dev);
		if (ret)
			goto out;
	}

	ret = pscnv_vram_init(dev);
	if (ret)
		goto out_bios;

	ret = nv50_vm_init(dev);
	if (ret)
		goto out_vram;

	/* PMC */
	nv_wr32(dev, NV03_PMC_ENABLE, 0xFFFFFFFF);

	/* PBUS */
	nv_wr32(dev, 0x1100, 0xFFFFFFFF);
	nv_wr32(dev, 0x1140, 0xFFFFFFFF);

	/* PTIMER */
	ret = nv04_timer_init(dev);
	if (ret)
		goto out_vm;

	/* XXX: handle noaccel */
	/* PFIFO */
	ret = nv50_fifo_init(dev);
	if (!ret) {
		/* PGRAPH */
		nv50_graph_init(dev);
	}

	/* this call irq_preinstall, register irq handler and
	 * call irq_postinstall
	 */
	ret = drm_irq_install(dev);
	if (ret)
		goto out_timer;

	ret = drm_vblank_init(dev, 0);
	if (ret)
		goto out_irq;

	/* what about PVIDEO/PCRTC/PRAMDAC etc? */
#if 0
	if (!engine->graph.accel_blocked) {
		ret = nouveau_card_init_channel(dev);
		if (ret)
			goto out_irq;
	}
#endif
	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		if (dev_priv->card_type >= NV_50)
			ret = nv50_display_create(dev);
		else
			ret = /* nv04_display_create(dev)*/ -ENOSYS;
		if (ret)
			goto out_channel;
	}

	ret = nouveau_backlight_init(dev);
	if (ret)
		NV_ERROR(dev, "Error %d registering backlight\n", ret);

	dev_priv->init_state = NOUVEAU_CARD_INIT_DONE;

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		drm_helper_initial_config(dev);

	NV_INFO(dev, "Card initialized.\n");
	return 0;

out_channel:
#if 0
	if (dev_priv->channel) {
		nouveau_channel_free(dev_priv->channel);
		dev_priv->channel = NULL;
	}
#endif
out_irq:
	drm_irq_uninstall(dev);
	for (i = 0; i < PSCNV_ENGINES_NUM; i++)
		if (dev_priv->engines[i]) {
			dev_priv->engines[i]->takedown(dev_priv->engines[i]);
			dev_priv->engines[i] = 0;
		}
out_timer:
out_vm:
	dev_priv->vm->takedown(dev);
out_vram:
	pscnv_vram_takedown(dev);
out_bios:
	nouveau_bios_takedown(dev);
out:
	vga_client_register(dev->pdev, NULL, NULL, NULL);
	return ret;
}

static void nouveau_card_takedown(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int i;

	NV_DEBUG(dev, "prev state = %d\n", dev_priv->init_state);

	if (dev_priv->init_state != NOUVEAU_CARD_INIT_DOWN) {
		NV_INFO(dev, "Stopping card...\n");
		nouveau_backlight_exit(dev);
		drm_irq_uninstall(dev);
		for (i = 0; i < PSCNV_ENGINES_NUM; i++)
			if (dev_priv->engines[i]) {
				dev_priv->engines[i]->takedown(dev_priv->engines[i]);
				dev_priv->engines[i] = 0;
			}
		dev_priv->vm->takedown(dev);
		pscnv_vram_takedown(dev);
		nouveau_bios_takedown(dev);

		vga_client_register(dev->pdev, NULL, NULL, NULL);

		dev_priv->init_state = NOUVEAU_CARD_INIT_DOWN;
		NV_INFO(dev, "Card stopped.\n");
	}
}

/* here a client dies, release the stuff that was allocated for its
 * file_priv */
void nouveau_preclose(struct drm_device *dev, struct drm_file *file_priv)
{
	pscnv_chan_cleanup(dev, file_priv);
	pscnv_vspace_cleanup(dev, file_priv);
}

/* first module load, setup the mmio/fb mapping */
/* KMS: we need mmio at load time, not when the first drm client opens. */
int nouveau_firstopen(struct drm_device *dev)
{
	nouveau_card_init(dev);
	return 0;
}

/* if we have an OF card, copy vbios to RAMIN */
static void nouveau_OF_copy_vbios_to_ramin(struct drm_device *dev)
{
#if defined(__powerpc__)
	int size, i;
	const uint32_t *bios;
	struct device_node *dn = pci_device_to_OF_node(dev->pdev);
	if (!dn) {
		NV_INFO(dev, "Unable to get the OF node\n");
		return;
	}

	bios = of_get_property(dn, "NVDA,BMP", &size);
	if (bios) {
		for (i = 0; i < size; i += 4)
			nv_wi32(dev, i, bios[i/4]);
		NV_INFO(dev, "OF bios successfully copied (%d bytes)\n", size);
	} else {
		NV_INFO(dev, "Unable to get the OF bios\n");
	}
#endif
}

int nouveau_load(struct drm_device *dev, unsigned long flags)
{
	struct drm_nouveau_private *dev_priv;
	uint32_t reg0;
	resource_size_t mmio_start_offs;

	dev_priv = kzalloc(sizeof(*dev_priv), GFP_KERNEL);
	if (!dev_priv)
		return -ENOMEM;
	dev->dev_private = dev_priv;
	dev_priv->dev = dev;

	dev_priv->flags = flags/* & NOUVEAU_FLAGS*/;
	dev_priv->init_state = NOUVEAU_CARD_INIT_DOWN;

	NV_DEBUG(dev, "vendor: 0x%X device: 0x%X class: 0x%X\n",
		 dev->pci_vendor, dev->pci_device, dev->pdev->class);

	dev_priv->wq = create_workqueue("nouveau");
	if (!dev_priv->wq)
		return -EINVAL;

	/* resource 0 is mmio regs */
	/* resource 1 is linear FB */
	/* resource 2 is RAMIN (mmio regs + 0x1000000) */
	/* resource 6 is bios */

	/* map the mmio regs */
	mmio_start_offs = pci_resource_start(dev->pdev, 0);
	dev_priv->mmio = ioremap(mmio_start_offs, 0x00800000);
	if (!dev_priv->mmio) {
		NV_ERROR(dev, "Unable to initialize the mmio mapping. "
			 "Please report your setup to " DRIVER_EMAIL "\n");
		return -EINVAL;
	}
	NV_DEBUG(dev, "regs mapped ok at 0x%llx\n",
					(unsigned long long)mmio_start_offs);

#ifdef __BIG_ENDIAN
	/* Put the card in BE mode if it's not */
	if (nv_rd32(dev, NV03_PMC_BOOT_1))
		nv_wr32(dev, NV03_PMC_BOOT_1, 0x00000001);

	DRM_MEMORYBARRIER();
#endif

	/* Time to determine the card architecture */
	reg0 = nv_rd32(dev, NV03_PMC_BOOT_0);

	/* We're dealing with >=NV10 */
	if ((reg0 & 0x0f000000) > 0) {
		/* Bit 27-20 contain the architecture in hex */
		dev_priv->chipset = (reg0 & 0xff00000) >> 20;
	/* NV04 or NV05 */
	} else if ((reg0 & 0xff00fff0) == 0x20004000) {
		if (reg0 & 0x00f00000)
			dev_priv->chipset = 0x05;
		else
			dev_priv->chipset = 0x04;
	} else
		dev_priv->chipset = 0xff;

	switch (dev_priv->chipset & 0xf0) {
	case 0x00:
	case 0x10:
	case 0x20:
	case 0x30:
		dev_priv->card_type = dev_priv->chipset & 0xf0;
		break;
	case 0x40:
	case 0x60:
		dev_priv->card_type = NV_40;
		break;
	case 0x50:
	case 0x80:
	case 0x90:
	case 0xa0:
		dev_priv->card_type = NV_50;
		break;
	default:
		NV_INFO(dev, "Unsupported chipset 0x%08x\n", reg0);
		return -EINVAL;
	}

	NV_INFO(dev, "Detected an NV%2x generation card (0x%08x)\n",
		dev_priv->card_type, reg0);

	dev_priv->fb_size = pci_resource_len(dev->pdev, 1);
	dev_priv->fb_phys = pci_resource_start(dev->pdev, 1);
	dev_priv->mmio_phys = pci_resource_start(dev->pdev, 0);

	/* map larger RAMIN aperture on NV40 cards */
	if (dev_priv->card_type >= NV_40) {
		int ramin_bar = 2;
		if (pci_resource_len(dev->pdev, ramin_bar) == 0)
			ramin_bar = 3;

		dev_priv->ramin_size = pci_resource_len(dev->pdev, ramin_bar);
		dev_priv->ramin = ioremap(
				pci_resource_start(dev->pdev, ramin_bar),
				dev_priv->ramin_size);
		if (!dev_priv->ramin) {
			NV_ERROR(dev, "Failed to init RAMIN mapping\n");
			return -ENOMEM;
		}
	}

	nouveau_OF_copy_vbios_to_ramin(dev);

	/* Special flags */
	if (dev->pci_device == 0x01a0)
		dev_priv->flags |= NV_NFORCE;
	else if (dev->pci_device == 0x01f0)
		dev_priv->flags |= NV_NFORCE2;

	/* For kernel modesetting, init card now and bring up fbcon */
	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		int ret = nouveau_card_init(dev);
		if (ret)
			return ret;
	}

	return 0;
}

static void nouveau_close(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	/* In the case of an error dev_priv may not be allocated yet */
	if (dev_priv)
		nouveau_card_takedown(dev);
}

/* KMS: we need mmio at load time, not when the first drm client opens. */
void nouveau_lastclose(struct drm_device *dev)
{
	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return;

	nouveau_close(dev);
}

int nouveau_unload(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		if (dev_priv->card_type >= NV_50)
			nv50_display_destroy(dev);
		else
			/*nv04_display_destroy(dev)*/;
		nouveau_close(dev);
	}

	iounmap(dev_priv->mmio);
	iounmap(dev_priv->ramin);

	kfree(dev_priv);
	dev->dev_private = NULL;
	return 0;
}

int pscnv_ioctl_getparam(struct drm_device *dev, void *data,
						struct drm_file *file_priv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_pscnv_getparam *getparam = data;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	switch (getparam->param) {
	case PSCNV_GETPARAM_CHIPSET_ID:
		getparam->value = dev_priv->chipset;
		break;
	case PSCNV_GETPARAM_PCI_VENDOR:
		getparam->value = dev->pci_vendor;
		break;
	case PSCNV_GETPARAM_PCI_DEVICE:
		getparam->value = dev->pci_device;
		break;
	case PSCNV_GETPARAM_BUS_TYPE:
		if (drm_device_is_agp(dev))
			getparam->value = NV_AGP;
		else if (drm_device_is_pcie(dev))
			getparam->value = NV_PCIE;
		else
			getparam->value = NV_PCI;
		break;
	case PSCNV_GETPARAM_PTIMER_TIME:
		getparam->value = nv04_timer_read(dev);
		break;
	case PSCNV_GETPARAM_FB_SIZE:
		getparam->value = dev_priv->vram_size;
		break;
	case PSCNV_GETPARAM_GRAPH_UNITS:
		/* NV40 and NV50 versions are quite different, but register
		 * address is the same. User is supposed to know the card
		 * family anyway... */
		if (dev_priv->chipset >= 0x40) {
			getparam->value = nv_rd32(dev, NV40_PMC_GRAPH_UNITS);
			break;
		}
		/* FALLTHRU */
	default:
		NV_ERROR(dev, "unknown parameter %lld\n", getparam->param);
		return -EINVAL;
	}

	return 0;
}

/* Wait until (value(reg) & mask) == val, up until timeout has hit */
bool nouveau_wait_until(struct drm_device *dev, uint64_t timeout,
			uint32_t reg, uint32_t mask, uint32_t val)
{
	uint64_t start = nv04_timer_read(dev);

	do {
		if ((nv_rd32(dev, reg) & mask) == val)
			return true;
	} while (nv04_timer_read(dev) - start < timeout);

	return false;
}
