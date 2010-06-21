/*
 * Copyright 2005 Stephane Marchesin.
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

#ifndef __NOUVEAU_DRV_H__
#define __NOUVEAU_DRV_H__

#define DRIVER_AUTHOR		"Stephane Marchesin"
#define DRIVER_EMAIL		"dri-devel@lists.sourceforge.net"

#define DRIVER_NAME		"pscnv"
#define DRIVER_DESC		"nVidia NV50"
#define DRIVER_DATE		"20090420"

#define DRIVER_MAJOR		0
#define DRIVER_MINOR		0
#define DRIVER_PATCHLEVEL	16

#define DRM_FILE_PAGE_OFFSET (0x100000000ULL >> PAGE_SHIFT)

#include "pscnv_drm.h"
#include "nouveau_bios.h"
#include "pscnv_vram.h"
#include "pscnv_vm.h"
#include "pscnv_ramht.h"
struct nouveau_grctx;

#define MAX_NUM_DCB_ENTRIES 16

#define NOUVEAU_MAX_CHANNEL_NR 128
#define NOUVEAU_MAX_TILE_NR 15

#define NV50_VM_MAX_VRAM (2*1024*1024*1024ULL)
#define NV50_VM_BLOCK    (512*1024*1024ULL)
#define NV50_VM_VRAM_NR  (NV50_VM_MAX_VRAM / NV50_VM_BLOCK)

#if 0 /* for pre-NV50 */
struct nouveau_tile_reg {
	struct nouveau_fence *fence;
	uint32_t addr;
	uint32_t size;
	bool used;
};
#endif

enum nouveau_flags {
	NV_NFORCE   = 0x10000000,
	NV_NFORCE2  = 0x20000000
};

struct nouveau_channel {
	struct drm_device *dev;
	int id;

	/* mapping of the regs controling the fifo */
	uint32_t user_get;
	uint32_t user_put;

	/* Fencing */
	struct {
		/* lock protects the pending list only */
		spinlock_t lock;
		struct list_head pending;
		uint32_t sequence;
		uint32_t sequence_ack;
		uint32_t last_sequence_irq;
	} fence;

	/* DMA push buffer */
	struct pscnv_vo *pushbuf;
	uint32_t pushbuf_base;

	/* EVO objects */
	struct pscnv_vo *evo_obj;
	struct pscnv_ramht evo_ramht;
	uint32_t evo_inst;

	/* GPU object info for stuff used in-kernel (mm_enabled) */
	bool accel_done;

	/* Push buffer state (only for drm's channel on !mm_enabled) */
	struct {
		int max;
		int free;
		int cur;
		int put;
		/* access via pushbuf_bo */
	} dma;

	struct {
		bool active;
		char name[32];
		struct drm_info_list info;
	} debugfs;
};

struct nouveau_mc_engine {
	int  (*init)(struct drm_device *dev);
	void (*takedown)(struct drm_device *dev);
};

struct nouveau_timer_engine {
	int      (*init)(struct drm_device *dev);
	void     (*takedown)(struct drm_device *dev);
	uint64_t (*read)(struct drm_device *dev);
};

struct nouveau_fb_engine {
	int num_tiles;

	int  (*init)(struct drm_device *dev);
	void (*takedown)(struct drm_device *dev);

	void (*set_region_tiling)(struct drm_device *dev, int i, uint32_t addr,
				 uint32_t size, uint32_t pitch);
};

struct nouveau_engine {
	struct nouveau_mc_engine      mc;
	struct nouveau_timer_engine   timer;
	struct nouveau_fb_engine      fb;
};

struct nouveau_pll_vals {
	union {
		struct {
#ifdef __BIG_ENDIAN
			uint8_t N1, M1, N2, M2;
#else
			uint8_t M1, N1, M2, N2;
#endif
		};
		struct {
			uint16_t NM1, NM2;
		} __attribute__((packed));
	};
	int log2P;

	int refclk;
};

enum nv04_fp_display_regs {
	FP_DISPLAY_END,
	FP_TOTAL,
	FP_CRTC,
	FP_SYNC_START,
	FP_SYNC_END,
	FP_VALID_START,
	FP_VALID_END
};

struct nv04_crtc_reg {
	unsigned char MiscOutReg;     /* */
	uint8_t CRTC[0x9f];
	uint8_t CR58[0x10];
	uint8_t Sequencer[5];
	uint8_t Graphics[9];
	uint8_t Attribute[21];
	unsigned char DAC[768];       /* Internal Colorlookuptable */

	/* PCRTC regs */
	uint32_t fb_start;
	uint32_t crtc_cfg;
	uint32_t cursor_cfg;
	uint32_t gpio_ext;
	uint32_t crtc_830;
	uint32_t crtc_834;
	uint32_t crtc_850;
	uint32_t crtc_eng_ctrl;

	/* PRAMDAC regs */
	uint32_t nv10_cursync;
	struct nouveau_pll_vals pllvals;
	uint32_t ramdac_gen_ctrl;
	uint32_t ramdac_630;
	uint32_t ramdac_634;
	uint32_t tv_setup;
	uint32_t tv_vtotal;
	uint32_t tv_vskew;
	uint32_t tv_vsync_delay;
	uint32_t tv_htotal;
	uint32_t tv_hskew;
	uint32_t tv_hsync_delay;
	uint32_t tv_hsync_delay2;
	uint32_t fp_horiz_regs[7];
	uint32_t fp_vert_regs[7];
	uint32_t dither;
	uint32_t fp_control;
	uint32_t dither_regs[6];
	uint32_t fp_debug_0;
	uint32_t fp_debug_1;
	uint32_t fp_debug_2;
	uint32_t fp_margin_color;
	uint32_t ramdac_8c0;
	uint32_t ramdac_a20;
	uint32_t ramdac_a24;
	uint32_t ramdac_a34;
	uint32_t ctv_regs[38];
};

