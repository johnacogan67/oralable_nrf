/*
 * Copyright (c) 2024 WeeGee bv
 */

#include <app/drivers/maxm86161.h>

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
LOG_MODULE_REGISTER(maxm86161, CONFIG_MAXM86161_LOG_LEVEL);

// Use R, IR and Green
#define COLORS 3u

BUILD_ASSERT(MAXM86161_PPG_CONFIG2_NOMINAL_50HZ == 0x08);
BUILD_ASSERT(MAXM86161_FIFO_MAX_BURST_WORDS * MAXM86161_FIFO_WORD_BYTES ==
	     MAXM86161_FIFO_MAX_BURST_BYTES);
BUILD_ASSERT((128 - (COLORS * 20)) == 68);
BUILD_ASSERT(CONFIG_PPG_SAMPLES_PER_FRAME * COLORS <= MAXM86161_FIFO_MAX_BURST_WORDS);

static uint8_t ppg_held[MAXM86161_FIFO_WORD_BYTES * 2];
static uint8_t ppg_held_words;
static bool ppg_phase_known = true;
static bool ppg_fifo_backlog;
static uint32_t ppg_fifo_overflow_events;
static uint32_t ppg_fifo_backlog_events;
static uint32_t ppg_fifo_tag_mismatch_events;

static void ppg_fifo_reset_phase(void)
{
	ppg_held_words = 0;
	ppg_phase_known = true;
	ppg_fifo_backlog = false;
}

bool ppg_sensor_fifo_backlog(void)
{
	return ppg_fifo_backlog;
}

uint32_t ppg_sensor_fifo_overflow_events(void)
{
	return ppg_fifo_overflow_events;
}

uint32_t ppg_sensor_fifo_backlog_events(void)
{
	return ppg_fifo_backlog_events;
}

static uint32_t ppg_fifo_word(const uint8_t *raw)
{
	return ((uint32_t)raw[0] << 16) | ((uint32_t)raw[1] << 8) | raw[2];
}

static uint8_t ppg_fifo_tag(const uint8_t *raw)
{
	return (uint8_t)(ppg_fifo_word(raw) >> MAXM86161_FIFO_TAG_SHIFT);
}

static int ppg_fifo_pull(const struct i2c_dt_spec *i2c, uint8_t *dst, uint8_t words)
{
	if (words == 0u)
	{
		return 0;
	}
	if (words > MAXM86161_FIFO_MAX_BURST_WORDS)
	{
		return -EINVAL;
	}

	return i2c_burst_read_dt(i2c, MAXM86161_REG_FIFO_DATA, dst,
				 (uint32_t)words * MAXM86161_FIFO_WORD_BYTES);
}

static int ppg_config2_readback(const struct i2c_dt_spec *i2c)
{
	uint8_t config2 = 0;
	const int err = i2c_burst_read_dt(i2c, MAXM86161_REG_PPG_CONFIG2, &config2, 1);

	if (err)
	{
		LOG_ERR("Failed to read PPG config 2");
		return err;
	}

	LOG_INF("PPG config2 readback 0x%02x SR=0x%02x AVE=%u", config2,
		(config2 & MAXM86161_PPG_SR_MASK) >> MAXM86161_PPG_SR_SHIFT,
		config2 & MAXM86161_SMP_AVE_MASK);
	if (config2 != MAXM86161_PPG_CONFIG2_NOMINAL_50HZ)
	{
		LOG_ERR("PPG config2 0x%02x is not nominal 50 Hz (0x08)", config2);
		return -EIO;
	}

	return 0;
}

