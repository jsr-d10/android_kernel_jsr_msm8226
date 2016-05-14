/**
 * @file drivers/video/console/llcon.c
 *
 * @brief
 * This file contains the implementation of Low Level Console
 * for output kernel messages to display.
 * This module support only 24-bit color format.
 *
 * @author	remittor <remittor@gmail.com>
 * @date	2016-05-11 
 */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/memblock.h>
#include <linux/io.h>
#include <linux/vmalloc.h>

#include <video/llcon.h>

#define LLCON_BPP           24
#define LLCON_PXL_SIZE      3

volatile int llcon_enabled = 0;

struct mutex llcon_lock;

static struct llcon_desc llcon;

/*
* androidboot.llcon=<mode>,<delay>,<textwrap>,<fb_addr>,<fb_bpp>,<fb_height>,
*                   <fb_width>,<fb_pitch>,<font_size>,<font_color>
*/

static int __init llcon_setup(char *str)
{
	int ints[16];
	int mode = LLCON_MODE_DISABLED;
	const struct font_desc * font = NULL;
	int font_size = 0;
	int font_color = 0;

	llcon.mode = LLCON_MODE_DISABLED;

	str = get_options(str, 15, ints);
	if (!ints[0])
		return 1;

	switch (ints[0]) {
	case 10:
		font_color = ints[10];
	case 9:
		font_size = ints[9];
	case 8:
		llcon.display.stride = ints[8];
	case 7:
		llcon.display.width = ints[7];
	case 6:
		llcon.display.height = ints[6];
	case 5:
		llcon.display.bpp = ints[5];
	case 4:
		llcon.display.bl_fbaddr = (void *)ints[4];
	case 3:
		llcon.textwrap = ints[3];
	case 2:
		llcon.delay = (size_t)ints[2];
	case 1:
		mode = ints[1];
	}

	if (mode <= LLCON_MODE_DISABLED)
		return 1;

	llcon.display.bpp = 24;
	llcon.display.pixel_size = 3;

	switch (font_size) {
#ifdef CONFIG_FONT_SUN12x22
	case 12:
		font = &font_sun_12x22;
		break;
#endif
#ifdef CONFIG_FONT_10x18
	case 10:
		font = &font_10x18;
		break;
#endif
#ifdef CONFIG_FONT_8x16
	case 8:
		font = &font_vga_8x16;
		break;
#endif
	default:
		font = &font_vga_6x11;
		break;
	}

	if (llcon_set_font_type(font))
		return 1;

	llcon_set_font_fg_color(font_color ? font_color : LLCON_WHITE);

	llcon.mode = mode;
	return 1; 
}
__setup("androidboot.llcon=", llcon_setup);


static noinline void llcon_drawglyph8(uint8_t * pixels, uint32_t paint,
                size_t gw, size_t gh, size_t stride, uint8_t * glyph)
{
	size_t x, y;
	uint8_t data;
	uint32_t pxlcol;

	for (y = 0; y < gh; y++) {
		data = *glyph++;
		for (x = 0; x < gw; x++) {
			pxlcol = paint & (~(((uint32_t)data >> 7) - 1));
			*pixels++ = (uint8_t)pxlcol;
			*pixels++ = (uint8_t)(pxlcol >> 8);
			*pixels++ = (uint8_t)(pxlcol >> 16);
			data <<= 1;
		}
		pixels += stride;
	}
}

static noinline void llcon_drawglyph16(uint8_t * pixels, uint32_t paint,
                size_t gw, size_t gh, size_t stride, uint8_t * glyph)
{
	size_t x, y;
	uint8_t data;
	uint32_t pxlcol;

	for (y = 0; y < gh; y++) {
		data = *glyph++;
		for (x = 0; x < 8; x++) {
			pxlcol = paint & (~(((uint32_t)data >> 7) - 1));
			*pixels++ = (uint8_t)pxlcol;
			*pixels++ = (uint8_t)(pxlcol >> 8);
			*pixels++ = (uint8_t)(pxlcol >> 16);
			data <<= 1;
		}
		data = *glyph++;
		for (x = 0; x < gw - 8; x++) {
			pxlcol = paint & (~(((uint32_t)data >> 7) - 1));
			*pixels++ = (uint8_t)pxlcol;
			*pixels++ = (uint8_t)(pxlcol >> 8);
			*pixels++ = (uint8_t)(pxlcol >> 16);
			data <<= 1;
		}
		pixels += stride;
	}
}