struct nv04_output_reg {
	uint32_t output;
	int head;
};

struct nv04_mode_state {
	uint32_t bpp;
	uint32_t width;
	uint32_t height;
	uint32_t interlace;
	uint32_t repaint0;
	uint32_t repaint1;
	uint32_t screen;
	uint32_t scale;
	uint32_t dither;
	uint32_t extra;
	uint32_t fifo;
	uint32_t pixel;
	uint32_t horiz;
	int arbitration0;
	int arbitration1;
	uint32_t pll;
	uint32_t pllB;
	uint32_t vpll;
	uint32_t vpll2;
	uint32_t vpllB;
	uint32_t vpll2B;
	uint32_t pllsel;
	uint32_t sel_clk;
	uint32_t general;
	uint32_t crtcOwner;
	uint32_t head;
	uint32_t head2;
	uint32_t cursorConfig;
	uint32_t cursor0;
	uint32_t cursor1;
	uint32_t cursor2;
	uint32_t timingH;
	uint32_t timingV;
	uint32_t displayV;
	uint32_t crtcSync;

	struct nv04_crtc_reg crtc_reg[2];
};

enum nouveau_card_type {
	NV_04      = 0x00,
	NV_10      = 0x10,
	NV_20      = 0x20,
	NV_30      = 0x30,
	NV_40      = 0x40,
	NV_50      = 0x50,
	NV_C0      = 0xc0,
};

struct drm_nouveau_private {
	struct drm_device *dev;
	enum {
		NOUVEAU_CARD_INIT_DOWN,
		NOUVEAU_CARD_INIT_DONE,
		NOUVEAU_CARD_INIT_FAILED
	} init_state;

	/* the card type, takes NV_* as values */
	enum nouveau_card_type card_type;
	/* exact chipset, derived from NV_PMC_BOOT_0 */
	int chipset;
	int flags;

	void __iomem *mmio;
	void __iomem *ramin;
	uint32_t ramin_size;

	struct workqueue_struct *wq;
	struct work_struct irq_work;
	struct work_struct hpd_work;

	struct fb_info *fbdev_info;
#if 0
	struct nouveau_channel *fifos[NOUVEAU_MAX_CHANNEL_NR];
#endif
	struct nouveau_engine engine;
#if 0
	struct nouveau_channel *channel;
#endif

	spinlock_t irq_lock;

#if 0 /* relevant only for pre-NV50 */
	/* RAMIN configuration, RAMFC, RAMHT and RAMRO offsets */
	struct nouveau_gpuobj *ramht;
	uint32_t ramin_rsvd_vram;
	uint32_t ramht_offset;
	uint32_t ramht_size;
	uint32_t ramht_bits;
	uint32_t ramfc_offset;
	uint32_t ramfc_size;
	uint32_t ramro_offset;
	uint32_t ramro_size;

	struct {
		enum {
			NOUVEAU_GART_NONE = 0,
			NOUVEAU_GART_AGP,
			NOUVEAU_GART_SGDMA
		} type;
		uint64_t aper_base;
		uint64_t aper_size;
		uint64_t aper_free;

		struct nouveau_gpuobj *sg_ctxdma;
		struct page *sg_dummy_page;
		dma_addr_t sg_dummy_bus;
	} gart_info;

	/* nv10-nv40 tiling regions */
	struct {
		struct nouveau_tile_reg reg[NOUVEAU_MAX_TILE_NR];
		spinlock_t lock;
	} tile;
#endif

	/* VRAM/fb configuration */
	uint64_t vram_size;
	uint64_t vram_sys_base;
	uint64_t vm_ramin_base;

	uint64_t fb_size;
	uint64_t fb_phys;
	int fb_mtrr;

	uint64_t mmio_phys;

	struct list_head vram_global_list;
	struct list_head vram_free_list;
	uint32_t vram_rblock_size;
	int ropc_count;
	struct mutex vram_mutex;

	struct pscnv_vspace *bar1_vm;
	struct pscnv_vspace *bar3_vm;
	struct pscnv_chan *bar1_ch;
	struct pscnv_chan *bar3_ch;

	struct pscnv_vspace *vspaces[128];
	struct pscnv_chan *chans[128];
	struct mutex vm_mutex;

	/* PFIFO */

	struct pscnv_vo *playlist[2];
	int cur_playlist;
	spinlock_t pfifo_lock;
	spinlock_t pgraph_lock;

	volatile uint32_t *fifo_ctl;
	struct pscnv_vo *fifo_vo;

	/* PGRAPH */

	const uint32_t *grctx_vals;
	uint32_t grctx_size;

	struct pscnv_vo *obj188b4, *obj188b8;
	struct pscnv_vo *obj08004, *obj0800c;
	struct pscnv_vo *obj19848;

	/* for slow-path nv_wv32/nv_rv32 */

	spinlock_t pramin_lock;
	uint64_t pramin_start;

#if 0 /* pre-NV50 only */
	struct mem_block *ramin_heap;

	/* context table pointed to be NV_PGRAPH_CHANNEL_CTX_TABLE (0x400780) */
	uint32_t ctx_table_size;
	struct nouveau_gpuobj_ref *ctx_table;
#endif

	struct nvbios vbios;

	struct nv04_mode_state mode_reg;
	struct nv04_mode_state saved_reg;
	uint32_t saved_vga_font[4][16384];
	uint32_t crtc_owner;
	uint32_t dac_users[4];

	struct nouveau_suspend_resume {
		uint32_t *ramin_copy;
	} susres;

	struct backlight_device *backlight;

	struct nouveau_channel *evo;
	struct pscnv_vo *evo_obj;

	struct {
		struct dentry *channel_root;
	} debugfs;
};
#if 0
static inline struct drm_nouveau_private *
nouveau_bdev(struct ttm_bo_device *bd)
{
	return container_of(bd, struct drm_nouveau_private, ttm.bdev);
}