int ppg_sensor_start(const struct i2c_dt_spec *i2c)
{
	int err;

	/* Almost-full spaces left empty. 128 - 60 = 68 (0x44) for 20 triplets. */
	uint8_t fifo_config1 = (128 - (COLORS * CONFIG_PPG_SAMPLES_PER_FRAME));
	err = i2c_burst_write_dt(i2c, MAXM86161_REG_FIFO_CONFIG1, &fifo_config1, 1);
	if (err)
	{
		LOG_ERR("Failed to set FIFO AFULL threshold");
		return err;
	}

	// Set the LED sequence to have red first, IR second and Green last (green = LED1, ir = LED2, red = LED3)
	uint8_t led_seq_reg[3] = {0x23, 0x01, 0x00};
	err = i2c_burst_write_dt(i2c, MAXM86161_REG_LED_SEQ_REG1, led_seq_reg, 3);
	if (err)
	{
		LOG_ERR("Failed to set LED sequence");
		return err;
	}

	// Set the LED range to 31mA
	uint8_t led_range[2] = {0x0, 0x0};
	err = i2c_burst_write_dt(i2c, MAXM86161_REG_LED_RANGE1, led_range, 2);
	if (err)
	{
		LOG_ERR("Failed to set LED range");
		return err;
	}

	// Configure PPG: maximum integration time (117.3μs), 16384nA ADC range
	// Bits 7:6 = 11 (117.3μs integration time - was 01 = 58.7μs)
	// Bits 5:4 = 01 (reserved)
	// Bits 3:2 = 10 (16384nA ADC range for better headroom)
	// Bits 1:0 = 11 (32768nA ADC range for PPG2)
	uint8_t ppg_config1 = 0b11011011;
	err = i2c_burst_write_dt(i2c, MAXM86161_REG_PPG_CONFIG1, &ppg_config1, 1);
	if (err)
	{
		LOG_ERR("Failed to set PPG config 1");
		return err;
	}

	/* Nominal 50 sps, one pulse, no averaging. 0x48 was SR 0x09, not 50 sps. */
	uint8_t ppg_config2 = MAXM86161_PPG_CONFIG2_NOMINAL_50HZ;
	err = i2c_burst_write_dt(i2c, MAXM86161_REG_PPG_CONFIG2, &ppg_config2, 1);
	if (err)
	{
		LOG_ERR("Failed to set PPG config 2");
		return err;
	}

	// LEDs stay off until worn mode or connect-probe sets PA via ppg_set_led_pa().
	uint8_t led_pa[3] = {0, 0, 0};
	err = i2c_burst_write_dt(i2c, MAXM86161_REG_LED1_PA, led_pa, 3);
	if (err)
	{
		LOG_ERR("Failed to set LED pulse amplitude");
		return err;
	}

	// Reset the interrupt status registers by simply reading them
	uint8_t dummy[2];
	err = i2c_burst_read_dt(i2c, MAXM86161_REG_INT_STAT_1, dummy, 2);
	if (err)
	{
		LOG_ERR("Failed to clear FIFO interrupt status");
		return err;
	}

	// Flush the FIFO and configure the interrupt to be cleared when FIFO_DATA register is read
	uint8_t fifo_config2 = 0b00011010;
	err = i2c_burst_write_dt(i2c, MAXM86161_REG_FIFO_CONFIG2, &fifo_config2, 1);
	if (err)
	{
		LOG_ERR("Failed to flush FIFO");
		return err;
	}

	// Enable the FIFO interrupt
	uint8_t int_en_1 = 0b10000000;
	err = i2c_burst_write_dt(i2c, MAXM86161_REG_INT_EN_1, &int_en_1, 1);
	if (err)
	{
		LOG_ERR("Failed to enable FIFO interrupt");
		return err;
	}

	// Enable the PPG sensor in low power mode
	uint8_t system_control = 0b00001100;
	err = i2c_burst_write_dt(i2c, MAXM86161_REG_SYSTEM_CONTROL, &system_control, 1);
	if (err)
	{
		LOG_ERR("Failed to enable PPG sensor");
		return err;
	}

	err = ppg_config2_readback(i2c);
	if (err)
	{
		return err;
	}

	ppg_fifo_reset_phase();
	return 0;
}

int ppg_sensor_stop(const struct i2c_dt_spec *i2c)
{
	int err;
	int ret = 0;

	// Disable the FIFO interrupt
	uint8_t int_en_1 = 0;
	err = i2c_burst_write_dt(i2c, MAXM86161_REG_INT_EN_1, &int_en_1, 1);
	if (err)
	{
		LOG_ERR("Failed to disable FIFO interrupt");
		ret = err;
	}

	// Turn off the LEDs
	uint8_t led_pa[3] = {0, 0, 0};
	err = i2c_burst_write_dt(i2c, MAXM86161_REG_LED1_PA, led_pa, 3);
	if (err)
	{
		LOG_ERR("Failed to turn off LEDs");
		ret = err;
	}

	// Put the sensor in power-save mode
	uint8_t system_control = 0b00000010;
	err = i2c_burst_write_dt(i2c, MAXM86161_REG_SYSTEM_CONTROL, &system_control, 1);
	if (err)
	{
		LOG_ERR("Failed to disable PPG sensor");
		ret = err;
	}

	return ret;
}