static inline void llcon_drawglyph_x(uint8_t * pixels, uint32_t paint,
                size_t gw, size_t gh, size_t stride, uint8_t * glyph, char c)
{
	if (gw <= 8) {
		glyph += gh * (uint8_t)c;
		llcon_drawglyph8(pixels, paint, gw, gh, stride, glyph);
	} else {
		glyph += 2 * gh * (uint8_t)c;
		llcon_drawglyph16(pixels, paint, gw, gh, stride, glyph);
	}
}

static inline void llcon_drawglyph(uint8_t * pixels, uint32_t paint, char c)
{
	size_t gw = llcon.font.width;
	size_t gh = llcon.font.height;
	size_t stride = llcon.glyph_stride;
	uint8_t * glyph = (uint8_t *)llcon.font.data;

	llcon_drawglyph_x(pixels, paint, gw, gh, stride, glyph, c);
}

void llcon_clear(void)
{
	size_t size;
	if (!llcon_enabled)
		return;
	size = llcon.display.stride * llcon.display.height * LLCON_PXL_SIZE;
	memset(llcon.display.fbaddr, 0, size);
}

static inline uint8_t * get_char_pos_ptr(size_t x, size_t y)
{
	size_t offset;
	offset = y * (size_t)llcon.font.height * llcon.display.stride;
	offset += x * (size_t)llcon.font.width;
	return llcon.display.fbaddr + offset * LLCON_PXL_SIZE;
} 

static inline uint8_t * get_cursor_pos_ptr(void)
{
	return get_char_pos_ptr(llcon.cur.x, llcon.cur.y);
}

void llcon_putc(char c)
{
	uint8_t * pixels;

	if (!llcon_enabled)
		return;

	if (c == '\n')
		goto newline;

	if (llcon.cur.x > llcon.max.x || llcon.cur.y >= llcon.max.y)
		return;

	if (llcon.mode == LLCON_MODE_SYNC && !llcon.cur.y && !llcon.cur.x)
		llcon_clear();

	pixels = get_cursor_pos_ptr();
	llcon_drawglyph(pixels, llcon.fg_color, c);

	llcon.cur.x++;
	if (llcon.cur.x > llcon.max.x && llcon.textwrap)
		goto newline;

	return;

newline:
	llcon.cur.y++;
	llcon.cur.x = 0;
	if (llcon.cur.y >= llcon.max.y) {
		llcon.cur.y = 0;
	}
}

void llcon_putc_to_buf(char c)
{
	size_t dx;

	if (llcon.log.cur.x >= llcon.res.x) {
		if (c == 0 || c == '\n')
			goto newline;
		return;
	}

	if (c == 0 || c == '\n') {
		dx = llcon.res.x - llcon.log.cur.x;
		memset(llcon.log.buf + llcon.log.pos, 0x20, dx);
		llcon.log.pos += dx;
		if (llcon.log.pos >= llcon.log.bufsize)
			llcon.log.pos = 0;
		goto newline;
	}

	llcon.log.buf[llcon.log.pos++] = c;
	if (llcon.log.pos >= llcon.log.bufsize)
		llcon.log.pos = 0;

	llcon.log.cur.x++;
	if (llcon.log.cur.x >= llcon.res.x && llcon.textwrap)
		goto newline;

	return;

newline:
	llcon.log.cur.x = 0;
	llcon.log.cur.y++;
	memset(llcon.log.buf + llcon.log.pos, 0x20, llcon.res.x);
}