static inline int
nouveau_bo_ref(struct nouveau_bo *ref, struct nouveau_bo **pnvbo)
{
	struct nouveau_bo *prev;

	if (!pnvbo)
		return -EINVAL;
	prev = *pnvbo;

	*pnvbo = ref ? nouveau_bo(ttm_bo_reference(&ref->bo)) : NULL;
	if (prev) {
		struct ttm_buffer_object *bo = &prev->bo;

		ttm_bo_unref(&bo);
	}

	return 0;
}
#endif
#define NOUVEAU_CHECK_INITIALISED_WITH_RETURN do {            \
	struct drm_nouveau_private *nv = dev->dev_private;    \
	if (nv->init_state != NOUVEAU_CARD_INIT_DONE) {       \
		NV_ERROR(dev, "called without init\n");       \
		return -EINVAL;                               \
	}                                                     \
} while (0)
#if 0
#define NOUVEAU_GET_USER_CHANNEL_WITH_RETURN(id, cl, ch) do {    \
	struct drm_nouveau_private *nv = dev->dev_private;       \
	if (!nouveau_channel_owner(dev, (cl), (id))) {           \
		NV_ERROR(dev, "pid %d doesn't own channel %d\n", \
			 DRM_CURRENTPID, (id));                  \
		return -EPERM;                                   \
	}                                                        \
	(ch) = nv->fifos[(id)];                                  \
} while (0)
#endif

/* nouveau_drv.c */
extern int nouveau_noagp;
extern int nouveau_duallink;
extern int nouveau_uscript_lvds;
extern int nouveau_uscript_tmds;
extern int nouveau_vram_pushbuf;
extern int nouveau_vram_notify;
extern int nouveau_fbpercrtc;
extern int nouveau_tv_disable;
extern char *nouveau_tv_norm;
extern int nouveau_reg_debug;
extern int pscnv_vram_debug;
extern int pscnv_vm_debug;
extern int pscnv_gem_debug;
extern int pscnv_ramht_debug;
extern char *nouveau_vbios;
extern int nouveau_ctxfw;
extern int nouveau_ignorelid;
extern int nouveau_nofbaccel;
extern int nouveau_noaccel;
extern int nouveau_override_conntype;

extern struct drm_ioctl_desc nouveau_ioctls[];
extern int nouveau_max_ioctl;

extern int nouveau_pci_suspend(struct pci_dev *pdev, pm_message_t pm_state);
extern int nouveau_pci_resume(struct pci_dev *pdev);

/* nouveau_state.c */
extern void nouveau_preclose(struct drm_device *dev, struct drm_file *);
extern int  nouveau_load(struct drm_device *, unsigned long flags);
extern int  nouveau_firstopen(struct drm_device *);
extern void nouveau_lastclose(struct drm_device *);
extern int  nouveau_unload(struct drm_device *);
extern int  pscnv_ioctl_getparam(struct drm_device *, void *data,
				   struct drm_file *);
extern bool nouveau_wait_until(struct drm_device *, uint64_t timeout,
			       uint32_t reg, uint32_t mask, uint32_t val);
//extern bool nouveau_wait_for_idle(struct drm_device *);
extern int  nouveau_card_init(struct drm_device *);

# if 0

/* nouveau_mem.c */
extern int  nouveau_mem_init_heap(struct mem_block **, uint64_t start,
				 uint64_t size);
extern struct mem_block *nouveau_mem_alloc_block(struct mem_block *,
						 uint64_t size, int align2,
						 struct drm_file *, int tail);
extern void nouveau_mem_takedown(struct mem_block **heap);
extern void nouveau_mem_free_block(struct mem_block *);
extern int  nouveau_mem_detect(struct drm_device *dev);
extern void nouveau_mem_release(struct drm_file *, struct mem_block *heap);
extern int  nouveau_mem_init(struct drm_device *);
extern int  nouveau_mem_init_agp(struct drm_device *);
extern void nouveau_mem_close(struct drm_device *);
extern struct nouveau_tile_reg *nv10_mem_set_tiling(struct drm_device *dev,
						    uint32_t addr,
						    uint32_t size,
						    uint32_t pitch);
extern void nv10_mem_expire_tiling(struct drm_device *dev,
				   struct nouveau_tile_reg *tile,
				   struct nouveau_fence *fence);
extern int  nv50_mem_vm_bind_linear(struct drm_device *, uint64_t virt,
				    uint32_t size, uint32_t flags,
				    uint64_t phys);
extern void nv50_mem_vm_unbind(struct drm_device *, uint64_t virt,
			       uint32_t size);

/* nouveau_notifier.c */
extern int  nouveau_notifier_init_channel(struct nouveau_channel *);
extern void nouveau_notifier_takedown_channel(struct nouveau_channel *);
extern int  nouveau_notifier_alloc(struct nouveau_channel *, uint32_t handle,
				   int cout, uint32_t *offset);
extern int  nouveau_notifier_offset(struct nouveau_gpuobj *, uint32_t *);
extern int  nouveau_ioctl_notifier_alloc(struct drm_device *, void *data,
					 struct drm_file *);
extern int  nouveau_ioctl_notifier_free(struct drm_device *, void *data,
					struct drm_file *);

/* nouveau_channel.c */
extern void nouveau_channel_cleanup(struct drm_device *, struct drm_file *);
extern int  nouveau_channel_owner(struct drm_device *, struct drm_file *,
				  int channel);
extern int  nouveau_channel_alloc(struct drm_device *dev,
				  struct nouveau_channel **chan,
				  struct drm_file *file_priv,
				  uint32_t fb_ctxdma, uint32_t tt_ctxdma);
extern void nouveau_channel_free(struct nouveau_channel *);

