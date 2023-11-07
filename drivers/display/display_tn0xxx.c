/*
 * Copyright (c) 2023 Alen Daniel
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT kyo_tn0xxx

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(tn0xxx, CONFIG_DISPLAY_LOG_LEVEL);

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/init.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>

/* Driver for Kyocera's 2.16" RESOLUTION MEMORY IN PIXEL (MIP) TFT (TN0216ANVNANN)
 */

/* Note:
 * high/1 means white, low/0 means black
 * SPI interface expects LSB first
 * see more notes in boards/shields/tn0xxx/doc/index.rst
 */

#define TN0XXX_PANEL_WIDTH  DT_INST_PROP(0, width)
#define TN0XXX_PANEL_HEIGHT DT_INST_PROP(0, height)

#define TN0XXX_PIXELS_PER_BYTE 8

#define LCD_ADDRESS_LEN_BITS          8
#define LCD_DUMMY_SPI_CYCLES_LEN_BITS 32
#define ALL_BLACK_BYTE                0x00
#define ALL_WHITE_BYTE                0xFF

/* Data packet format
 * +-------------------+-------------------+--------------------+
 * | line addr (8 bits) | data (8 WIDTH bits) | dummy (32 bits) |
 * +-------------------+-------------------+--------------------+
 */

struct tn0xxx_config_s {
	struct spi_dt_spec bus;
};

// ---------- start of unsupported API ----------

static int tn0xxx_blanking_off(const struct device *dev)
{
	LOG_WRN("Unsupported");
	return -ENOTSUP;
}

static int tn0xxx_blanking_on(const struct device *dev)
{
	LOG_WRN("Unsupported");
	return -ENOTSUP;
}

static int tn0xxx_read(const struct device *dev, const uint16_t x, const uint16_t y,
		       const struct display_buffer_descriptor *desc, void *buf)
{
	LOG_ERR("not supported");
	return -ENOTSUP;
}

static void *tn0xxx_get_framebuffer(const struct device *dev)
{
	LOG_ERR("not supported");
	return NULL;
}

static int tn0xxx_set_brightness(const struct device *dev, const uint8_t brightness)
{
	LOG_WRN("not supported");
	return -ENOTSUP;
}

static int tn0xxx_set_contrast(const struct device *dev, uint8_t contrast)
{
	LOG_WRN("not supported");
	return -ENOTSUP;
}

static int tn0xxx_set_orientation(const struct device *dev,
				  const enum display_orientation orientation)
{
	LOG_ERR("Unsupported");
	return -ENOTSUP;
}

static int tn0xxx_set_pixel_format(const struct device *dev, const enum display_pixel_format pf)
{
	if (pf == PIXEL_FORMAT_MONO01) {
		return 0;
	}

	LOG_ERR("specified pixel format not supported");
	return -ENOTSUP;
}
// ---------- end of unsupported API ----------

static int update_display(const struct device *dev, uint16_t start_line, uint16_t num_lines,
			  const uint8_t *bitmap_buffer)
{

	const struct tn0xxx_config_s *config = dev->config;
	uint8_t single_line_buffer[(TN0XXX_PANEL_HEIGHT + LCD_DUMMY_SPI_CYCLES_LEN_BITS +
				    LCD_ADDRESS_LEN_BITS) /
				   TN0XXX_PIXELS_PER_BYTE];

	uint16_t bitmap_buffer_index = 0;
	for (int column_addr = start_line;
	     column_addr < start_line + num_lines && column_addr < TN0XXX_PANEL_WIDTH;
	     column_addr++) {
		uint8_t buff_index = 0;

		single_line_buffer[buff_index++] = (uint8_t)column_addr;

		for (int i = 0; i < TN0XXX_PANEL_HEIGHT / TN0XXX_PIXELS_PER_BYTE; i++) {
			single_line_buffer[buff_index++] = bitmap_buffer[bitmap_buffer_index++];
		}
		// write 32 dummy bits
		for (int i = 0; i < LCD_DUMMY_SPI_CYCLES_LEN_BITS / TN0XXX_PIXELS_PER_BYTE; i++) {
			single_line_buffer[buff_index++] = ALL_BLACK_BYTE;
		}

		int len = sizeof(single_line_buffer);

		struct spi_buf tx_buf = {.buf = single_line_buffer, .len = len};
		struct spi_buf_set tx_bufs = {.buffers = &tx_buf, .count = 1};

		if (spi_write_dt(&config->bus, &tx_bufs)) {
			LOG_ERR("SPI write to black out screen failed\r\n");

			return 1;
		}
	}

	k_sleep(K_USEC(10)); // SCS low width time per datasheet
	LOG_INF("Display update complete");

	return 0;
}

