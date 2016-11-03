/* drivers/i2c/chips/tps61310_flashlight.c
 *
 * Copyright (C) 2008-2009 HTC Corporation.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#if 0
#include <mach/msm_iomap.h>
#include <mach/devices_cmdline.h>
#include <mach/socinfo.h>
#endif

#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/earlysuspend.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/leds.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/htc_flashlight.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/tca6418_ioexpander.h>

#include <linux/dma-mapping.h>
#include <linux/htc_devices_dtb.h>



#define FLT_DBG_LOG(fmt, ...) \
		printk(KERN_DEBUG "[FLT]TPS " fmt, ##__VA_ARGS__)
#define FLT_INFO_LOG(fmt, ...) \
		printk(KERN_INFO "[FLT]TPS " fmt, ##__VA_ARGS__)
#define FLT_ERR_LOG(fmt, ...) \
		printk(KERN_ERR "[FLT][ERR]TPS " fmt, ##__VA_ARGS__)

#define TPS61310_RETRY_COUNT 10
#define TPS61310_OFF_CURRENT 0
#define TPS61310_MAX_DUAL_FLASH_CURRENT 1500
#define TPS61310_MAX_LED_CURRENT 750
#define TPS61310_MAX_LED13_TORCH_CURRENT 350	
#define TPS61310_MAX_LED2_TORCH_CURRENT 175
#define TPS61310_MAX_LED2_DIVIDER 25
#define TPS61310_MAX_LED13_DIVIDER 50
#define TPS61310_SW_TIMEOUT 600



struct tps61310_data {
	struct led_classdev 		fl_lcdev;
	struct early_suspend		fl_early_suspend;
	enum flashlight_mode_flags 	mode_status;
	uint32_t			flash_sw_timeout;
	struct mutex 			tps61310_data_mutex;
	uint32_t			strb0;
	uint32_t			strb1;
	uint32_t			reset;
	uint8_t 			led_count;
	uint8_t 			mode_pin_suspend_state_low;
	uint8_t				enable_FLT_1500mA;
	uint8_t				disable_tx_mask;
	uint32_t			power_save;
	uint32_t			power_save_2;
	struct tps61310_led_data	*led_array;
};

static struct i2c_client *this_client;
static struct tps61310_data *this_tps61310;
struct delayed_work tps61310_delayed_work;
static struct workqueue_struct *tps61310_work_queue;
static struct mutex tps61310_mutex;

static int switch_state = 1;
static int support_dual_flashlight = 1; 
static int retry = 0;
static int reg_init_fail = 0;

static int regaddr = 0x00;
static int regdata = 0x00;
static int reg_buffered[256] = {0x00};

static int tps61310_i2c_command(uint8_t, uint8_t);
int tps61310_flashlight_control(int);
int tps61310_flashlight_mode(int);
int tps61310_flashlight_mode2(int, int);

static int flashlight_turn_off(void);

#define GTP_DMA_MAX_TRANSACTION_LENGTH 255
#define GTP_DMA_MAX_I2C_TRANSFER_SIZE 255

static u8 *gpDMABuf_va = NULL;
static dma_addr_t *gpDMABuf_pa = 0;
static u8 *gpDMABuf_va1 = NULL;
static dma_addr_t *gpDMABuf_pa1 = 0;
static DEFINE_MUTEX(dma_i2c_lock);


static int i2c_read_dma(struct i2c_client *client, uint8_t addr,
		uint8_t *data, int length)
{
  int ret;
  s32 retry = 0;
  u8 buffer[1];

  struct i2c_msg msg[2] =
  {
	{
	  .addr = (client->addr & I2C_MASK_FLAG),
	  .flags = 0,
	  .buf = buffer,
	  .len = 1,
	  .timing = 100
	},
	{
	  .addr = (client->addr & I2C_MASK_FLAG),
	  .ext_flag = (client->ext_flag | I2C_ENEXT_FLAG | I2C_DMA_FLAG),
	  .flags = I2C_M_RD,
	  .buf = gpDMABuf_pa1,
	  .len = length,
	  .timing = 100
	},
  };
  mutex_lock(&dma_i2c_lock);
  buffer[0] = addr;

  if (data == NULL){
	mutex_unlock(&dma_i2c_lock);
	return -1;
  }
  for (retry = 0; retry < 5; ++retry)
  {
	ret = i2c_transfer(client->adapter, &msg[0], 2);
	if (ret < 0)
	{
	  continue;
	}
	memcpy(data, gpDMABuf_va1, length);
	mutex_unlock(&dma_i2c_lock);
	return 0;
  }
  printk("[FLT] Dma I2C Read Error: 0x%04X, %d byte(s), err-code: %d", addr, length, ret);
  mutex_unlock(&dma_i2c_lock);
  return ret;
}
static int i2c_write_dma(struct i2c_client *client, uint8_t addr,
		uint8_t *data, int length)
{
  int ret;
  s32 retry = 0;
  u8 *wr_buf = gpDMABuf_va1;

  struct i2c_msg msg =
  {
	.addr = (client->addr & I2C_MASK_FLAG),
	.ext_flag = (client->ext_flag | I2C_ENEXT_FLAG | I2C_DMA_FLAG),
	.flags = 0,
	.buf = gpDMABuf_pa1,
	.len = 1 + length,
	.timing = 100
  };

  mutex_lock(&dma_i2c_lock);
  wr_buf[0] = addr;

  if (data == NULL){
	mutex_unlock(&dma_i2c_lock);
	return -1;
  }
  memcpy(wr_buf+1, data, length);
  for (retry = 0; retry < 5; ++retry)
  {
	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0)
	{
	  continue;
	}
	mutex_unlock(&dma_i2c_lock);
	return 0;
  }
  printk("[FLT] Dma I2C Write Error: 0x%04X, %d byte(s), err-code: %d", addr, length, ret);
  mutex_unlock(&dma_i2c_lock);
  return ret;
}


static ssize_t support_dual_flashlight_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return snprintf(buf, sizeof(support_dual_flashlight)+1, "%d\n", support_dual_flashlight);
}

static ssize_t support_dual_flashlight_store(
		struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	int input;
	input = simple_strtoul(buf, NULL, 10);

	if(input >= 0 && input < 2){
		support_dual_flashlight = input;
		FLT_INFO_LOG("%s: %d\n",__func__,support_dual_flashlight);
	}else
		FLT_INFO_LOG("%s: Input out of range\n",__func__);
	return size;
}

static DEVICE_ATTR(support_dual_flashlight, 0664,
		   support_dual_flashlight_show, support_dual_flashlight_store);

static ssize_t poweroff_store(
		struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	int input;
	input = simple_strtoul(buf, NULL, 10);
	FLT_INFO_LOG("%s\n", __func__);

	if(input == 1){
		flashlight_turn_off();
	}else
		FLT_INFO_LOG("%s: Input out of range\n",__func__);

	return size;
}

static DEVICE_ATTR(poweroff, 0220,
		   NULL, poweroff_store);

static ssize_t sw_timeout_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return snprintf(buf, sizeof(this_tps61310->flash_sw_timeout)+1, "%d\n", this_tps61310->flash_sw_timeout);
}
static ssize_t sw_timeout_store(
		struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	int input;
	input = simple_strtoul(buf, NULL, 10);

	if(input >= 0 && input < TPS61310_MAX_DUAL_FLASH_CURRENT){
		this_tps61310->flash_sw_timeout = input;
		FLT_INFO_LOG("%s: %d\n",__func__,this_tps61310->flash_sw_timeout);
	}else
		FLT_INFO_LOG("%s: Input out of range\n",__func__);
	return size;
}
static DEVICE_ATTR(sw_timeout, S_IRUGO | S_IWUSR, sw_timeout_show, sw_timeout_store);

static ssize_t regaddr_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return snprintf(buf, 6, "0x%02x\n", regaddr);
}
static ssize_t regaddr_store(
		struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	int input;
	input = simple_strtoul(buf, NULL, 16);

	if(input >= 0 && input < 256){
		regaddr = input;
		FLT_INFO_LOG("%s: %d\n",__func__,regaddr);
	}else
		FLT_INFO_LOG("%s: Input out of range\n",__func__);
	return size;
}
static DEVICE_ATTR(regaddr, S_IRUGO | S_IWUSR, regaddr_show, regaddr_store);

static ssize_t regdata_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return snprintf(buf, 6, "0x%02x\n", reg_buffered[regaddr]);
}
static ssize_t regdata_store(
		struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	int input;
	input = simple_strtoul(buf, NULL, 16);

	if(input >= 0 && input < 256){
		regdata = input;
		FLT_INFO_LOG("%s: %d\n",__func__,regdata);
		tps61310_i2c_command(regaddr, regdata);
	}else
		FLT_INFO_LOG("%s: Input out of range\n",__func__);

	return size;
}
static DEVICE_ATTR(regdata, S_IRUGO | S_IWUSR, regdata_show, regdata_store);

static ssize_t switch_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return snprintf(buf, sizeof("switch status:") + sizeof(switch_state) + 1, "switch status:%d\n", switch_state);
}

static ssize_t switch_store(
		struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	int switch_status;
	switch_status = -1;
	switch_status = simple_strtoul(buf, NULL, 10);

	if(switch_status >= 0 && switch_status < 2){
		switch_state = switch_status;
		FLT_INFO_LOG("%s: %d\n",__func__,switch_state);
	}else
		FLT_INFO_LOG("%s: Input out of range\n",__func__);
	return size;
}

static DEVICE_ATTR(function_switch, S_IRUGO | S_IWUSR, switch_show, switch_store);

static ssize_t max_current_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	if (this_tps61310->enable_FLT_1500mA)
		return snprintf(buf, 6, "1500\n");
	else
		return snprintf(buf, 5, "750\n");
}
static DEVICE_ATTR(max_current, S_IRUGO | S_IWUSR, max_current_show, NULL);
static ssize_t flash_store(
		struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	int val;
	val = simple_strtoul(buf, NULL, 10);

	if(val >= 0){
		FLT_INFO_LOG("%s: %d\n",__func__,val);
		tps61310_flashlight_mode(val);
	}else
		FLT_INFO_LOG("%s: Input out of range\n",__func__);
	return size;
}
static DEVICE_ATTR(flash, S_IRUGO | S_IWUSR, NULL, flash_store);

static int TPS61310_I2C_TxData(char *txData, int length)
{
	uint8_t loop_i;
	struct i2c_msg msg[] = {
		{
		 .addr = this_client->addr,
		 .flags = 0,
		 .len = length,
		 .buf = txData,
		 },
	};

	for (loop_i = 0; loop_i < TPS61310_RETRY_COUNT; loop_i++) {
		if (i2c_transfer(this_client->adapter, msg, 1) > 0)
			break;

		mdelay(10);
	}

	if (loop_i >= TPS61310_RETRY_COUNT) {
		FLT_ERR_LOG("%s retry over %d\n", __func__,
							TPS61310_RETRY_COUNT);
		return -EIO;
	}

	return 0;
}



static int tps61310_i2c_command(uint8_t address, uint8_t data)
{
	uint8_t buffer[2];
	int ret;
	int err = 0;

	reg_buffered[address] = data;

	buffer[0] = address;
	buffer[1] = data;
	
	ret = i2c_write_dma(this_client, address, &data,1);
	if (ret < 0) {
		FLT_ERR_LOG("%s error\n", __func__);
		if (this_tps61310->reset) {
			FLT_INFO_LOG("reset register");
			ioexp_gpio_set_cfg(this_tps61310->reset, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
			mdelay(10);
			ioexp_gpio_set_cfg(this_tps61310->reset, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
			if (address!=0x07 && address!=0x04) {
				if (this_tps61310->enable_FLT_1500mA) {
					err |= tps61310_i2c_command(0x07, 0x46);
					err |= tps61310_i2c_command(0x04, 0x10);
				} else {
					
					err |= tps61310_i2c_command(0x07, 0xF6);
				}
				if (err)
					reg_init_fail++;
			} else {
				reg_init_fail++;
			}
		}
		return ret;
	}
	return 0;
}

static int flashlight_turn_off(void)
{
	int status;
	FLT_INFO_LOG("%s\n", __func__);
	ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
	ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
	tps61310_i2c_command(0x02, 0x08);
	tps61310_i2c_command(0x01, 0x00);
	FLT_INFO_LOG("%s %d\n", __func__,this_tps61310->mode_status);
	if (this_tps61310->power_save) {
		status = this_tps61310->mode_status;
		if (status == 2 || (status >= 10 && status <=16)) {
			FLT_INFO_LOG("Disable power saving\n");
			ioexp_gpio_set_cfg(this_tps61310->power_save, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
		} else if (status == FL_MODE_PRE_FLASH) {
			FLT_INFO_LOG("Enable power saving\n");
			ioexp_gpio_set_cfg(this_tps61310->power_save, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
		}
	}
	if (this_tps61310->power_save_2) {
		status = this_tps61310->mode_status;
		if (status == 2 || (status >= 10 && status <=16)) {
			FLT_INFO_LOG("Disable power saving\n");
			ioexp_gpio_set_cfg(this_tps61310->power_save_2, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
		} else if (status == FL_MODE_PRE_FLASH) {
			FLT_INFO_LOG("Enable power saving\n");
			ioexp_gpio_set_cfg(this_tps61310->power_save_2, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
		}
	}
	this_tps61310->mode_status = FL_MODE_OFF;
	return 0;
}

static int reboot_notify_sys(struct notifier_block *this,
			      unsigned long event,
			      void *unused)
{
	FLT_INFO_LOG("%s: %ld", __func__, event);
	switch (event) {
		case SYS_RESTART:
		case SYS_HALT:
		case SYS_POWER_OFF:
			flashlight_turn_off();
			return NOTIFY_OK;
	}
	return NOTIFY_DONE;
}

static struct notifier_block reboot_notifier = {
	.notifier_call  = reboot_notify_sys,
};


void retry_flashlight_control(int err, int mode)
{
	if (err && !retry) {
		FLT_INFO_LOG("%s error once\n", __func__);
		retry++;
		mutex_unlock(&tps61310_mutex);
		tps61310_flashlight_control(mode);
		mutex_lock(&tps61310_mutex);
	} else if(err) {
		FLT_INFO_LOG("%s error twice\n", __func__);
		retry = 0;
	}
}

int tps61310_flashlight_mode(int mode)
{
	int err = 0;
	uint8_t current_hex = 0x0;
	FLT_INFO_LOG("camera flash current %d. ver: 0220\n", mode);
	mutex_lock(&tps61310_mutex);
	if (this_tps61310->reset && reg_init_fail) {
		reg_init_fail = 0;
		if (this_tps61310->enable_FLT_1500mA) {
			err |= tps61310_i2c_command(0x07, 0x46);
			err |= tps61310_i2c_command(0x04, 0x10);
		} else {
			
			err |= tps61310_i2c_command(0x07, 0xF6);
		}
	}
	if (err) {
		FLT_ERR_LOG("%s error init register\n", __func__);
		reg_init_fail = 0;
		mutex_unlock(&tps61310_mutex);
		return -err;
	}
#if defined CONFIG_FLASHLIGHT_1500mA
	if( mode < TPS61310_OFF_CURRENT )
	{
		FLT_INFO_LOG("flashlight current < 0, set current to 0.\n");
		mode = TPS61310_OFF_CURRENT;
	}
	else if( mode > TPS61310_MAX_DUAL_FLASH_CURRENT )
	{
		FLT_INFO_LOG("flashlight current > 1500, set current to 1500.\n");
		mode = TPS61310_MAX_DUAL_FLASH_CURRENT;
	}
	if (mode == TPS61310_OFF_CURRENT)
		flashlight_turn_off();
	else if (mode > TPS61310_OFF_CURRENT) {
		FLT_INFO_LOG("flash 1.5A\n");
		if (mode >= TPS61310_MAX_LED_CURRENT) {
			current_hex = (mode - TPS61310_MAX_LED_CURRENT) / TPS61310_MAX_LED13_DIVIDER;
			current_hex += 0x80;
			tps61310_i2c_command(0x05, 0x6F);
			tps61310_i2c_command(0x00, 0x00);
			tps61310_i2c_command(0x01, 0x9E);
			tps61310_i2c_command(0x02, current_hex);
		} else {
			current_hex =  mode / TPS61310_MAX_LED2_DIVIDER;
			current_hex += 0x80;
			tps61310_i2c_command(0x05, 0x6A);
			tps61310_i2c_command(0x00, 0x00);
			tps61310_i2c_command(0x01, current_hex);
		}
		ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
		ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
		queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
				   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
	}
#endif
#if !defined CONFIG_FLASHLIGHT_1500mA
	if( mode < TPS61310_OFF_CURRENT )
	{
		FLT_INFO_LOG("flashlight current < 0, set current to 0.\n");
		mode = TPS61310_OFF_CURRENT;
	}
	else if( mode > TPS61310_MAX_LED_CURRENT )
	{
		FLT_INFO_LOG("flashlight current > 750, set current to 750.\n");
		mode = TPS61310_MAX_LED_CURRENT;
	}
	if (mode == TPS61310_OFF_CURRENT)
		flashlight_turn_off();
	else if (mode > TPS61310_OFF_CURRENT) {
		current_hex =  mode / TPS61310_MAX_LED2_DIVIDER;
		current_hex += 0x80;
		tps61310_i2c_command(0x05, 0x6F);
		tps61310_i2c_command(0x00, 0x00);
		tps61310_i2c_command(0x01, current_hex);
		ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
		ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
		queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
				   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
	}
#endif
	mutex_unlock(&tps61310_mutex);
	return 0;
}

int tps61310_flashlight_mode2(int mode2, int mode13)
{
	int err = 0;
	uint8_t current_hex = 0x0;
	FLT_INFO_LOG("camera flash current %d+%d. ver: 0220\n", mode2, mode13);
	if( mode2 < TPS61310_OFF_CURRENT )
	{
		FLT_INFO_LOG("led2 current < 0, set current to 0.\n");
		mode2 = TPS61310_OFF_CURRENT;
	}
	else if( mode2 > TPS61310_MAX_LED_CURRENT )
	{
		FLT_INFO_LOG("led2 current > 750, set current to 750.\n");
		mode2 = TPS61310_MAX_LED_CURRENT;
	}
	if( mode13 < TPS61310_OFF_CURRENT )
	{
		FLT_INFO_LOG("led13 current < 0, set current to 0.\n");
		mode13 = TPS61310_OFF_CURRENT;
	}
	else if( mode13 > TPS61310_MAX_LED_CURRENT )
	{
		FLT_INFO_LOG("led13 current > 750, set current to 750.\n");
		mode13 = TPS61310_MAX_LED_CURRENT;
	}
	mutex_lock(&tps61310_mutex);
	if (this_tps61310->reset && reg_init_fail) {
		reg_init_fail = 0;
		if (this_tps61310->enable_FLT_1500mA) {
			err |= tps61310_i2c_command(0x07, 0x46);
			err |= tps61310_i2c_command(0x04, 0x10);
		} else {
			
			err |= tps61310_i2c_command(0x07, 0xF6);
		}
	}
	if (err) {
		FLT_ERR_LOG("%s error init register\n", __func__);
		reg_init_fail = 0;
		mutex_unlock(&tps61310_mutex);
		return -err;
	}
	if (mode2 == TPS61310_OFF_CURRENT && mode13 == TPS61310_OFF_CURRENT)
		flashlight_turn_off();
	else {
		uint8_t enled = 0x68;
		FLT_INFO_LOG("dual flash mode2\n");
		if (mode13) {
			current_hex = mode13 / TPS61310_MAX_LED13_DIVIDER;
			current_hex += 0x80;
			enled |= 0x05;
			tps61310_i2c_command(0x05, enled);
			tps61310_i2c_command(0x00, 0x00);
			tps61310_i2c_command(0x02, current_hex);
			printk("set led13 current to 0x%x.\r\n", current_hex);
		}
		if (mode2) {
			current_hex = mode2 / TPS61310_MAX_LED2_DIVIDER;
			current_hex += 0x80;
			enled |= 0x02;
			tps61310_i2c_command(0x05, enled);
			tps61310_i2c_command(0x00, 0x00);
			tps61310_i2c_command(0x01, current_hex);
			printk("set led2 current to 0x%x.\r\n", current_hex);
		}
		ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
		ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
		queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
				   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
	}
	mutex_unlock(&tps61310_mutex);
	return 0;
}

int tps61310_torch_mode2(int mode2, int mode13)
{
	int err = 0;
	uint8_t current_hex = 0x0;
	FLT_INFO_LOG("camera torch current %d+%d.\n", mode2, mode13);
	if( mode2 < TPS61310_OFF_CURRENT )
	{
		FLT_INFO_LOG("torch current < 0, set current 1 to 0.\n");
		mode2 = TPS61310_OFF_CURRENT;
	}
	else if( mode2 > TPS61310_MAX_LED2_TORCH_CURRENT )
	{
		FLT_INFO_LOG("torch current > 175, set current 1 to 175.\n");
		mode2 = TPS61310_MAX_LED2_TORCH_CURRENT;
	}
	if( mode13 < TPS61310_OFF_CURRENT )
	{
		FLT_INFO_LOG("torch current < 0, set current 2 to 0.\n");
		mode13 = TPS61310_OFF_CURRENT;
	}
	else if( mode13 > TPS61310_MAX_LED13_TORCH_CURRENT )
	{
		FLT_INFO_LOG("torch current > 175, set current 2 to 175.\n");
		mode13 = TPS61310_MAX_LED13_TORCH_CURRENT;
	}
	mutex_lock(&tps61310_mutex);
	if (this_tps61310->reset && reg_init_fail) {
		reg_init_fail = 0;
		if (this_tps61310->enable_FLT_1500mA) {
			err |= tps61310_i2c_command(0x07, 0x46);
			err |= tps61310_i2c_command(0x04, 0x10);
		} else {
			
			err |= tps61310_i2c_command(0x07, 0xF6);
		}
	}
	if (err) {
		FLT_ERR_LOG("%s error init register\n", __func__);
		reg_init_fail = 0;
		mutex_unlock(&tps61310_mutex);
		return -err;
	}
	if (mode2 == TPS61310_OFF_CURRENT && mode13 == TPS61310_OFF_CURRENT)
		flashlight_turn_off();
	else {
		uint8_t enled = 0x6F;
		FLT_INFO_LOG("dual torch mode2\n");

		current_hex = (( mode13 / TPS61310_MAX_LED13_DIVIDER ) << 3 ) + ( mode2 / TPS61310_MAX_LED2_DIVIDER );

		tps61310_i2c_command(0x05, enled);
		tps61310_i2c_command(0x00, current_hex);

		ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
		ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
		tps61310_i2c_command(0x01, 0x40);
	}
	mutex_unlock(&tps61310_mutex);
	return 0;
}


int tps61310_flashlight_control(int mode)
{
	int ret = 0;
	int err = 0;
	int rc = 0;

	rc = cancel_delayed_work_sync(&tps61310_delayed_work);
	if (rc)
		FLT_INFO_LOG("tps61310_delayed_work is cancelled\n");
	mutex_lock(&tps61310_mutex);
	if (this_tps61310->reset && reg_init_fail) {
		reg_init_fail = 0;
		if (this_tps61310->enable_FLT_1500mA) {
			err |= tps61310_i2c_command(0x07, 0x46);
			err |= tps61310_i2c_command(0x04, 0x10);
		} else {
			
			err |= tps61310_i2c_command(0x07, 0xF6);
		}
	}
	if (err) {
		FLT_ERR_LOG("%s error init register\n", __func__);
		reg_init_fail = 0;
		mutex_unlock(&tps61310_mutex);
		return -err;
	}
	if ( support_dual_flashlight ) {
			switch (mode) {
			case FL_MODE_OFF:
				flashlight_turn_off();
			break;
			case FL_MODE_FLASH:
				FLT_INFO_LOG("flash 1.5A\n");
				tps61310_i2c_command(0x05, 0x6F);
				tps61310_i2c_command(0x00, 0x00);
				tps61310_i2c_command(0x01, 0x9E);
				tps61310_i2c_command(0x02, 0x8F);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
						   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
			break;
			
			
			case FL_MODE_FLASH_LEVEL1:
					tps61310_i2c_command(0x05, 0x6A);
					tps61310_i2c_command(0x00, 0x00);
					tps61310_i2c_command(0x01, 0x86);
					ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
					ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
					queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
							   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
			break;
			case FL_MODE_FLASH_LEVEL2:
					tps61310_i2c_command(0x05, 0x6A);
					tps61310_i2c_command(0x00, 0x00);
					tps61310_i2c_command(0x01, 0x88);
					ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
					ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
					queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
							   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
			break;
			case FL_MODE_FLASH_LEVEL3:
					tps61310_i2c_command(0x05, 0x6A);
					tps61310_i2c_command(0x00, 0x00);
					tps61310_i2c_command(0x01, 0x8C);
					ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
					ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
					queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
							   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
			break;
			case FL_MODE_FLASH_LEVEL4:
					tps61310_i2c_command(0x05, 0x6A);
					tps61310_i2c_command(0x00, 0x00);
					tps61310_i2c_command(0x01, 0x90);
					ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
					ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
					queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
							   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
			break;
			case FL_MODE_FLASH_LEVEL5:
					tps61310_i2c_command(0x05, 0x6A);
					tps61310_i2c_command(0x00, 0x00);
					tps61310_i2c_command(0x01, 0x94);
					ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
					ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
					queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
							   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
			break;
			case FL_MODE_FLASH_LEVEL6:
					tps61310_i2c_command(0x05, 0x6A);
					tps61310_i2c_command(0x00, 0x00);
					tps61310_i2c_command(0x01, 0x98);
					ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
					ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
					queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
							   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
			break;
			case FL_MODE_FLASH_LEVEL7:
					tps61310_i2c_command(0x05, 0x6A);
					tps61310_i2c_command(0x00, 0x00);
					tps61310_i2c_command(0x01, 0x9C);
					ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
					ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
					queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
							   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
			break;
			case FL_MODE_PRE_FLASH:
				tps61310_i2c_command(0x05, 0x6F);
				tps61310_i2c_command(0x00, 0x0D);
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				tps61310_i2c_command(0x01, 0x40);
				tps61310_i2c_command(0x02, 0x40);
			break;
			case FL_MODE_VIDEO_TORCH:
				tps61310_i2c_command(0x05, 0x6F);
				tps61310_i2c_command(0x00, 0x0D);
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				tps61310_i2c_command(0x01, 0x40);
			break;
			case FL_MODE_VIDEO_TORCH_1:
				tps61310_i2c_command(0x05, 0x6F);
				tps61310_i2c_command(0x00, 0x0A);
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				tps61310_i2c_command(0x01, 0x40);
			break;
			case FL_MODE_VIDEO_TORCH_2:
				tps61310_i2c_command(0x05, 0x6F);
				tps61310_i2c_command(0x00, 0x0B);
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				tps61310_i2c_command(0x01, 0x40);
			break;
			case FL_MODE_VIDEO_TORCH_3:
				tps61310_i2c_command(0x05, 0x6F);
				tps61310_i2c_command(0x00, 0x0C);
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				tps61310_i2c_command(0x01, 0x40);
			break;
			case FL_MODE_VIDEO_TORCH_4:
				tps61310_i2c_command(0x05, 0x6F);
				tps61310_i2c_command(0x00, 0x0D);
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				tps61310_i2c_command(0x01, 0x40);
			break;
			case FL_MODE_TORCH:
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				err |= tps61310_i2c_command(0x05, 0x6F);
				err |= tps61310_i2c_command(0x00, 0x0E);
				err |= tps61310_i2c_command(0x01, 0x40);
				if (this_tps61310->reset)
					retry_flashlight_control(err, mode);
			break;
			case FL_MODE_TORCH_LEVEL_1:
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				err |= tps61310_i2c_command(0x05, 0x6F);
				err |= tps61310_i2c_command(0x00, 0x0A);
				err |= tps61310_i2c_command(0x01, 0x40);
				if (this_tps61310->reset)
					retry_flashlight_control(err, mode);
			break;
			case FL_MODE_TORCH_LEVEL_2:
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				err |= tps61310_i2c_command(0x05, 0x6F);
				err |= tps61310_i2c_command(0x00, 0x0C);
				err |= tps61310_i2c_command(0x01, 0x40);
				if (this_tps61310->reset)
					retry_flashlight_control(err, mode);
			break;
			default:
				FLT_ERR_LOG("%s: unknown flash_light flags: %d\n",
									__func__, mode);
				ret = -EINVAL;
			break;
			}
	} else if (this_tps61310->led_count == 1) {
		if (this_tps61310->enable_FLT_1500mA) {
#if defined CONFIG_FLASHLIGHT_1500mA
			switch (mode) {
			case FL_MODE_OFF:
				flashlight_turn_off();
			break;
			case FL_MODE_FLASH:
				FLT_INFO_LOG("flash 1.5A\n");
				tps61310_i2c_command(0x05, 0x6F);
				tps61310_i2c_command(0x00, 0x00);
				tps61310_i2c_command(0x01, 0x9E);
				tps61310_i2c_command(0x02, 0x8F);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
						   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
			break;
			case FL_MODE_FLASH_LEVEL1:
					tps61310_i2c_command(0x05, 0x6A);
					tps61310_i2c_command(0x00, 0x00);
					tps61310_i2c_command(0x01, 0x86);
					ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
					ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
					queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
							   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
			break;
			case FL_MODE_FLASH_LEVEL2:
					tps61310_i2c_command(0x05, 0x6A);
					tps61310_i2c_command(0x00, 0x00);
					tps61310_i2c_command(0x01, 0x88);
					ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
					ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
					queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
							   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
			break;
			case FL_MODE_FLASH_LEVEL3:
					tps61310_i2c_command(0x05, 0x6A);
					tps61310_i2c_command(0x00, 0x00);
					tps61310_i2c_command(0x01, 0x8C);
					ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
					ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
					queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
							   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
			break;
			case FL_MODE_FLASH_LEVEL4:
					tps61310_i2c_command(0x05, 0x6A);
					tps61310_i2c_command(0x00, 0x00);
					tps61310_i2c_command(0x01, 0x90);
					ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
					ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
					queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
							   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
			break;
			case FL_MODE_FLASH_LEVEL5:
					tps61310_i2c_command(0x05, 0x6A);
					tps61310_i2c_command(0x00, 0x00);
					tps61310_i2c_command(0x01, 0x94);
					ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
					ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
					queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
							   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
			break;
			case FL_MODE_FLASH_LEVEL6:
					tps61310_i2c_command(0x05, 0x6A);
					tps61310_i2c_command(0x00, 0x00);
					tps61310_i2c_command(0x01, 0x98);
					ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
					ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
					queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
							   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
			break;
			case FL_MODE_FLASH_LEVEL7:
					tps61310_i2c_command(0x05, 0x6A);
					tps61310_i2c_command(0x00, 0x00);
					tps61310_i2c_command(0x01, 0x9C);
					ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
					ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
					queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
							   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
			break;
			case FL_MODE_PRE_FLASH:
				tps61310_i2c_command(0x05, 0x6A);
				tps61310_i2c_command(0x00, 0x04);
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				tps61310_i2c_command(0x01, 0x40);
			break;
			case FL_MODE_VIDEO_TORCH:
				tps61310_i2c_command(0x05, 0x6A);
				tps61310_i2c_command(0x00, 0x04);
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				tps61310_i2c_command(0x01, 0x40);
			break;
			case FL_MODE_VIDEO_TORCH_1:
				tps61310_i2c_command(0x05, 0x6A);
				tps61310_i2c_command(0x00, 0x01);
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				tps61310_i2c_command(0x01, 0x40);
			break;
			case FL_MODE_VIDEO_TORCH_2:
				tps61310_i2c_command(0x05, 0x6A);
				tps61310_i2c_command(0x00, 0x02);
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				tps61310_i2c_command(0x01, 0x40);
			break;
			case FL_MODE_VIDEO_TORCH_3:
				tps61310_i2c_command(0x05, 0x6A);
				tps61310_i2c_command(0x00, 0x03);
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				tps61310_i2c_command(0x01, 0x40);
			break;
			case FL_MODE_VIDEO_TORCH_4:
				tps61310_i2c_command(0x05, 0x6A);
				tps61310_i2c_command(0x00, 0x04);
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				tps61310_i2c_command(0x01, 0x40);
			break;
			case FL_MODE_TORCH:
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				err |= tps61310_i2c_command(0x05, 0x6A);
				err |= tps61310_i2c_command(0x00, 0x05);
				err |= tps61310_i2c_command(0x01, 0x40);
				if (this_tps61310->reset)
					retry_flashlight_control(err, mode);
			break;
			case FL_MODE_TORCH_LEVEL_1:
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				err |= tps61310_i2c_command(0x05, 0x6A);
				err |= tps61310_i2c_command(0x00, 0x01);
				err |= tps61310_i2c_command(0x01, 0x40);
				if (this_tps61310->reset)
					retry_flashlight_control(err, mode);
			break;
			case FL_MODE_TORCH_LEVEL_2:
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				err |= tps61310_i2c_command(0x05, 0x6A);
				err |= tps61310_i2c_command(0x00, 0x03);
				err |= tps61310_i2c_command(0x01, 0x40);
				if (this_tps61310->reset)
					retry_flashlight_control(err, mode);
			break;
			default:
				FLT_ERR_LOG("%s: unknown flash_light flags: %d\n",
									__func__, mode);
				ret = -EINVAL;
			break;
			}
#endif
		} else {
#if !defined CONFIG_FLASHLIGHT_1500mA
			switch (mode) {
			case FL_MODE_OFF:
				flashlight_turn_off();
			break;
			case FL_MODE_FLASH:
				tps61310_i2c_command(0x05, 0x6A);
				tps61310_i2c_command(0x00, 0x00);
				tps61310_i2c_command(0x01, 0x9E);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
						   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
			break;
			case FL_MODE_FLASH_LEVEL1:
					tps61310_i2c_command(0x05, 0x6A);
					tps61310_i2c_command(0x00, 0x00);
					tps61310_i2c_command(0x01, 0x86);
					ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
					ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
					queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
							   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
			break;
			case FL_MODE_FLASH_LEVEL2:
					tps61310_i2c_command(0x05, 0x6A);
					tps61310_i2c_command(0x00, 0x00);
					tps61310_i2c_command(0x01, 0x88);
					ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
					ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
					queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
							   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
			break;
			case FL_MODE_FLASH_LEVEL3:
					tps61310_i2c_command(0x05, 0x6A);
					tps61310_i2c_command(0x00, 0x00);
					tps61310_i2c_command(0x01, 0x8C);
					ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
					ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
					queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
							   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
			break;
			case FL_MODE_FLASH_LEVEL4:
					tps61310_i2c_command(0x05, 0x6A);
					tps61310_i2c_command(0x00, 0x00);
					tps61310_i2c_command(0x01, 0x90);
					ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
					ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
					queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
							   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
			break;
			case FL_MODE_FLASH_LEVEL5:
					tps61310_i2c_command(0x05, 0x6A);
					tps61310_i2c_command(0x00, 0x00);
					tps61310_i2c_command(0x01, 0x94);
					ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
					ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
					queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
							   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
			break;
			case FL_MODE_FLASH_LEVEL6:
					tps61310_i2c_command(0x05, 0x6A);
					tps61310_i2c_command(0x00, 0x00);
					tps61310_i2c_command(0x01, 0x98);
					ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
					ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
					queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
							   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
			break;
			case FL_MODE_FLASH_LEVEL7:
					tps61310_i2c_command(0x05, 0x6A);
					tps61310_i2c_command(0x00, 0x00);
					tps61310_i2c_command(0x01, 0x9C);
					ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
					ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
					queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
							   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
			break;
			case FL_MODE_PRE_FLASH:
				tps61310_i2c_command(0x05, 0x6A);
				tps61310_i2c_command(0x00, 0x04);
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				tps61310_i2c_command(0x01, 0x40);
			break;
			case FL_MODE_VIDEO_TORCH:
				tps61310_i2c_command(0x05, 0x6A);
				tps61310_i2c_command(0x00, 0x04);
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				tps61310_i2c_command(0x01, 0x40);
			break;
			case FL_MODE_VIDEO_TORCH_1:
				tps61310_i2c_command(0x05, 0x6A);
				tps61310_i2c_command(0x00, 0x01);
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				tps61310_i2c_command(0x01, 0x40);
			break;
			case FL_MODE_VIDEO_TORCH_2:
				tps61310_i2c_command(0x05, 0x6A);
				tps61310_i2c_command(0x00, 0x02);
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				tps61310_i2c_command(0x01, 0x40);
			break;
			case FL_MODE_VIDEO_TORCH_3:
				tps61310_i2c_command(0x05, 0x6A);
				tps61310_i2c_command(0x00, 0x03);
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				tps61310_i2c_command(0x01, 0x40);
			break;
			case FL_MODE_VIDEO_TORCH_4:
				tps61310_i2c_command(0x05, 0x6A);
				tps61310_i2c_command(0x00, 0x04);
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				tps61310_i2c_command(0x01, 0x40);
			break;
			case FL_MODE_TORCH:
				tps61310_i2c_command(0x05, 0x6A);
				tps61310_i2c_command(0x00, 0x05);
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				tps61310_i2c_command(0x01, 0x40);
			break;
			case FL_MODE_TORCH_LEVEL_1:
				tps61310_i2c_command(0x05, 0x6A);
				tps61310_i2c_command(0x00, 0x01);
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				tps61310_i2c_command(0x01, 0x40);
			break;
			case FL_MODE_TORCH_LEVEL_2:
				tps61310_i2c_command(0x05, 0x6A);
				tps61310_i2c_command(0x00, 0x03);
				ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
				ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
				tps61310_i2c_command(0x01, 0x40);
			break;
			default:
				FLT_ERR_LOG("%s: unknown flash_light flags: %d\n",
									__func__, mode);
				ret = -EINVAL;
			break;
			}
#endif
		}
	} else if (this_tps61310->led_count == 2) {
#if defined CONFIG_TWO_FLASHLIGHT
	switch (mode) {
	case FL_MODE_OFF:
		flashlight_turn_off();
	break;
	case FL_MODE_FLASH:
		tps61310_i2c_command(0x05, 0x6B);
		tps61310_i2c_command(0x00, 0x00);
		tps61310_i2c_command(0x02, 0x90);
		tps61310_i2c_command(0x01, 0x90);
		ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
		ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
		queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
				   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
	break;
	case FL_MODE_FLASH_LEVEL1:
			tps61310_i2c_command(0x05, 0x6B);
			tps61310_i2c_command(0x00, 0x00);
			tps61310_i2c_command(0x02, 0x83);
			tps61310_i2c_command(0x01, 0x83);
			ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
			ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
			queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
					   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
	break;
	case FL_MODE_FLASH_LEVEL2:
			tps61310_i2c_command(0x05, 0x6B);
			tps61310_i2c_command(0x00, 0x00);
			tps61310_i2c_command(0x02, 0x84);
			tps61310_i2c_command(0x01, 0x84);
			ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
			ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
			queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
					   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
	break;
	case FL_MODE_FLASH_LEVEL3:
			tps61310_i2c_command(0x05, 0x6B);
			tps61310_i2c_command(0x00, 0x00);
			tps61310_i2c_command(0x02, 0x86);
			tps61310_i2c_command(0x01, 0x86);
			ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
			ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
			queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
					   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
	break;
	case FL_MODE_FLASH_LEVEL4:
			tps61310_i2c_command(0x05, 0x6B);
			tps61310_i2c_command(0x00, 0x00);
			tps61310_i2c_command(0x02, 0x88);
			tps61310_i2c_command(0x01, 0x88);
			ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
			ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
			queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
					   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
	break;
	case FL_MODE_FLASH_LEVEL5:
			tps61310_i2c_command(0x05, 0x6B);
			tps61310_i2c_command(0x00, 0x00);
			tps61310_i2c_command(0x02, 0x8A);
			tps61310_i2c_command(0x01, 0x8A);
			ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
			ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
			queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
					   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
	break;
	case FL_MODE_FLASH_LEVEL6:
			tps61310_i2c_command(0x05, 0x6B);
			tps61310_i2c_command(0x00, 0x00);
			tps61310_i2c_command(0x02, 0x8C);
			tps61310_i2c_command(0x01, 0x8C);
			ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
			ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
			queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
					   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
	break;
	case FL_MODE_FLASH_LEVEL7:
			tps61310_i2c_command(0x05, 0x6B);
			tps61310_i2c_command(0x00, 0x00);
			tps61310_i2c_command(0x02, 0x8E);
			tps61310_i2c_command(0x01, 0x8E);
			ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
			ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
			queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
					   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
	break;
	case FL_MODE_PRE_FLASH:
		tps61310_i2c_command(0x05, 0x6B);
		tps61310_i2c_command(0x00, 0x12);
		ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
		ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
		tps61310_i2c_command(0x01, 0x40);
	break;
	case FL_MODE_VIDEO_TORCH:
		tps61310_i2c_command(0x05, 0x6B);
		tps61310_i2c_command(0x00, 0x12);
		ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
		ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
		tps61310_i2c_command(0x01, 0x40);
	break;
	case FL_MODE_TORCH:
		tps61310_i2c_command(0x05, 0x6B);
		tps61310_i2c_command(0x00, 0x1B);
		ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
		ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
		tps61310_i2c_command(0x01, 0x40);
	break;
	case FL_MODE_TORCH_LEVEL_1:
		tps61310_i2c_command(0x05, 0x6B);
		tps61310_i2c_command(0x00, 0x09);
		ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
		ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
		tps61310_i2c_command(0x01, 0x40);
	break;
	case FL_MODE_TORCH_LEVEL_2:
		tps61310_i2c_command(0x05, 0x6B);
		tps61310_i2c_command(0x00, 0x12);
		ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
		ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
		tps61310_i2c_command(0x01, 0x40);
	break;
	case FL_MODE_TORCH_LED_A:
		tps61310_i2c_command(0x05, 0x69);
		tps61310_i2c_command(0x00, 0x09);
		ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
		ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
		tps61310_i2c_command(0x01, 0x40);
	break;
	case FL_MODE_TORCH_LED_B:
		tps61310_i2c_command(0x05, 0x6A);
		tps61310_i2c_command(0x00, 0x09);
		ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
		ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
		tps61310_i2c_command(0x01, 0x40);
	break;

	default:
		FLT_ERR_LOG("%s: unknown flash_light flags: %d\n",
							__func__, mode);
		ret = -EINVAL;
	break;
	}
#endif
		}

	FLT_INFO_LOG("%s: mode: %d\n", __func__, mode);
	this_tps61310->mode_status = mode;
	mutex_unlock(&tps61310_mutex);

	return ret;
}


#if defined(CONFIG_HTC_FLASHLIGHT_COMMON)
static int tps61310_flt_flash_adapter(int mA1, int mA2){
	return tps61310_flashlight_mode2(mA1,mA2);
}

static int tps61310_flt_torch_adapter(int mA1, int mA2){
	return tps61310_torch_mode2(mA1,mA2);
}
#endif

static void fl_lcdev_brightness_set(struct led_classdev *led_cdev,
						enum led_brightness brightness)
{
	enum flashlight_mode_flags mode;
	int ret = -1;


	if (brightness > TPS61310_OFF_CURRENT && brightness <= LED_HALF) {
		if (brightness == (LED_HALF - 2))
			mode = FL_MODE_TORCH_LEVEL_1;
		else if (brightness == (LED_HALF - 1))
			mode = FL_MODE_TORCH_LEVEL_2;
		else if (brightness == 1 && this_tps61310->led_count ==2)
			mode = FL_MODE_TORCH_LED_A;
		else if (brightness == 2 && this_tps61310->led_count ==2)
			mode = FL_MODE_TORCH_LED_B;
		else
			mode = FL_MODE_TORCH;
	} else if (brightness > LED_HALF && brightness <= LED_FULL) {
		if (brightness == (LED_HALF + 1))
			mode = FL_MODE_PRE_FLASH; 
		else if (brightness == (LED_HALF + 3))
			mode = FL_MODE_FLASH_LEVEL1; 
		else if (brightness == (LED_HALF + 4))
			mode = FL_MODE_FLASH_LEVEL2; 
		else if (brightness == (LED_HALF + 5))
			mode = FL_MODE_FLASH_LEVEL3; 
		else if (brightness == (LED_HALF + 6))
			mode = FL_MODE_FLASH_LEVEL4; 
		else if (brightness == (LED_HALF + 7))
			mode = FL_MODE_FLASH_LEVEL5; 
		else if (brightness == (LED_HALF + 8))
			mode = FL_MODE_FLASH_LEVEL6; 
		else if (brightness == (LED_HALF + 9))
			mode = FL_MODE_FLASH_LEVEL7; 
		else
			mode = FL_MODE_FLASH; 
	} else
		
		mode = FL_MODE_OFF;

	if ((mode != FL_MODE_OFF) && switch_state == 0){
		FLT_INFO_LOG("%s flashlight is disabled by switch, mode = %d\n",__func__, mode);
		return;
	}

	retry = 0;
	ret = tps61310_flashlight_control(mode);
	if (ret) {
		FLT_ERR_LOG("%s: control failure rc:%d\n", __func__, ret);
		return;
	}
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void flashlight_early_suspend(struct early_suspend *handler)
{
	FLT_INFO_LOG("%s\n", __func__);
	if (this_tps61310->mode_status)
		flashlight_turn_off();
	if (this_tps61310->power_save)
		ioexp_gpio_set_cfg(this_tps61310->power_save, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
	if (this_tps61310->power_save_2)
		ioexp_gpio_set_cfg(this_tps61310->power_save_2, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
}

static void flashlight_late_resume(struct early_suspend *handler)
{
}
#endif

static void flashlight_turn_off_work(struct work_struct *work)
{
	FLT_INFO_LOG("%s\n", __func__);
	flashlight_turn_off();
}

static int tps61310_parse_dt(struct device *dev, struct TPS61310_flashlight_platform_data *pdata)
{
	struct property *prop;
	struct device_node *dt = dev->of_node;
	prop = of_find_property(dt, "tps61310,tps61310_strb0", NULL);
	if (prop) {
		
		of_property_read_u32(dt, "tps61310,tps61310_strb0", &pdata->tps61310_strb0);
	}
	prop = of_find_property(dt, "tps61310,tps61310_strb1", NULL);
	if (prop) {
		
		of_property_read_u32(dt, "tps61310,tps61310_strb1", &pdata->tps61310_strb1);
	}
	prop = of_find_property(dt, "tps61310,flash_duration_ms", NULL);
	if (prop) {
		of_property_read_u32(dt, "tps61310,flash_duration_ms", &pdata->flash_duration_ms);
	}
	prop = of_find_property(dt, "tps61310,enable_FLT_1500mA", NULL);
	if (prop) {
		of_property_read_u32(dt, "tps61310,enable_FLT_1500mA", &pdata->enable_FLT_1500mA);
	}
	prop = of_find_property(dt, "tps61310,led_count", NULL);
	if (prop) {
		of_property_read_u32(dt, "tps61310,led_count", &pdata->led_count);
	}
	prop = of_find_property(dt, "tps61310,disable_tx_mask", NULL);
	if (prop) {
		of_property_read_u32(dt, "tps61310,disable_tx_mask", &pdata->disable_tx_mask);
	}

	return 0;

}

enum led_status {
	OFF = 0,
	ON,
	BLINK,
};

enum led_id {
	LED_2   = 0,
	LED_1_3 = 1,
};

struct tps61310_led_data {
	u8			num_leds;
	struct i2c_client	*client_dev;
	struct tps61310_data 	*tps61310;
	int status;
	struct led_classdev	cdev;
	int			max_current;
	int			id;
	u8			default_state;
	int                     torch_mode;
	struct mutex		lock;
	struct work_struct	work;
};

static int tps61310_get_common_configs(struct tps61310_led_data *led,
		struct device_node *node)
{
	int rc;
	const char *temp_string;

	led->cdev.default_trigger = "none";
	rc = of_property_read_string(node, "linux,default-trigger",
		&temp_string);
	if (!rc)
		led->cdev.default_trigger = temp_string;
	else if (rc != -EINVAL)
		return rc;

	led->default_state = LEDS_GPIO_DEFSTATE_OFF;
	rc = of_property_read_string(node, "htc,default-state",
		&temp_string);
	if (!rc) {
		if (!strcmp(temp_string, "keep"))
			led->default_state = LEDS_GPIO_DEFSTATE_KEEP;
		else if (!strcmp(temp_string, "on"))
			led->default_state = LEDS_GPIO_DEFSTATE_ON;
		else
			led->default_state = LEDS_GPIO_DEFSTATE_OFF;
	} else if (rc != -EINVAL)
		return rc;

	return 0;
}

static int tps61310_error_recover(void)
{
	int err = 0;
	if (this_tps61310->reset && reg_init_fail) {
		reg_init_fail = 0;
		if (this_tps61310->enable_FLT_1500mA) {
			err |= tps61310_i2c_command(0x07, 0x46);
			err |= tps61310_i2c_command(0x04, 0x10);
		} else {
			
			err |= tps61310_i2c_command(0x07, 0xF6);
		}
	}
	if (err) {
		FLT_ERR_LOG("%s error init register\n", __func__);
		reg_init_fail = 0;
		return -err;
	}

	return err;
}

static void tps61310_flash_strb(void)
{
	ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
	ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
	queue_delayed_work(tps61310_work_queue, &tps61310_delayed_work,
			   msecs_to_jiffies(this_tps61310->flash_sw_timeout));
}

static void tps61310_torch_strb(void)
{
	ioexp_gpio_set_cfg(this_tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
	ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
	tps61310_i2c_command(0x01, 0x40);
}


static int tps61310_flash_set(struct tps61310_led_data *led,
				enum led_brightness value)
{
	int err = 0;
	uint8_t current_hex = 0x0;

	FLT_INFO_LOG("flash_set current=%d\n", value);

	mutex_lock(&tps61310_mutex);
	err = tps61310_error_recover();
	if ( err ) {
		mutex_unlock(&tps61310_mutex);
		return err;
	}

	if ( value == TPS61310_OFF_CURRENT )
		flashlight_turn_off();
	else if ( value > TPS61310_OFF_CURRENT ) {
		uint8_t enled = 0x68;
		FLT_INFO_LOG("dualflash 1.5A - simple\n");
		switch (led->id)
		{
		case LED_2:
			current_hex = value / TPS61310_MAX_LED2_DIVIDER;
			current_hex += 0x80;
			enled |= 0x02;
			tps61310_i2c_command(0x05, enled);
			tps61310_i2c_command(0x00, 0x00);
			tps61310_i2c_command(0x01, current_hex);
			printk(KERN_INFO "[FLT]"
					"set led2 current to 0x%x.\r\n", current_hex);
			break;
		case LED_1_3:
			current_hex = value / TPS61310_MAX_LED13_DIVIDER;
			current_hex += 0x80;
			enled |= 0x05;
			tps61310_i2c_command(0x05, enled);
			tps61310_i2c_command(0x00, 0x00);
			tps61310_i2c_command(0x02, current_hex);
			printk(KERN_INFO "[FLT]"
					"set led13 current to 0x%x.\r\n", current_hex);
			break;
		}

		tps61310_flash_strb();
	}

	mutex_unlock(&tps61310_mutex);
	return err;
}

static int tps61310_torch_set(struct tps61310_led_data *led,
				enum led_brightness value)
{
	int err = 0;
	uint8_t current_hex = 0x0;

	FLT_INFO_LOG("torch_set current=%d\n", value);

	mutex_lock(&tps61310_mutex);
	err = tps61310_error_recover();
	if ( err ) {
		mutex_unlock(&tps61310_mutex);
		return err;
	}

	if ( value == TPS61310_OFF_CURRENT )
		flashlight_turn_off();
	else if ( value > TPS61310_OFF_CURRENT ) {
		FLT_INFO_LOG("dualtorch 1.5A - simple\n");
		switch (led->id) {
		case LED_2:
			current_hex = (value / TPS61310_MAX_LED2_DIVIDER) & 0x07;
			tps61310_i2c_command(0x05, 0x6A);
			tps61310_i2c_command(0x00, current_hex);
			break;
		case LED_1_3:
			current_hex = ((value / TPS61310_MAX_LED13_DIVIDER) << 3) & 0x38;
			tps61310_i2c_command(0x05, 0x6B);
			tps61310_i2c_command(0x00, current_hex);
			break;
		};

		tps61310_torch_strb();
	}

	mutex_unlock(&tps61310_mutex);
	return err;
}

static void __tps61310_led_work(struct tps61310_led_data *led,
				enum led_brightness value)
{
	mutex_lock(&led->lock);
	if ( led->torch_mode ) {
		switch (led->id) {
		case LED_2:
			tps61310_torch_set(led, value);
			break;
		case LED_1_3:
			tps61310_torch_set(led, value);
			break;
		};
	} else {
		switch (led->id) {
		case LED_2:
			tps61310_flash_set(led, value);
			break;
		case LED_1_3:
			tps61310_flash_set(led, value);
			break;
		};
	}
	mutex_unlock(&led->lock);
}

static void tps61310_led_work(struct work_struct *work)
{
	struct tps61310_led_data *led = container_of(work,
					struct tps61310_led_data, work);

	__tps61310_led_work(led, led->cdev.brightness);

	return;
}

static void tps61310_led_set(struct led_classdev *led_cdev,
				enum led_brightness value)
{
	struct tps61310_led_data *led;

	led = container_of(led_cdev, struct tps61310_led_data, cdev);
	if (value < LED_OFF || value > led->cdev.max_brightness) {
		printk(KERN_ERR "[FLT]"
				"Invalid brightness value\n");
		return;
	}

	led->cdev.brightness = value;
	schedule_work(&led->work);
}

static enum led_brightness tps61310_led_get(struct led_classdev *led_cdev)
{
	struct tps61310_led_data *led;

	led = container_of(led_cdev, struct tps61310_led_data, cdev);

	return led->cdev.brightness;
}

static int tps61310_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct tps61310_data *tps61310;
	struct TPS61310_flashlight_platform_data *pdata;
	int i = 0, err = 0, ret = 0;

	struct tps61310_led_data *led, *led_array;
	struct device_node *node, *temp;
	int num_leds = 0, parsed_leds = 0;
	const char *led_label;
	int rc;

	if(strcmp(htc_get_bootmode(), "charger") == 0) {
		FLT_INFO_LOG("%s: offmode_charging, do not probe tps61310_flashlight\n", __func__);
		return -EACCES;
	}

	FLT_INFO_LOG("%s +\n", __func__);


	
		pdata =  kzalloc(sizeof(*pdata), GFP_KERNEL);
		if (pdata == NULL){
			err = -ENOMEM;
			return err;
		}
		err = tps61310_parse_dt(&client->dev, pdata);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		goto check_functionality_failed;
	}

	tps61310 = kzalloc(sizeof(struct tps61310_data), GFP_KERNEL);
	if (!tps61310) {
		FLT_ERR_LOG("%s: kzalloc fail !!!\n", __func__);
		kfree(pdata);
		return -ENOMEM;
	}

	i2c_set_clientdata(client, tps61310);
	this_client = client;

    this_client->dev.coherent_dma_mask = DMA_BIT_MASK(32);
    gpDMABuf_va = (u8 *)dma_alloc_coherent(&this_client->dev, GTP_DMA_MAX_TRANSACTION_LENGTH, &gpDMABuf_pa, GFP_KERNEL);                           
    if(!gpDMABuf_va){
        printk("[Error] Allocate DMA I2C Buffer failed!\n");
    }
    memset(gpDMABuf_va, 0, GTP_DMA_MAX_TRANSACTION_LENGTH);

    gpDMABuf_va1 = (u8 *)dma_alloc_coherent(&this_client->dev, GTP_DMA_MAX_TRANSACTION_LENGTH, &gpDMABuf_pa1, GFP_KERNEL);
    if(!gpDMABuf_va1){
        printk("[Error] Allocate DMA I2C Buffer failed!\n");
    }
    memset(gpDMABuf_va1, 0, GTP_DMA_MAX_TRANSACTION_LENGTH);

	INIT_DELAYED_WORK(&tps61310_delayed_work, flashlight_turn_off_work);
	tps61310_work_queue = create_singlethread_workqueue("tps61310_wq");
	if (!tps61310_work_queue)
		goto err_create_tps61310_work_queue;

	tps61310->fl_lcdev.name              = FLASHLIGHT_NAME;
	tps61310->fl_lcdev.brightness_set    = fl_lcdev_brightness_set;
	tps61310->strb0                      = pdata->tps61310_strb0;
	tps61310->strb1                      = pdata->tps61310_strb1;
	tps61310->reset                      = pdata->tps61310_reset;
	tps61310->flash_sw_timeout           = pdata->flash_duration_ms;
	tps61310->led_count                  = (pdata->led_count) ? pdata->led_count : 1;
	tps61310->mode_pin_suspend_state_low = pdata->mode_pin_suspend_state_low;
	tps61310->enable_FLT_1500mA          = pdata->enable_FLT_1500mA;
	tps61310->disable_tx_mask            = pdata->disable_tx_mask;
	tps61310->power_save                 = pdata->power_save;
	tps61310->power_save_2               = pdata->power_save_2;

	if (tps61310->strb0) {
		ret = gpio_request(tps61310->strb0, "strb0");
		if (ret) {
			FLT_ERR_LOG("%s: unable to request gpio %d (%d)\n",
				__func__, tps61310->strb0, ret);
			return ret;
		}

		
		ret = ioexp_gpio_set_cfg(tps61310->strb0, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
		if (ret) {
			FLT_ERR_LOG("%s: Unable to set direction\n", __func__);
			return ret;
		}
	}
	if (tps61310->strb1) {
		ret = gpio_request(tps61310->strb1, "strb1");
		if (ret) {
			FLT_ERR_LOG("%s: unable to request gpio %d (%d)\n",
				__func__, tps61310->strb1, ret);
			return ret;
		}

		ret = ioexp_gpio_set_cfg(tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
		if (ret) {
			FLT_ERR_LOG("%s: Unable to set direction\n", __func__);
			return ret;
		}
	}
	if (tps61310->flash_sw_timeout <= 0)
		tps61310->flash_sw_timeout = TPS61310_SW_TIMEOUT;

	node = client->dev.of_node;

	if (node == NULL)
		return -ENODEV;

	temp = NULL;
	while ((temp = of_get_next_child(node, temp)))
		num_leds++;

	if (!num_leds)
		return -ECHILD;

	led_array = devm_kzalloc(&client->dev,
		(sizeof(struct tps61310_led_data) * num_leds), GFP_KERNEL);
	if (!led_array) {
		dev_err(&client->dev, "Unable to allocate memory\n");
		return -ENOMEM;
	}

	tps61310->led_array = led_array;
	for_each_child_of_node(node, temp) {
		led = &led_array[parsed_leds];
		led->num_leds = num_leds;
		led->client_dev = client;
		led->tps61310 = tps61310;
		led->status = OFF;

		rc = of_property_read_string(temp, "label", &led_label);
		if (rc < 0) {
			printk(KERN_ERR "[FLT] "
				"Failure reading label, rc = %d\n", rc);
			goto fail_id_check;
		}

		rc = of_property_read_string(temp, "linux,name", &led->cdev.name);
		if (rc < 0) {
			printk(KERN_ERR "[FLT] "
				"Failure reading led name, rc = %d\n", rc);
			goto fail_id_check;
		}

		rc = of_property_read_u32(temp, "htc,max-current", &led->max_current);
		if (rc < 0) {
			printk(KERN_ERR "[FLT] "
				"Failure reading max_current, rc =  %d\n", rc);
			goto fail_id_check;
		}

		rc = of_property_read_u32(temp, "htc,id", &led->id);
		if (rc < 0) {
			printk(KERN_ERR "[FLT] "
				"Failure reading led id, rc =  %d\n", rc);
			goto fail_id_check;
		}

		rc = tps61310_get_common_configs(led, temp);
		if (rc) {
			printk(KERN_ERR "[FLT] "
				"Failure reading common led configuration," \
				" rc = %d\n", rc);
			goto fail_id_check;
		}

		led->cdev.brightness_set    = tps61310_led_set;
		led->cdev.brightness_get    = tps61310_led_get;

		if (strncmp(led_label, "flash", sizeof("flash")) == 0) {
			led->torch_mode = 0;
			if (rc < 0) {
				printk(KERN_ERR "[FLT] "
					"Unable to read flash config data\n");
				goto fail_id_check;
			}
		} else if (strncmp(led_label, "torch", sizeof("torch")) == 0) {
			led->torch_mode = 1;
			if (rc < 0) {
				printk(KERN_ERR "[FLT] "
					"Unable to read torch config data\n");
				goto fail_id_check;
			}
		} else {
			printk(KERN_ERR "[FLT] "
				"No LED matching label\n");
			rc = -EINVAL;
			goto fail_id_check;
		}

		mutex_init(&led->lock);
		INIT_WORK(&led->work, tps61310_led_work);

		led->cdev.max_brightness = led->max_current;

		rc = led_classdev_register(&client->dev, &led->cdev);
		if (rc) {
			printk(KERN_ERR "[FLT] "
					"unable to register led %d,rc=%d\n",
					led->id, rc);
			goto fail_id_check;
		}

		
		switch (led->default_state) {
			case LEDS_GPIO_DEFSTATE_OFF:
				led->cdev.brightness = LED_OFF;
				break;
			case LEDS_GPIO_DEFSTATE_ON:
				led->cdev.brightness = led->cdev.max_brightness;
				__tps61310_led_work(led, led->cdev.brightness);
				schedule_work(&led->work);
				break;
			case LEDS_GPIO_DEFSTATE_KEEP:
				led->cdev.brightness = led->cdev.max_brightness;
				break;
		}

		parsed_leds++;
	}

	mutex_init(&tps61310_mutex);
	err = led_classdev_register(&client->dev, &tps61310->fl_lcdev);
	if (err < 0) {
		FLT_ERR_LOG("%s: failed on led_classdev_register\n", __func__);
		goto platform_data_null;
	}

	this_tps61310 = tps61310;

	#if defined(CONFIG_HTC_FLASHLIGHT_COMMON)
	htc_flash_main			= &tps61310_flt_flash_adapter;
	htc_torch_main			= &tps61310_flt_torch_adapter;
	#endif

	err = register_reboot_notifier(&reboot_notifier);
	if (err < 0) {
		FLT_ERR_LOG("%s: Register reboot notifier failed(err=%d)\n", __func__, err);
		goto platform_data_null;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	tps61310->fl_early_suspend.suspend = flashlight_early_suspend;
	tps61310->fl_early_suspend.resume  = flashlight_late_resume;
	register_early_suspend(&tps61310->fl_early_suspend);
#endif

	rc = of_property_read_u32(node, "htc,dualflash", &support_dual_flashlight);
	if (rc < 0)
		support_dual_flashlight = 0;

	err = device_create_file(tps61310->fl_lcdev.dev, &dev_attr_support_dual_flashlight);
	if (err < 0) {
		FLT_ERR_LOG("%s, create support_dual_flashlight sysfs fail\n", __func__);
	}
	err = device_create_file(tps61310->fl_lcdev.dev, &dev_attr_poweroff);
	if (err < 0) {
		FLT_ERR_LOG("%s, create poweroff sysfs fail\n", __func__);
	}
	err = device_create_file(tps61310->fl_lcdev.dev, &dev_attr_sw_timeout);
	if (err < 0) {
		FLT_ERR_LOG("%s, create sw_timeout sysfs fail\n", __func__);
	}
	err = device_create_file(tps61310->fl_lcdev.dev, &dev_attr_regaddr);
	if (err < 0) {
		FLT_ERR_LOG("%s, create regaddr sysfs fail\n", __func__);
	}
	err = device_create_file(tps61310->fl_lcdev.dev, &dev_attr_regdata);
	if (err < 0) {
		FLT_ERR_LOG("%s, create regdata sysfs fail\n", __func__);
	}
	err = device_create_file(tps61310->fl_lcdev.dev, &dev_attr_function_switch);
	if (err < 0) {
		FLT_ERR_LOG("%s, create function_switch sysfs fail\n", __func__);
	}
	err = device_create_file(tps61310->fl_lcdev.dev, &dev_attr_max_current);
	if (err < 0) {
		FLT_ERR_LOG("%s, create max_current sysfs fail\n", __func__);
	}
	err = device_create_file(tps61310->fl_lcdev.dev, &dev_attr_flash);
	if (err < 0) {
		FLT_ERR_LOG("%s, create max_current sysfs fail\n", __func__);
	}
	
	tps61310_i2c_command(0x01, 0x00);

	if (this_tps61310->enable_FLT_1500mA) {
		FLT_INFO_LOG("Flashlight with 1.5A\n");
		tps61310_i2c_command(0x07, 0x46);
		tps61310_i2c_command(0x04, 0x10);
	} else {
		
		tps61310_i2c_command(0x07, 0x76);
	}
	
	if (this_tps61310->disable_tx_mask)
		tps61310_i2c_command(0x03, 0xC0);
	if (this_tps61310->reset)
		FLT_INFO_LOG("%s reset pin exist\n", __func__);
	else
		FLT_INFO_LOG("%s no reset pin\n", __func__);
	if (this_tps61310->power_save) {
		FLT_INFO_LOG("%s power save pin exist\n", __func__);
		ioexp_gpio_set_cfg(this_tps61310->power_save, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
	}
	else
		FLT_INFO_LOG("%s no power save pin\n", __func__);
	if (this_tps61310->power_save_2) {
		FLT_INFO_LOG("%s power save pin_2 exist\n", __func__);
		ioexp_gpio_set_cfg(this_tps61310->power_save_2, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);
	}
	else
		FLT_INFO_LOG("%s no power save pin_2\n", __func__);
	FLT_INFO_LOG("%s -\n", __func__);
	return 0;


platform_data_null:
	destroy_workqueue(tps61310_work_queue);
	mutex_destroy(&tps61310_mutex);
err_create_tps61310_work_queue:
	kfree(tps61310);
check_functionality_failed:
	return err;
fail_id_check:
	for (i = 0; i < parsed_leds; i++) {
		mutex_destroy(&led_array[i].lock);
		led_classdev_unregister(&led_array[i].cdev);
	}

	return rc;
}

static int tps61310_remove(struct i2c_client *client)
{
	struct tps61310_data *tps61310 = i2c_get_clientdata(client);
	struct tps61310_led_data *led_array = tps61310->led_array;

	if (led_array) {
		int i, parsed_leds = led_array->num_leds;
		for (i=0; i<parsed_leds; i++) {
			cancel_work_sync(&led_array[i].work);
			mutex_destroy(&led_array[i].lock);
			led_classdev_unregister(&led_array[i].cdev);
		}
	}

	unregister_reboot_notifier(&reboot_notifier);
	led_classdev_unregister(&tps61310->fl_lcdev);
	destroy_workqueue(tps61310_work_queue);
	mutex_destroy(&tps61310_mutex);
	unregister_early_suspend(&tps61310->fl_early_suspend);
	kfree(tps61310);

	FLT_INFO_LOG("%s:\n", __func__);
	return 0;
}

static const struct i2c_device_id tps61310_id[] = {
	{ "TPS61310_FLASHLIGHT", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tps61310_id);
static int tps61310_resume(struct i2c_client *client)
{

		FLT_INFO_LOG("%s:\n", __func__);
		if (this_tps61310->mode_pin_suspend_state_low)
			ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 1);
		return 0;
}
static int tps61310_suspend(struct i2c_client *client, pm_message_t state)
{

		FLT_INFO_LOG("%s:\n", __func__);
		if (this_tps61310->mode_pin_suspend_state_low)
			ioexp_gpio_set_cfg(this_tps61310->strb1, IOEXP_OUTPUT, IOEXP_PULLDOWN, 0);

		flashlight_turn_off();
		return 0;
}

static const struct of_device_id tps61310_mttable[] = {
	{ .compatible = "TPS61310_FLASHLIGHT"},
	{ },
};

static struct i2c_driver tps61310_driver = {
	.driver		= {
		.name = "TPS61310_FLASHLIGHT",
		.owner = THIS_MODULE,
		.of_match_table = tps61310_mttable,
	},
	.probe		= tps61310_probe,
	.remove		= tps61310_remove,
	.suspend	= tps61310_suspend,
	.resume		= tps61310_resume,
	.id_table = tps61310_id,
};
static int __init tps61310_init(void)
{
	printk("[FLT] tps61310_%s Enter\n", __func__);
	return i2c_add_driver(&tps61310_driver);
}

static void __exit tps61310_exit(void)
{
	i2c_del_driver(&tps61310_driver);
}

late_initcall(tps61310_init);
module_exit(tps61310_exit);



MODULE_DESCRIPTION("TPS61310 Led Flash driver");
MODULE_LICENSE("GPL");