/* nouveau_object.c */
extern int  nouveau_gpuobj_early_init(struct drm_device *);
extern int  nouveau_gpuobj_init(struct drm_device *);
extern void nouveau_gpuobj_takedown(struct drm_device *);
extern void nouveau_gpuobj_late_takedown(struct drm_device *);
extern int  nouveau_gpuobj_suspend(struct drm_device *dev);
extern void nouveau_gpuobj_suspend_cleanup(struct drm_device *dev);
extern void nouveau_gpuobj_resume(struct drm_device *dev);
extern int nouveau_gpuobj_channel_init(struct nouveau_channel *,
				       uint32_t vram_h, uint32_t tt_h);
extern void nouveau_gpuobj_channel_takedown(struct nouveau_channel *);
extern int nouveau_gpuobj_new(struct drm_device *, struct nouveau_channel *,
			      uint32_t size, int align, uint32_t flags,
			      struct nouveau_gpuobj **);
extern int nouveau_gpuobj_del(struct drm_device *, struct nouveau_gpuobj **);
extern int nouveau_gpuobj_ref_add(struct drm_device *, struct nouveau_channel *,
				  uint32_t handle, struct nouveau_gpuobj *,
				  struct nouveau_gpuobj_ref **);
extern int nouveau_gpuobj_ref_del(struct drm_device *,
				  struct nouveau_gpuobj_ref **);
extern int nouveau_gpuobj_ref_find(struct nouveau_channel *, uint32_t handle,
				   struct nouveau_gpuobj_ref **ref_ret);
extern int nouveau_gpuobj_new_ref(struct drm_device *,
				  struct nouveau_channel *alloc_chan,
				  struct nouveau_channel *ref_chan,
				  uint32_t handle, uint32_t size, int align,
				  uint32_t flags, struct nouveau_gpuobj_ref **);
extern int nouveau_gpuobj_new_fake(struct drm_device *,
				   uint32_t p_offset, uint32_t b_offset,
				   uint32_t size, uint32_t flags,
				   struct nouveau_gpuobj **,
				   struct nouveau_gpuobj_ref**);
extern int nouveau_gpuobj_dma_new(struct nouveau_channel *, int class,
				  uint64_t offset, uint64_t size, int access,
				  int target, struct nouveau_gpuobj **);
extern int nouveau_gpuobj_gart_dma_new(struct nouveau_channel *,
				       uint64_t offset, uint64_t size,
				       int access, struct nouveau_gpuobj **,
				       uint32_t *o_ret);
extern int nouveau_gpuobj_gr_new(struct nouveau_channel *, int class,
				 struct nouveau_gpuobj **);
extern int nouveau_gpuobj_sw_new(struct nouveau_channel *, int class,
				 struct nouveau_gpuobj **);
extern int nouveau_ioctl_grobj_alloc(struct drm_device *, void *data,
				     struct drm_file *);
extern int nouveau_ioctl_gpuobj_free(struct drm_device *, void *data,
				     struct drm_file *);

#endif
/* nouveau_irq.c */
extern irqreturn_t nouveau_irq_handler(DRM_IRQ_ARGS);
extern void        nouveau_irq_preinstall(struct drm_device *);
extern int         nouveau_irq_postinstall(struct drm_device *);
extern void        nouveau_irq_uninstall(struct drm_device *);
#if 0
/* nouveau_sgdma.c */
extern int nouveau_sgdma_init(struct drm_device *);
extern void nouveau_sgdma_takedown(struct drm_device *);
extern int nouveau_sgdma_get_page(struct drm_device *, uint32_t offset,
				  uint32_t *page);
extern struct ttm_backend *nouveau_sgdma_init_ttm(struct drm_device *);

/* nouveau_debugfs.c */
#endif
#if defined(CONFIG_DRM_NOUVEAU_DEBUG)
extern int  nouveau_debugfs_init(struct drm_minor *);
extern void nouveau_debugfs_takedown(struct drm_minor *);
#if 0
extern int  nouveau_debugfs_channel_init(struct nouveau_channel *);
extern void nouveau_debugfs_channel_fini(struct nouveau_channel *);
#endif
#else
static inline int
nouveau_debugfs_init(struct drm_minor *minor)
{
	return 0;
}

static inline void nouveau_debugfs_takedown(struct drm_minor *minor)
{
}
#if 0
static inline int
nouveau_debugfs_channel_init(struct nouveau_channel *chan)
{
	return 0;
}

static inline void
nouveau_debugfs_channel_fini(struct nouveau_channel *chan)
{
}
#endif
#endif

/* nouveau_acpi.c */
#if defined(CONFIG_ACPI)
void nouveau_register_dsm_handler(void);
void nouveau_unregister_dsm_handler(void);
#else
static inline void nouveau_register_dsm_handler(void) {}
static inline void nouveau_unregister_dsm_handler(void) {}
#endif

/* nouveau_backlight.c */
#ifdef CONFIG_DRM_NOUVEAU_BACKLIGHT
extern int nouveau_backlight_init(struct drm_device *);
extern void nouveau_backlight_exit(struct drm_device *);
#else
static inline int nouveau_backlight_init(struct drm_device *dev)
{
	return 0;
}

static inline void nouveau_backlight_exit(struct drm_device *dev) { }
#endif

/* nouveau_bios.c */
extern int nouveau_bios_init(struct drm_device *);
extern void nouveau_bios_takedown(struct drm_device *dev);
extern int nouveau_run_vbios_init(struct drm_device *);
extern void nouveau_bios_run_init_table(struct drm_device *, uint16_t table,
					struct dcb_entry *);
extern struct dcb_gpio_entry *nouveau_bios_gpio_entry(struct drm_device *,
						      enum dcb_gpio_tag);
