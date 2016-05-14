/**
 * @file include/video/llcon.h
 *
 * @brief
 * This file contains the implementation of Low Level Console
 * for output kernel messages to display.
 *
 * @author	remittor <remittor@gmail.com>
 * @date	2016-05-11 
 */

#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/font.h>

#define LLCON_MODE_DISABLED  0
#define LLCON_MODE_SYNC      1
#define LLCON_MODE_ASYNC     2
#define LLCON_MODE_MAX       3

#define LLCON_BLACK     0x000000
#define LLCON_WHITE     0xFFFFFF
#define LLCON_GREEN     0x008000

struct llcon_pos {
	size_t x;
	size_t y;
};

struct llcon_display {
	void * bl_fbaddr;
	void * dt_fbaddr;
	void * fbaddr;
	size_t width;
	size_t height;
	size_t stride;
	size_t bpp;
	size_t pixel_size;
};

struct llcon_log {
	char * buf;
	size_t bufsize;
	size_t pos;
	struct llcon_pos cur;
};

struct llcon_desc {
	int mode;
	int textwrap;
	size_t delay;
	struct llcon_display display;
	struct font_desc font;
	size_t glyph_stride;
	uint32_t fg_color;
	uint32_t bg_color;
	struct llcon_pos cur;
	struct llcon_pos res;
	struct llcon_pos max;
	struct task_struct * task;
	struct llcon_log log;
};

extern volatile int llcon_enabled;

void llcon_clear(void);
void llcon_putc(char c);
void llcon_emit(char c);
void llcon_set_cursor_pos(size_t x, size_t y);
void llcon_set_font_fg_color(uint32_t color);
void llcon_set_font_bg_color(uint32_t color);
void llcon_set_font_color(uint32_t fg, uint32_t bg);
int llcon_set_font_type(const struct font_desc * font);
int llcon_set_font_by_name(const char * fontname);
struct llcon_desc * llcon_get(void);
void llcon_set_fb_addr(void * addr, size_t size);
int llcon_init(void);
void llcon_uninit(void);

static inline void llcon_emit_log_char(char c)
{
	if (llcon_enabled) {
		llcon_emit(c);
	}
}

static inline void llcon_exit(void)
{
	if (llcon_enabled) {
		llcon_uninit();
	}
}