#define MAXM86161_SYS_ACTIVE_LP 0x0CU

int ppg_sensor_ensure_awake(const struct i2c_dt_spec *i2c)
{
	uint8_t sys_ctrl;
	int err = i2c_reg_read_byte_dt(i2c, MAXM86161_REG_SYSTEM_CONTROL, &sys_ctrl);

	if (err)
	{
		LOG_ERR("Failed to read system control");
		return err;
	}

	if ((sys_ctrl & MAXM86161_SYS_ACTIVE_LP) == MAXM86161_SYS_ACTIVE_LP)
	{
		return 0;
	}

	uint8_t system_control = MAXM86161_SYS_ACTIVE_LP;
	err = i2c_burst_write_dt(i2c, MAXM86161_REG_SYSTEM_CONTROL, &system_control, 1);
	if (err)
	{
		LOG_ERR("Failed to wake PPG sensor from power-save");
	}

	return err;
}

int ppg_sensor_get_data(const struct i2c_dt_spec *i2c, struct ppg_sample *ppg_data, uint8_t *sample_count)
{
	int err;
	uint8_t overflow = 0;
	uint8_t available = 0;
	uint8_t raw[MAXM86161_FIFO_MAX_BURST_BYTES];
	uint8_t window[MAXM86161_FIFO_MAX_BURST_BYTES + sizeof(ppg_held)];
	uint8_t have;
	uint8_t take;
	uint8_t triplets;
	uint8_t window_words;
	uint8_t index = 0;
	uint8_t produced = 0;

	*sample_count = 0;
	ppg_fifo_backlog = false;

	err = i2c_burst_read_dt(i2c, MAXM86161_REG_FIFO_OVF_CNT, &overflow, 1);
	if (err)
	{
		LOG_ERR("Failed to read FIFO overflow count");
		return err;
	}

	err = i2c_burst_read_dt(i2c, MAXM86161_REG_FIFO_DATA_CNT, &available, 1);
	if (err)
	{
		LOG_ERR("Failed to read FIFO data count");
		return err;
	}

	LOG_DBG("FIFO data count: %u overflow: %u", available, overflow);
	if (overflow != 0u)
	{
		ppg_fifo_overflow_events++;
		ppg_phase_known = false;
		ppg_held_words = 0;
		LOG_WRN("PPG FIFO hardware overflow=%u words=%u", overflow, available);
	}
	else if (available > MAXM86161_FIFO_MAX_BURST_WORDS)
	{
		ppg_fifo_backlog_events++;
		LOG_DBG("PPG FIFO backlog words=%u", available);
	}

	/* After overflow or a bad tag, pull single words until slot LEDC1.
	 * The tag is the sequence slot. LEDC1 is red on this board.
	 * Unaligned words are discarded on purpose. The LEDC1 word is retained.
	 */
	if (!ppg_phase_known)
	{
		const uint8_t steps = available > 6u ? 6u : available;
		bool found = false;

		for (uint8_t n = 0; n < steps; n++)
		{
			uint8_t word[MAXM86161_FIFO_WORD_BYTES];

			err = ppg_fifo_pull(i2c, word, 1);
			if (err)
			{
				LOG_ERR("Failed to read FIFO data");
				return err;
			}
			available--;
			if (ppg_fifo_tag(word) == MAXM86161_FIFO_TAG_LEDC1)
			{
				memcpy(ppg_held, word, sizeof(word));
				ppg_held_words = 1;
				ppg_phase_known = true;
				found = true;
				break;
			}
		}

		if (!found)
		{
			ppg_held_words = 0;
			ppg_fifo_backlog = available > 0u;
			LOG_WRN("PPG FIFO resync found no LEDC1 tag");
			return 0;
		}
	}

	have = ppg_held_words;
	triplets = (uint8_t)((have + available) / COLORS);
	if (triplets > CONFIG_PPG_SAMPLES_PER_FRAME)
	{
		triplets = CONFIG_PPG_SAMPLES_PER_FRAME;
	}
	take = (uint8_t)((triplets * COLORS) - have);
	if (triplets == 0u || take > available)
	{
		take = 0;
	}
	if (take > MAXM86161_FIFO_MAX_BURST_WORDS)
	{
		take = MAXM86161_FIFO_MAX_BURST_WORDS;
	}

	if (take > 0u)
	{
		err = ppg_fifo_pull(i2c, raw, take);
		if (err)
		{
			LOG_ERR("Failed to read FIFO data");
			return err;
		}
	}

	window_words = (uint8_t)(have + take);
	if (have > 0u)
	{
		memcpy(window, ppg_held, (size_t)have * MAXM86161_FIFO_WORD_BYTES);
	}
	if (take > 0u)
	{
		memcpy(&window[have * MAXM86161_FIFO_WORD_BYTES], raw,
		       (size_t)take * MAXM86161_FIFO_WORD_BYTES);
	}
	ppg_held_words = 0;

	while ((uint8_t)(index + 2u) < window_words && produced < CONFIG_PPG_SAMPLES_PER_FRAME)
	{
		const uint8_t *sample = &window[index * MAXM86161_FIFO_WORD_BYTES];
		const uint8_t tag_red = ppg_fifo_tag(sample);
		const uint8_t tag_ir = ppg_fifo_tag(sample + MAXM86161_FIFO_WORD_BYTES);
		const uint8_t tag_green = ppg_fifo_tag(sample + (2u * MAXM86161_FIFO_WORD_BYTES));

		if (tag_red != MAXM86161_FIFO_TAG_LEDC1 || tag_ir != MAXM86161_FIFO_TAG_LEDC2 ||
		    tag_green != MAXM86161_FIFO_TAG_LEDC3)
		{
			ppg_fifo_tag_mismatch_events++;
			ppg_phase_known = false;
			ppg_held_words = 0;
			ppg_fifo_backlog = available > take;
			LOG_WRN("PPG FIFO tag mismatch %u %u %u", tag_red, tag_ir, tag_green);
			*sample_count = produced;
			return 0;
		}

		ppg_data[produced].red = ppg_fifo_word(sample) & MAXM86161_FIFO_ADC_MASK;
		ppg_data[produced].ir =
			ppg_fifo_word(sample + MAXM86161_FIFO_WORD_BYTES) & MAXM86161_FIFO_ADC_MASK;
		ppg_data[produced].green =
			ppg_fifo_word(sample + (2u * MAXM86161_FIFO_WORD_BYTES)) & MAXM86161_FIFO_ADC_MASK;
		LOG_DBG("PPG data: red = %d, ir = %d, green = %d", ppg_data[produced].red,
			ppg_data[produced].ir, ppg_data[produced].green);
		produced++;
		index = (uint8_t)(index + COLORS);
	}

	if (ppg_phase_known && index < window_words)
	{
		const uint8_t left = (uint8_t)(window_words - index);
		const uint8_t *tail = &window[index * MAXM86161_FIFO_WORD_BYTES];
		const bool prefix = left <= 2u && ppg_fifo_tag(tail) == MAXM86161_FIFO_TAG_LEDC1 &&
				    (left == 1u || ppg_fifo_tag(tail + MAXM86161_FIFO_WORD_BYTES) ==
							  MAXM86161_FIFO_TAG_LEDC2);

		if (prefix)
		{
			memcpy(ppg_held, tail, (size_t)left * MAXM86161_FIFO_WORD_BYTES);
			ppg_held_words = left;
		}
		else
		{
			ppg_fifo_tag_mismatch_events++;
			ppg_phase_known = false;
			LOG_WRN("PPG FIFO dropped an unaligned tail of %u words", left);
		}
	}

	*sample_count = produced;
	ppg_fifo_backlog = (uint8_t)(available - take) >= COLORS;
	return 0;
}

int ppg_sensor_read_reg(const struct i2c_dt_spec *i2c, uint8_t reg, uint8_t *data)
{
	int err = i2c_burst_read_dt(i2c, reg, data, 1);
	if (err)
	{
		LOG_ERR("Failed to read register 0x%02x", reg);
	}

	return err;
}

int ppg_sensor_write_reg(const struct i2c_dt_spec *i2c, uint8_t reg, uint8_t data)
{
	int err = i2c_burst_write_dt(i2c, reg, &data, 1);
	if (err)
	{
		LOG_ERR("Failed to write register 0x%02x err=%d", reg, err);
	}

	return err;
}