extern struct dcb_connector_table_entry *
nouveau_bios_connector_entry(struct drm_device *, int index);
extern int get_pll_limits(struct drm_device *, uint32_t limit_match,
			  struct pll_lims *);
extern int nouveau_bios_run_display_table(struct drm_device *,
					  struct dcb_entry *,
					  uint32_t script, int pxclk);
extern void *nouveau_bios_dp_table(struct drm_device *, struct dcb_entry *,
				   int *length);
extern bool nouveau_bios_fp_mode(struct drm_device *, struct drm_display_mode *);
extern uint8_t *nouveau_bios_embedded_edid(struct drm_device *);
extern int nouveau_bios_parse_lvds_table(struct drm_device *, int pxclk,
					 bool *dl, bool *if_is_24bit);
extern int run_tmds_table(struct drm_device *, struct dcb_entry *,
			  int head, int pxclk);
extern int call_lvds_script(struct drm_device *, struct dcb_entry *, int head,
			    enum LVDS_script, int pxclk);

/* nouveau_dp.c */
int nouveau_dp_auxch(struct nouveau_i2c_chan *auxch, int cmd, int addr,
		     uint8_t *data, int data_nr);
bool nouveau_dp_detect(struct drm_encoder *);
bool nouveau_dp_link_train(struct drm_encoder *);

/* nv04_fb.c */
extern int  nv04_fb_init(struct drm_device *);
extern void nv04_fb_takedown(struct drm_device *);

/* nv10_fb.c */
extern int  nv10_fb_init(struct drm_device *);
extern void nv10_fb_takedown(struct drm_device *);
extern void nv10_fb_set_region_tiling(struct drm_device *, int, uint32_t,
				      uint32_t, uint32_t);

/* nv40_fb.c */
extern int  nv40_fb_init(struct drm_device *);
extern void nv40_fb_takedown(struct drm_device *);
extern void nv40_fb_set_region_tiling(struct drm_device *, int, uint32_t,
				      uint32_t, uint32_t);
/* nv50_fb.c */
extern int  nv50_fb_init(struct drm_device *);
extern void nv50_fb_takedown(struct drm_device *);

/* nv50_fb.c */
extern int  nv50_fb_init(struct drm_device *);
extern void nv50_fb_takedown(struct drm_device *);
#if 0
/* nv04_fifo.c */
extern int  nv04_fifo_init(struct drm_device *);
extern void nv04_fifo_disable(struct drm_device *);
extern void nv04_fifo_enable(struct drm_device *);
extern bool nv04_fifo_reassign(struct drm_device *, bool);
extern bool nv04_fifo_cache_flush(struct drm_device *);
extern bool nv04_fifo_cache_pull(struct drm_device *, bool);
extern int  nv04_fifo_channel_id(struct drm_device *);
extern int  nv04_fifo_create_context(struct nouveau_channel *);
extern void nv04_fifo_destroy_context(struct nouveau_channel *);
extern int  nv04_fifo_load_context(struct nouveau_channel *);
extern int  nv04_fifo_unload_context(struct drm_device *);

/* nv10_fifo.c */
extern int  nv10_fifo_init(struct drm_device *);
extern int  nv10_fifo_channel_id(struct drm_device *);
extern int  nv10_fifo_create_context(struct nouveau_channel *);
extern void nv10_fifo_destroy_context(struct nouveau_channel *);
extern int  nv10_fifo_load_context(struct nouveau_channel *);
extern int  nv10_fifo_unload_context(struct drm_device *);

/* nv40_fifo.c */
extern int  nv40_fifo_init(struct drm_device *);
extern int  nv40_fifo_create_context(struct nouveau_channel *);
extern void nv40_fifo_destroy_context(struct nouveau_channel *);
extern int  nv40_fifo_load_context(struct nouveau_channel *);
extern int  nv40_fifo_unload_context(struct drm_device *);

/* nv50_fifo.c */
extern int  nv50_fifo_init(struct drm_device *);
extern void nv50_fifo_takedown(struct drm_device *);
extern int  nv50_fifo_channel_id(struct drm_device *);
extern int  nv50_fifo_create_context(struct nouveau_channel *);
extern void nv50_fifo_destroy_context(struct nouveau_channel *);
extern int  nv50_fifo_load_context(struct nouveau_channel *);
extern int  nv50_fifo_unload_context(struct drm_device *);

/* nv04_graph.c */
extern struct nouveau_pgraph_object_class nv04_graph_grclass[];
extern int  nv04_graph_init(struct drm_device *);
extern void nv04_graph_takedown(struct drm_device *);
extern void nv04_graph_fifo_access(struct drm_device *, bool);
extern struct nouveau_channel *nv04_graph_channel(struct drm_device *);
extern int  nv04_graph_create_context(struct nouveau_channel *);
extern void nv04_graph_destroy_context(struct nouveau_channel *);
extern int  nv04_graph_load_context(struct nouveau_channel *);
extern int  nv04_graph_unload_context(struct drm_device *);
extern void nv04_graph_context_switch(struct drm_device *);

/* nv10_graph.c */
extern struct nouveau_pgraph_object_class nv10_graph_grclass[];
extern int  nv10_graph_init(struct drm_device *);
extern void nv10_graph_takedown(struct drm_device *);
extern struct nouveau_channel *nv10_graph_channel(struct drm_device *);
extern int  nv10_graph_create_context(struct nouveau_channel *);
extern void nv10_graph_destroy_context(struct nouveau_channel *);
extern int  nv10_graph_load_context(struct nouveau_channel *);
extern int  nv10_graph_unload_context(struct drm_device *);
extern void nv10_graph_context_switch(struct drm_device *);
extern void nv10_graph_set_region_tiling(struct drm_device *, int, uint32_t,
					 uint32_t, uint32_t);