void llcon_emit(char c)
{
	switch (llcon.mode) {
	case LLCON_MODE_SYNC:
		llcon_putc(c);
		break;
	case LLCON_MODE_ASYNC:
		llcon_putc_to_buf(c);
		break;
	}
}

void llcon_set_cursor_pos(size_t x, size_t y)
{
	llcon.cur.x = x;
	llcon.cur.y = y;
}

void llcon_set_font_fg_color(uint32_t color)
{
	llcon.fg_color = color;
}

void llcon_set_font_bg_color(uint32_t color)
{
	llcon.bg_color = color;
}

void llcon_set_font_color(uint32_t fg, uint32_t bg)
{
	llcon_set_font_fg_color(fg);
	llcon_set_font_bg_color(bg);
}

int llcon_set_font_type(const struct font_desc * font)
{
	if (!font)
		return -1;
	llcon.font = *font;
	llcon_set_font_color(LLCON_WHITE, LLCON_BLACK);
	llcon.res.x = llcon.display.width / (size_t)llcon.font.width;
	llcon.res.y = llcon.display.height / (size_t)llcon.font.height;
	llcon.max.x = llcon.res.x - 1;
	llcon.max.y = llcon.res.y - 1;
	if (llcon.font.width > 16)
		return -2;

	llcon.glyph_stride = llcon.display.stride - (size_t)llcon.font.width;
	llcon.glyph_stride *= LLCON_PXL_SIZE;
	return 0;
}

int llcon_set_font_by_name(const char * fontname)
{
	const struct font_desc * font = find_font(fontname);
	return llcon_set_font_type(font);
}

struct llcon_desc * llcon_get(void)
{
	return &llcon;
}

void llcon_set_fb_addr(void * addr, size_t size)
{
	llcon.display.dt_fbaddr = addr;
	pr_info("LLCON: set FB addr = %p \n", addr);
}

void llcon_mem_free(void)
{
	if (llcon.display.fbaddr) {
		vunmap(llcon.display.fbaddr);
		llcon.display.fbaddr = NULL;
	}
	if (llcon.log.buf) {
		kfree(llcon.log.buf);
		llcon.log.buf = NULL;
		llcon.log.bufsize = 0;
	}
}

int llcon_thread(void * data)
{
	uint8_t * fb_addr;
	uint8_t * pixels;
	char c;
	size_t x, y;
	size_t cx, cy, cp, py, dy, dx, pos;
	size_t scanline;
	size_t gw = llcon.font.width;
	size_t gh = llcon.font.height;
	size_t stride = llcon.glyph_stride;
	uint8_t * glyph = (uint8_t *)llcon.font.data;
	char * logbuf = llcon.log.buf;
	size_t bufsize = llcon.log.bufsize;
	uint32_t fg_color = llcon.fg_color;
	size_t thread_delay = llcon.delay;
	size_t resx = llcon.res.x;
	size_t resy = llcon.res.y;
	size_t last_line = 0;

	llcon_clear();

	fb_addr = get_char_pos_ptr(0, 0);
	scanline = llcon.display.stride * LLCON_PXL_SIZE * gh;
	dx = gw * LLCON_PXL_SIZE;
	dy = scanline - (dx * resx);

	while (llcon_enabled) {
		mdelay(thread_delay);
		cx = llcon.log.cur.x;
		cy = llcon.log.cur.y;
		cp = llcon.log.pos;

		if (cy <= last_line)
			continue;

		pos = 0;
		if (cy >= resy) {
			py = cp / resx;
			pos = (py + 1) * resx;
			if (pos >= bufsize)
				pos = 0;
		}

		pixels = fb_addr;
		for (y = 0; y < resy - 1; y++) {
			for (x = 0; x < resx; x++) {
				c = logbuf[pos++];
				if (pos >= bufsize)
					pos = 0;
				llcon_drawglyph_x(pixels, fg_color, gw, gh, stride, glyph, c);
				pixels += dx;
			}
			pixels += dy;
			if (!llcon_enabled)
				goto exit;
		}

		last_line = cy;
	}

exit:
	vunmap(llcon.display.fbaddr);
	llcon.display.fbaddr = NULL;

	mdelay(300);
	llcon_mem_free();
	return 0;
}