static int tn0xxx_write(const struct device *dev, const uint16_t x, const uint16_t y,
			const struct display_buffer_descriptor *desc, const void *buf)
{

	LOG_INF("X: %d, Y: %d, W: %d, H: %d, pitch: %d, buf_size: %d", x, y, desc->width,
		desc->height, desc->pitch, desc->buf_size);

	if (buf == NULL) {
		LOG_WRN("Display buffer is not available");
		return -EINVAL;
	}

#if CONFIG_PORTRAIT_MODE || CONFIG_PORTRAIT_MODE_ROTATED_180_DEGREE

	if (desc->height != TN0XXX_PANEL_HEIGHT) {
		LOG_ERR("Height not a multiple of %d", TN0XXX_PANEL_HEIGHT);
		return -EINVAL;
	}

	if (desc->pitch != desc->height) {
		LOG_ERR("Unsupported mode");
		return -ENOTSUP;
	}

	if ((x + desc->width) > TN0XXX_PANEL_WIDTH) {
		LOG_ERR("Buffer out of bounds (width)");
		return -EINVAL;
	}

	if (y != 0) {
		LOG_ERR("y-coordinate has to be 0");
		return -EINVAL;
	}

	/* Adding 1 since line numbering on the display starts with 1 */
	return update_display(dev, y, desc->height, buf);

#endif

	if (desc->width != TN0XXX_PANEL_WIDTH) {
		LOG_ERR("Width not a multiple of %d", TN0XXX_PANEL_WIDTH);
		return -EINVAL;
	}

	// if (desc->pitch != desc->width) {
	// 	LOG_ERR("Unsupported mode");
	// 	return -ENOTSUP;
	// }

	if ((y + desc->height) > TN0XXX_PANEL_HEIGHT) {
		LOG_ERR("Buffer out of bounds (height)");
		return -EINVAL;
	}

	if (x != 0) {
		LOG_ERR("X-coordinate has to be 0");
		return -EINVAL;
	}

	/* Adding 1 since line numbering on the display starts with 1 */
	return update_display(dev, x, desc->width, buf);
}

// #define CONFIG_PORTRAIT_MODE
static void tn0xxx_get_capabilities(const struct device *dev, struct display_capabilities *caps)
{
	memset(caps, 0, sizeof(struct display_capabilities));
	caps->x_resolution = TN0XXX_PANEL_WIDTH;
	caps->y_resolution = TN0XXX_PANEL_HEIGHT;
	caps->supported_pixel_formats = PIXEL_FORMAT_MONO01;
	caps->current_pixel_format = PIXEL_FORMAT_MONO01;
	caps->current_orientation = DISPLAY_ORIENTATION_NORMAL;

#if CONFIG_PORTRAIT_MODE || CONFIG_PORTRAIT_MODE_ROTATED_180_DEGREE
	caps->screen_info = SCREEN_INFO_X_ALIGNMENT_WIDTH;
#else
	caps->screen_info = SCREEN_INFO_Y_ALIGNMENT_WIDTH;
#if CONFIG_PORTRAIT_MODE_ROTATED_180_DEGREE
	caps->screen_info |= SCREEN_INFO_VERTICAL_BIT_ALIGNMENT_ROTATED_180;
#else
	caps->screen_info |= SCREEN_INFO_VERTICAL_BIT_ALIGNMENT;
#endif // CONFIG_PORTRAIT_MODE_ROTATED_180_DEGREE
#endif // CONFIG_PORTRAIT_MODE || CONFIG_PORTRAIT_MODE_ROTATED_180_DEGREE
}

static int tn0xxx_init(const struct device *dev)
{
	const struct tn0xxx_config_s *config = dev->config;

	if (!spi_is_ready_dt(&config->bus)) {
		LOG_ERR("SPI bus %s not ready", config->bus.bus->name);
		return -ENODEV;
	}

	return 0;
}

static const struct tn0xxx_config_s tn0xxx_config = {
	.bus = SPI_DT_SPEC_INST_GET(0,
				    SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_LSB |
					    SPI_CS_ACTIVE_HIGH | SPI_HOLD_ON_CS | SPI_LOCK_ON,
				    2),
};

static struct display_driver_api tn0xxx_driver_api = {
	.blanking_on = tn0xxx_blanking_on,
	.blanking_off = tn0xxx_blanking_off,
	.write = tn0xxx_write,
	.read = tn0xxx_read,
	.get_framebuffer = tn0xxx_get_framebuffer,
	.set_brightness = tn0xxx_set_brightness,
	.set_contrast = tn0xxx_set_contrast,
	.get_capabilities = tn0xxx_get_capabilities,
	.set_pixel_format = tn0xxx_set_pixel_format,
	.set_orientation = tn0xxx_set_orientation,
};

DEVICE_DT_INST_DEFINE(0, tn0xxx_init, NULL, NULL, &tn0xxx_config, POST_KERNEL,
		      CONFIG_DISPLAY_INIT_PRIORITY, &tn0xxx_driver_api);