/* nv20_graph.c */
extern struct nouveau_pgraph_object_class nv20_graph_grclass[];
extern struct nouveau_pgraph_object_class nv30_graph_grclass[];
extern int  nv20_graph_create_context(struct nouveau_channel *);
extern void nv20_graph_destroy_context(struct nouveau_channel *);
extern int  nv20_graph_load_context(struct nouveau_channel *);
extern int  nv20_graph_unload_context(struct drm_device *);
extern int  nv20_graph_init(struct drm_device *);
extern void nv20_graph_takedown(struct drm_device *);
extern int  nv30_graph_init(struct drm_device *);
extern void nv20_graph_set_region_tiling(struct drm_device *, int, uint32_t,
					 uint32_t, uint32_t);

/* nv40_graph.c */
extern struct nouveau_pgraph_object_class nv40_graph_grclass[];
extern int  nv40_graph_init(struct drm_device *);
extern void nv40_graph_takedown(struct drm_device *);
extern struct nouveau_channel *nv40_graph_channel(struct drm_device *);
extern int  nv40_graph_create_context(struct nouveau_channel *);
extern void nv40_graph_destroy_context(struct nouveau_channel *);
extern int  nv40_graph_load_context(struct nouveau_channel *);
extern int  nv40_graph_unload_context(struct drm_device *);
extern void nv40_grctx_init(struct nouveau_grctx *);
extern void nv40_graph_set_region_tiling(struct drm_device *, int, uint32_t,
					 uint32_t, uint32_t);

/* nv50_graph.c */
extern struct nouveau_pgraph_object_class nv50_graph_grclass[];
extern int  nv50_graph_init(struct drm_device *);
extern void nv50_graph_takedown(struct drm_device *);
extern void nv50_graph_fifo_access(struct drm_device *, bool);
extern struct nouveau_channel *nv50_graph_channel(struct drm_device *);
extern int  nv50_graph_create_context(struct nouveau_channel *);
extern void nv50_graph_destroy_context(struct nouveau_channel *);
extern int  nv50_graph_load_context(struct nouveau_channel *);
extern int  nv50_graph_unload_context(struct drm_device *);
extern void nv50_graph_context_switch(struct drm_device *);
extern int  nv50_grctx_init(struct nouveau_grctx *);

/* nouveau_grctx.c */
extern int  nouveau_grctx_prog_load(struct drm_device *);
extern void nouveau_grctx_vals_load(struct drm_device *,
				    struct nouveau_gpuobj *);
extern void nouveau_grctx_fini(struct drm_device *);

/* nv04_instmem.c */
extern int  nv04_instmem_init(struct drm_device *);
extern void nv04_instmem_takedown(struct drm_device *);
extern int  nv04_instmem_suspend(struct drm_device *);
extern void nv04_instmem_resume(struct drm_device *);
extern int  nv04_instmem_populate(struct drm_device *, struct nouveau_gpuobj *,
				  uint32_t *size);
extern void nv04_instmem_clear(struct drm_device *, struct nouveau_gpuobj *);
extern int  nv04_instmem_bind(struct drm_device *, struct nouveau_gpuobj *);
extern int  nv04_instmem_unbind(struct drm_device *, struct nouveau_gpuobj *);
extern void nv04_instmem_prepare_access(struct drm_device *, bool write);
extern void nv04_instmem_finish_access(struct drm_device *);

/* nv50_instmem.c */
extern int  nv50_instmem_init(struct drm_device *);
extern void nv50_instmem_takedown(struct drm_device *);
extern int  nv50_instmem_suspend(struct drm_device *);
extern void nv50_instmem_resume(struct drm_device *);
extern int  nv50_instmem_populate(struct drm_device *, struct nouveau_gpuobj *,
				  uint32_t *size);
extern void nv50_instmem_clear(struct drm_device *, struct nouveau_gpuobj *);
extern int  nv50_instmem_bind(struct drm_device *, struct nouveau_gpuobj *);
extern int  nv50_instmem_unbind(struct drm_device *, struct nouveau_gpuobj *);
extern void nv50_instmem_prepare_access(struct drm_device *, bool write);
extern void nv50_instmem_finish_access(struct drm_device *);

#endif
/* nv04_mc.c */
extern int  nv04_mc_init(struct drm_device *);
extern void nv04_mc_takedown(struct drm_device *);

/* nv40_mc.c */
extern int  nv40_mc_init(struct drm_device *);
extern void nv40_mc_takedown(struct drm_device *);

/* nv50_mc.c */
extern int  nv50_mc_init(struct drm_device *);
extern int  nvc0_mc_init(struct drm_device *);
extern void nv50_mc_takedown(struct drm_device *);

/* nv04_timer.c */
extern int  nv04_timer_init(struct drm_device *);
extern uint64_t nv04_timer_read(struct drm_device *);
extern void nv04_timer_takedown(struct drm_device *);

extern long nouveau_compat_ioctl(struct file *file, unsigned int cmd,
				 unsigned long arg);

/* nv04_dac.c */
extern int nv04_dac_create(struct drm_device *dev, struct dcb_entry *entry);
extern uint32_t nv17_dac_sample_load(struct drm_encoder *encoder);
extern int nv04_dac_output_offset(struct drm_encoder *encoder);
extern void nv04_dac_update_dacclk(struct drm_encoder *encoder, bool enable);

/* nv04_dfp.c */
extern int nv04_dfp_create(struct drm_device *dev, struct dcb_entry *entry);
extern int nv04_dfp_get_bound_head(struct drm_device *dev, struct dcb_entry *dcbent);
extern void nv04_dfp_bind_head(struct drm_device *dev, struct dcb_entry *dcbent,
			       int head, bool dl);
extern void nv04_dfp_disable(struct drm_device *dev, int head);
extern void nv04_dfp_update_fp_control(struct drm_encoder *encoder, int mode);