int llcon_init(void)
{
	int rc = 0;
	pgprot_t prot;
	struct page **pages;
	size_t x, fbsize, pc;
	phys_addr_t base;
	phys_addr_t addr;

	if (llcon.mode <= LLCON_MODE_DISABLED || llcon.mode >= LLCON_MODE_MAX) {
		pr_err("LLCON: can't init (mode = %d) \n", llcon.mode);
		return -1;
	}

	if (llcon.display.dt_fbaddr) {
		base = (phys_addr_t)llcon.display.dt_fbaddr;
	} else {
		base = (phys_addr_t)llcon.display.bl_fbaddr;
	}
	if (!base) {
		pr_err("LLCON: can't init (FB addr = NULL) \n");
		return -2;
	}

	prot = pgprot_noncached(PAGE_KERNEL);
	fbsize = llcon.display.height * llcon.display.stride * LLCON_PXL_SIZE;
	pc = (fbsize / PAGE_SIZE) + 1;
	pages = kmalloc(sizeof(struct page *) * pc, GFP_KERNEL);
	if (!pages)
		return -6;
	addr = base;
	for (x = 0; x < pc; x++) {
		pages[x] = pfn_to_page(addr >> PAGE_SHIFT);
		addr += PAGE_SIZE;
	}
	llcon.display.fbaddr = vmap(pages, pc, VM_MAP, prot);
	kfree(pages);

	if (!llcon.display.fbaddr) {
		pr_err("LLCON: can't map virtual memory (FB addr = %p) \n", (void *)base);
		rc = -8;
		goto err;
	}

	if (llcon.delay <= 0)
		llcon.delay = 1;
	if (llcon.delay > 2000)
		llcon.delay = 2000;

	memset(&llcon.log, 0, sizeof(llcon.log));
	if (llcon.mode == LLCON_MODE_ASYNC) {
		llcon.log.bufsize = llcon.res.x * llcon.res.y;
		llcon.log.buf = kmalloc(llcon.log.bufsize + llcon.res.x, GFP_KERNEL);
		if (!llcon.log.buf) {
			rc = -9;
			goto err;
		}
		memset(llcon.log.buf, 0x20, llcon.log.bufsize);
		llcon.task = kthread_create_on_node(llcon_thread, NULL, -1, "llcon");
		if (IS_ERR(llcon.task)) {
			pr_err("LLCON: can't create kthread (err = %d) \n", (int)llcon.task);
			rc = -10;
			goto err;
		}
	}

	pr_info("LLCON: mode: %d, addr: %p (%p), size: %ux%u, pitch: %u,"
		" font: %dx%d [%x], delay: %u \n",
		llcon.mode, (void *)base, llcon.display.fbaddr,
		llcon.display.width, llcon.display.height, llcon.display.stride,
		llcon.font.width, llcon.font.height, llcon.fg_color, llcon.delay);

	mutex_init(&llcon_lock);
	llcon_set_cursor_pos(0, 0);
	llcon_enabled = 1;
	if (llcon.mode == LLCON_MODE_ASYNC)
		wake_up_process(llcon.task);

	return 0;

err:
	llcon_mem_free();
	return rc;
}

void llcon_uninit(void)
{
	if (llcon_enabled) {
		pr_info("LLCON: exit \n");

		switch (llcon.mode) {
		case LLCON_MODE_ASYNC:
			llcon_enabled = 0;
			while (llcon.display.fbaddr) {
				mdelay(5);
			}
			break;

		case LLCON_MODE_SYNC:
			mutex_lock(&llcon_lock);
			if (llcon_enabled) {
				llcon_enabled = 0;
				mdelay(llcon.delay);
				llcon_mem_free();
			}
			mutex_unlock(&llcon_lock);
			break;

		default:
			llcon_enabled = 0;
		}
	}
}