/* nv04_tv.c */
extern int nv04_tv_identify(struct drm_device *dev, int i2c_index);
extern int nv04_tv_create(struct drm_device *dev, struct dcb_entry *entry);
#if 0
/* nv17_tv.c */
extern int nv17_tv_create(struct drm_device *dev, struct dcb_entry *entry);

/* nv04_display.c */
extern int nv04_display_create(struct drm_device *);
extern void nv04_display_destroy(struct drm_device *);
extern void nv04_display_restore(struct drm_device *);

/* nv04_crtc.c */
extern int nv04_crtc_create(struct drm_device *, int index);

/* nouveau_bo.c */
extern struct ttm_bo_driver nouveau_bo_driver;
extern int nouveau_bo_new(struct drm_device *, struct nouveau_channel *,
			  int size, int align, uint32_t flags,
			  uint32_t tile_mode, uint32_t tile_flags,
			  bool no_vm, bool mappable, struct nouveau_bo **);
extern int nouveau_bo_pin(struct nouveau_bo *, uint32_t flags);
extern int nouveau_bo_unpin(struct nouveau_bo *);
extern int nouveau_bo_map(struct nouveau_bo *);
extern void nouveau_bo_unmap(struct nouveau_bo *);
extern void nouveau_bo_placement_set(struct nouveau_bo *, uint32_t type,
				     uint32_t busy);
extern u16 nouveau_bo_rd16(struct nouveau_bo *nvbo, unsigned index);
extern void nouveau_bo_wr16(struct nouveau_bo *nvbo, unsigned index, u16 val);
extern u32 nouveau_bo_rd32(struct nouveau_bo *nvbo, unsigned index);
extern void nouveau_bo_wr32(struct nouveau_bo *nvbo, unsigned index, u32 val);

/* nouveau_fence.c */
struct nouveau_fence;
extern int nouveau_fence_init(struct nouveau_channel *);
extern void nouveau_fence_fini(struct nouveau_channel *);
extern void nouveau_fence_update(struct nouveau_channel *);
extern int nouveau_fence_new(struct nouveau_channel *, struct nouveau_fence **,
			     bool emit);
extern int nouveau_fence_emit(struct nouveau_fence *);
struct nouveau_channel *nouveau_fence_channel(struct nouveau_fence *);
extern bool nouveau_fence_signalled(void *obj, void *arg);
extern int nouveau_fence_wait(void *obj, void *arg, bool lazy, bool intr);
extern int nouveau_fence_flush(void *obj, void *arg);
extern void nouveau_fence_unref(void **obj);
extern void *nouveau_fence_ref(void *obj);
extern void nouveau_fence_handler(struct drm_device *dev, int channel);

#endif
/* nv17_gpio.c */
int nv17_gpio_get(struct drm_device *dev, enum dcb_gpio_tag tag);
int nv17_gpio_set(struct drm_device *dev, enum dcb_gpio_tag tag, int state);

/* nv50_gpio.c */
int nv50_gpio_get(struct drm_device *dev, enum dcb_gpio_tag tag);
int nv50_gpio_set(struct drm_device *dev, enum dcb_gpio_tag tag, int state);

/* nv50_calc. */
int nv50_calc_pll(struct drm_device *, struct pll_lims *, int clk,
		  int *N1, int *M1, int *N2, int *M2, int *P);
int nv50_calc_pll2(struct drm_device *, struct pll_lims *,
		   int clk, int *N, int *fN, int *M, int *P);

#ifndef ioread32_native
#ifdef __BIG_ENDIAN
#define ioread16_native ioread16be
#define iowrite16_native iowrite16be
#define ioread32_native  ioread32be
#define iowrite32_native iowrite32be
#else /* def __BIG_ENDIAN */
#define ioread16_native ioread16
#define iowrite16_native iowrite16
#define ioread32_native  ioread32
#define iowrite32_native iowrite32
#endif /* def __BIG_ENDIAN else */
#endif /* !ioread32_native */

#if 0
/* channel control reg access */
static inline u32 nvchan_rd32(struct nouveau_channel *chan, unsigned reg)
{
	return ioread32_native(chan->user + reg);
}

static inline void nvchan_wr32(struct nouveau_channel *chan,
							unsigned reg, u32 val)
{
	iowrite32_native(val, chan->user + reg);
}
#endif

/* register access */
static inline u32 nv_rd32(struct drm_device *dev, unsigned reg)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	return ioread32_native(dev_priv->mmio + reg);
}

static inline void nv_wr32(struct drm_device *dev, unsigned reg, u32 val)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	iowrite32_native(val, dev_priv->mmio + reg);
}

static inline u8 nv_rd08(struct drm_device *dev, unsigned reg)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	return ioread8(dev_priv->mmio + reg);
}

static inline void nv_wr08(struct drm_device *dev, unsigned reg, u8 val)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	iowrite8(val, dev_priv->mmio + reg);
}

#define nv_wait(reg, mask, val) \
	nouveau_wait_until(dev, 2000000000ULL, (reg), (mask), (val))
#if 0 /* not removing yet - may be useful for pre-NV50 one day */
/* PRAMIN access */
static inline u32 nv_ri32(struct drm_device *dev, unsigned offset)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	return ioread32_native(dev_priv->ramin + offset);
}

static inline void nv_wi32(struct drm_device *dev, unsigned offset, u32 val)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	iowrite32_native(val, dev_priv->ramin + offset);
}
#endif
/*
 * Logging
 * Argument d is (struct drm_device *).
 */
#define NV_PRINTK(level, d, fmt, arg...) \
	printk(level "[" DRM_NAME "] " DRIVER_NAME " %s: " fmt, \
					pci_name(d->pdev), ##arg)
#ifndef NV_DEBUG_NOTRACE
#define NV_DEBUG(d, fmt, arg...) do {                                          \
	if (drm_debug & DRM_UT_DRIVER) {                                       \
		NV_PRINTK(KERN_DEBUG, d, "%s:%d - " fmt, __func__,             \
			  __LINE__, ##arg);                                    \
	}                                                                      \
} while (0)
#define NV_DEBUG_KMS(d, fmt, arg...) do {                                      \
	if (drm_debug & DRM_UT_KMS) {                                          \
		NV_PRINTK(KERN_DEBUG, d, "%s:%d - " fmt, __func__,             \
			  __LINE__, ##arg);                                    \
	}                                                                      \
} while (0)
#else
#define NV_DEBUG(d, fmt, arg...) do {                                          \
	if (drm_debug & DRM_UT_DRIVER)                                         \
		NV_PRINTK(KERN_DEBUG, d, fmt, ##arg);                          \
} while (0)
#define NV_DEBUG_KMS(d, fmt, arg...) do {                                      \
	if (drm_debug & DRM_UT_KMS)                                            \
		NV_PRINTK(KERN_DEBUG, d, fmt, ##arg);                          \
} while (0)
#endif
#define NV_ERROR(d, fmt, arg...) NV_PRINTK(KERN_ERR, d, fmt, ##arg)
#define NV_INFO(d, fmt, arg...) NV_PRINTK(KERN_INFO, d, fmt, ##arg)
#define NV_TRACEWARN(d, fmt, arg...) NV_PRINTK(KERN_NOTICE, d, fmt, ##arg)
#define NV_TRACE(d, fmt, arg...) NV_PRINTK(KERN_INFO, d, fmt, ##arg)
#define NV_WARN(d, fmt, arg...) NV_PRINTK(KERN_WARNING, d, fmt, ##arg)

/* nouveau_reg_debug bitmask */
enum {
	NOUVEAU_REG_DEBUG_MC             = 0x1,
	NOUVEAU_REG_DEBUG_VIDEO          = 0x2,
	NOUVEAU_REG_DEBUG_FB             = 0x4,
	NOUVEAU_REG_DEBUG_EXTDEV         = 0x8,
	NOUVEAU_REG_DEBUG_CRTC           = 0x10,
	NOUVEAU_REG_DEBUG_RAMDAC         = 0x20,
	NOUVEAU_REG_DEBUG_VGACRTC        = 0x40,
	NOUVEAU_REG_DEBUG_RMVIO          = 0x80,
	NOUVEAU_REG_DEBUG_VGAATTR        = 0x100,
	NOUVEAU_REG_DEBUG_EVO            = 0x200,
};

#define NV_REG_DEBUG(type, dev, fmt, arg...) do { \
	if (nouveau_reg_debug & NOUVEAU_REG_DEBUG_##type) \
		NV_PRINTK(KERN_DEBUG, dev, "%s: " fmt, __func__, ##arg); \
} while (0)

static inline bool
nv_two_heads(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	const int impl = dev->pci_device & 0x0ff0;

	if (dev_priv->card_type >= NV_10 && impl != 0x0100 &&
	    impl != 0x0150 && impl != 0x01a0 && impl != 0x0200)
		return true;

	return false;
}

static inline bool
nv_gf4_disp_arch(struct drm_device *dev)
{
	return nv_two_heads(dev) && (dev->pci_device & 0x0ff0) != 0x0110;
}

static inline bool
nv_two_reg_pll(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	const int impl = dev->pci_device & 0x0ff0;

	if (impl == 0x0310 || impl == 0x0340 || dev_priv->card_type >= NV_40)
		return true;
	return false;
}
#if 0
#define NV_SW                                                        0x0000506e
#define NV_SW_DMA_SEMAPHORE                                          0x00000060
#define NV_SW_SEMAPHORE_OFFSET                                       0x00000064
#define NV_SW_SEMAPHORE_ACQUIRE                                      0x00000068
#define NV_SW_SEMAPHORE_RELEASE                                      0x0000006c
#define NV_SW_DMA_VBLSEM                                             0x0000018c
#define NV_SW_VBLSEM_OFFSET                                          0x00000400
#define NV_SW_VBLSEM_RELEASE_VALUE                                   0x00000404
#define NV_SW_VBLSEM_RELEASE                                         0x00000408

#endif

/* object access */

static inline uint32_t nv_rv32(struct pscnv_vo *vo,
				unsigned offset)
{
	struct drm_nouveau_private *dev_priv = vo->dev->dev_private;
	uint32_t res;
	uint64_t addr = vo->start + offset;
	if (vo->map3 && dev_priv->bar3_vm)
		return ioread32_native(dev_priv->ramin + vo->map3->start
				       - dev_priv->vm_ramin_base + offset);
	spin_lock(&dev_priv->pramin_lock);
	if (addr >> 16 != dev_priv->pramin_start) {
		dev_priv->pramin_start = addr >> 16;
		nv_wr32(vo->dev, 0x1700, addr >> 16);
	}
	res = nv_rd32(vo->dev, 0x700000 + (addr & 0xffff));
	spin_unlock(&dev_priv->pramin_lock);
	return res;
}

static inline void nv_wv32(struct pscnv_vo *vo,
				unsigned offset, uint32_t val)
{
	struct drm_nouveau_private *dev_priv = vo->dev->dev_private;
	uint64_t addr = vo->start + offset;
	if (vo->map3 && dev_priv->bar3_vm)
		return iowrite32_native(val, dev_priv->ramin + vo->map3->start
					- dev_priv->vm_ramin_base + offset);
	spin_lock(&dev_priv->pramin_lock);
	if (addr >> 16 != dev_priv->pramin_start) {
		dev_priv->pramin_start = addr >> 16;
		nv_wr32(vo->dev, 0x1700, addr >> 16);
	}
	nv_wr32(vo->dev, 0x700000 + (addr & 0xffff), val);
	spin_unlock(&dev_priv->pramin_lock);
}

#endif /* __NOUVEAU_DRV_H__ */
