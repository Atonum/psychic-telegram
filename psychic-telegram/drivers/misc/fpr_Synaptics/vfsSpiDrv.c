/*! @file vfsSpiDrv.c
*******************************************************************************
**  SPI Driver Interface Functions
**
**  This file contains the SPI driver interface functions.
**
**  Copyright (C) 2011-2013 Validity Sensors, Inc.
**  This program is free software; you can redistribute it and/or
**  modify it under the terms of the GNU General Public License
**  as published by the Free Software Foundation; either version 2
**  of the License, or (at your option) any later version.
**  
**  This program is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**  
**  You should have received a copy of the GNU General Public License
**  along with this program; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**  
*/

#include <vfsSpiDrv.h>

#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spi/spi.h>

#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/i2c/twl.h>
#include <linux/wait.h>
#include <linux/uaccess.h>
#include <linux/irq.h>
#include <linux/compat.h>

#include <asm-generic/siginfo.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/jiffies.h>

#ifdef MTK_PLATFORM
#include <mach/mt_gpio.h>
#include <mach/mt_spi.h>
#include <mach/mt_pm_ldo.h>
#include <mach/mt_gpio.h>
#endif

#define VALIDITY_PART_NAME "validity_fingerprint"
static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_mutex);
static struct class *vfsspi_device_class;
static int gpio_irq;

#ifdef CONFIG_OF
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/of_irq.h>
static struct of_device_id validity_metallica_table[] = {
	{ .compatible = "validity,metallica",},
	{ },
};
#else
#define validity_metallica_table NULL
#endif
#ifdef CONFIG_FPR_CT
extern int fp_sensor;
#else
int fp_sensor = 0;
#endif

struct fingerprint_pdata_struct {
    int (*set_sleep_pin)(int, int);
    int (*set_power_control)(int);
    unsigned int (*read_engineerid)(void);
};

struct fingerprint_pdata_struct *fingerprint_pdata;


struct vfsspi_device_data {
	dev_t devt;
	struct cdev cdev;
	spinlock_t vfs_spi_lock;
	struct spi_device *spi;
	struct list_head device_entry;
	struct mutex buffer_mutex;
	unsigned int is_opened;
	unsigned char *buffer;
	unsigned char *null_buffer;
	unsigned char *stream_buffer;
	size_t stream_buffer_size;
	unsigned int drdy_pin;
	unsigned int sleep_pin;
#if DO_CHIP_SELECT
	unsigned int cs_pin;
#endif
	int user_pid;
	int signal_id;
	unsigned int current_spi_speed;
	unsigned int is_drdy_irq_enabled;
	unsigned int drdy_ntf_type;
	struct mutex kernel_lock;
};

struct vfsspi_platform_data {
	int irq_gpio;
	int reset_gpio;
	int cs_gpio;
	int pwr_3v3_gpio;
	int pwr_1v8_gpio;
};

struct vfsspi_platform_data *vfsspi_pdata;
struct vfsspi_platform_data pdata_of;
struct spi_device *fp_spi;
char firmware_syn[30];
static ssize_t fpr_test(struct spi_device *);
static int fpr_power_control(int, struct vfsspi_platform_data *);
static ssize_t firmware_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	int pos_ind;
	pos_ind = fpr_test(fp_spi);
	pos_ind = snprintf(buf, pos_ind + 2, "%s", firmware_syn);
	return pos_ind;
}

static ssize_t vendor_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	int pos_ind= 0;
	printk("[fp][SYN]Show finegrprint vendor 0105\n");

	switch(fp_sensor)
	{
		case 0: 
			printk("[fp][SYN] Fingerprint sensor non exist\n");
			pos_ind += snprintf(firmware_syn+pos_ind, sizeof(firmware_syn), "0");
			break;
		case 1: 
			printk("[fp][SYN] Fingerprint sensor is Validity\n");
			pos_ind += snprintf(firmware_syn+pos_ind, sizeof(firmware_syn), "1");
			break;
		case 2: 
			printk("[fp][SYN] Fingerprint sensor is Synaptics\n");
			pos_ind += snprintf(firmware_syn+pos_ind, sizeof(firmware_syn), "2");
			break;
		case 3: 
			printk("[fp][SYN] Fingerprint sensor is Fingerprint Card\n");
			pos_ind += snprintf(firmware_syn+pos_ind, sizeof(firmware_syn), "3");
			break;
	}

	pos_ind = snprintf(buf, pos_ind + 2, "%s", firmware_syn);

	return pos_ind;
}
static DEVICE_ATTR(firmware, (S_IWUSR|S_IRUGO), firmware_show, NULL);
static DEVICE_ATTR(fp_mount, (S_IWUSR|S_IRUGO), vendor_show, NULL);

static struct kobject *android_fingerprint_kobj;

static int fingerprint_sysfs_init(void)
{
        int ret = 0;
        android_fingerprint_kobj = kobject_create_and_add("android_fingerprint", NULL);
        if (android_fingerprint_kobj == NULL) {
                printk("%s:subsystem_register_failed", __func__);
                ret = -ENOMEM;
                return ret;
        }

        ret = sysfs_create_file(android_fingerprint_kobj, &dev_attr_firmware.attr);
        if (ret) {
                printk("%s: sysfs_create_file read_att failed(firmware)\n", __func__);
                return ret;
        }
        ret = sysfs_create_file(android_fingerprint_kobj, &dev_attr_fp_mount.attr);
        if (ret) {
                printk("%s: sysfs_create_file read_att failed\n", __func__);
                return ret;
        }
        printk("attribute file register Done");
        return 0;
}

#ifdef VFSSPI_32BIT
struct vfsspi_compat_ioctl_transfer {
	compat_uptr_t rx_buffer;
	compat_uptr_t tx_buffer;
	unsigned int len;
};
#endif

static int vfsspi_sendDrdyEventFd(struct vfsspi_device_data *vfsSpiDev);
static int vfsspi_sendDrdyNotify(struct vfsspi_device_data *vfsSpiDev);

static int vfsspi_send_drdy_signal(struct vfsspi_device_data *vfsspi_device)
{
	struct task_struct *t;
	int ret = 0;

	
	

	if (vfsspi_device->user_pid != 0) {
		rcu_read_lock();
		
		
		
		printk("[fp][SYN] vfsspi_send_drdy_signal - Searching task with PID=%08x\n", vfsspi_device->user_pid);
		t = pid_task(find_pid_ns(vfsspi_device->user_pid, &init_pid_ns),
			     PIDTYPE_PID);
		if (t == NULL) {
			
			printk("[fp][SYN] No such pid\n");
			rcu_read_unlock();
			return -ENODEV;
		}
		rcu_read_unlock();
		
		ret = send_sig_info(vfsspi_device->signal_id,
				    (struct siginfo *)1, t);
		if (ret < 0)
		{
			
			printk("[fp][SYN] Error sending signal\n");
		}

	} else {
		
		printk("[fp][SYN] pid not received yet\n");
	}

	return ret;
}

/* Return no. of bytes written to device. Negative number for errors */
static inline ssize_t vfsspi_writeSync(struct vfsspi_device_data *vfsspi_device,
					size_t len)
{
	int    status = 0;
	struct spi_message m;
#ifdef MTK_PLATFORM
	struct spi_transfer cmd, data;
	struct mt_chip_conf *chip_config = NULL;
#else
	struct spi_transfer t;
#endif

	
	

	spi_message_init(&m);
#ifdef MTK_PLATFORM
	
	
	if(len > 32)
	{
		chip_config = ( struct mt_chip_conf * ) vfsspi_device->spi->controller_data;
		chip_config->com_mod = DMA_TRANSFER;
		chip_config->pause = 1;
		spi_setup(vfsspi_device->spi);
	}

	if(len > 1024 && len % 1024 != 0)
	{
		

		memset(&cmd, 0, sizeof(cmd));
		memset(&data, 0, sizeof(data));

		cmd.cs_change = 0;
		cmd.tx_buf    = vfsspi_device->buffer;
		cmd.rx_buf    = vfsspi_device->null_buffer;
		cmd.len       = 1024 * (len/1024);
		cmd.speed_hz  = vfsspi_device->current_spi_speed;

		data.cs_change = 0;
		data.len       = len % 1024;
		data.tx_buf    = vfsspi_device->buffer + cmd.len;
		data.rx_buf    = vfsspi_device->null_buffer + cmd.len;
		data.speed_hz  = vfsspi_device->current_spi_speed;

		spi_message_add_tail(&cmd, &m);
		spi_message_add_tail(&data, &m);
		
	}
	else
	{
		memset(&data, 0, sizeof(data));

		data.len       = len;
		data.tx_buf    = vfsspi_device->buffer;
		data.rx_buf    = vfsspi_device->null_buffer;
		data.speed_hz  = vfsspi_device->current_spi_speed;

		spi_message_add_tail(&data, &m);
	}
#else
	memset(&t, 0, sizeof(t));

	t.tx_buf = vfsspi_device->buffer;
	t.rx_buf = vfsspi_device->null_buffer;
	t.len = len;
	t.speed_hz = vfsspi_device->current_spi_speed;
	spi_message_add_tail(&t, &m);
#endif

#if DO_CHIP_SELECT
	gpio_set_value(vfsspi_device->cs_pin, 0);
#endif
	status = spi_sync(vfsspi_device->spi, &m);

#ifdef MTK_PLATFORM
	if(len > 32)
	{
		chip_config = ( struct mt_chip_conf * ) vfsspi_device->spi->controller_data;
		chip_config->com_mod = FIFO_TRANSFER;
		chip_config->pause = 0;
		spi_setup(vfsspi_device->spi);
	}
#endif

#if DO_CHIP_SELECT
	gpio_set_value(vfsspi_device->cs_pin, 1);
#endif
	if (status == 0)
		status = m.actual_length;
	else
		printk("[fp][SYN] vfsspi_writeSync fail, status = %d\n", status);

	
	
	
	return status;
}

static inline ssize_t vfsspi_readSync(struct vfsspi_device_data *vfsspi_device,
					size_t len)
{
	int    status = 0;
	struct spi_message m;
#ifdef MTK_PLATFORM
	struct spi_transfer cmd, data;
	struct mt_chip_conf *chip_config = NULL;
#else
	struct spi_transfer t;
#endif

	
	

	spi_message_init(&m);

	memset(vfsspi_device->null_buffer, 0x0, len);
#ifdef MTK_PLATFORM
	
	
	if(len > 32)
	{
		chip_config = ( struct mt_chip_conf * ) vfsspi_device->spi->controller_data;
		chip_config->com_mod = DMA_TRANSFER;
		chip_config->pause = 1;
		spi_setup(vfsspi_device->spi);
	}

	if(len > 1024 && len % 1024 != 0)
	{

		memset(&cmd, 0, sizeof(cmd));
		memset(&data, 0, sizeof(data));

		cmd.cs_change = 0;
		cmd.tx_buf    = vfsspi_device->null_buffer;
		cmd.rx_buf    = vfsspi_device->buffer;
		cmd.len       = 1024 * (len/1024);
		cmd.speed_hz  = vfsspi_device->current_spi_speed;

		data.cs_change = 0;
		data.len       = len % 1024;
		data.tx_buf    = vfsspi_device->null_buffer + cmd.len;
		data.rx_buf    = vfsspi_device->buffer + cmd.len;
		data.speed_hz  = vfsspi_device->current_spi_speed;

		spi_message_add_tail(&cmd, &m);
		spi_message_add_tail(&data, &m);
		
	}
	else
	{
		memset(&data, 0, sizeof(data));

		data.len       = len;
		data.tx_buf    = vfsspi_device->null_buffer;
		data.rx_buf    = vfsspi_device->buffer;
		data.speed_hz  = vfsspi_device->current_spi_speed;

		spi_message_add_tail(&data, &m);
	}
#else
	memset(&t, 0, sizeof(t));

	t.tx_buf = vfsspi_device->null_buffer;
	t.rx_buf = vfsspi_device->buffer;
	t.len = len;
	t.speed_hz = vfsspi_device->current_spi_speed;
	spi_message_add_tail(&t, &m);
#endif

#if DO_CHIP_SELECT
	gpio_set_value(vfsspi_device->cs_pin, 0);
#endif

#ifdef MTK_PLATFORM
	status = spi_sync(vfsspi_device->spi, &m);
	if(len > 32)
	{	
		chip_config = ( struct mt_chip_conf * ) vfsspi_device->spi->controller_data;
		chip_config->com_mod = FIFO_TRANSFER;
		chip_config->pause = 0;
		spi_setup(vfsspi_device->spi);
	}
#endif

	
	
#if DO_CHIP_SELECT
	gpio_set_value(vfsspi_device->cs_pin, 1);
#endif
	if (status == 0)
		status = len;
	else
		printk("[fp][SYN] vfsspi_readSync fail, status = %d\n", status);

	
	
	

	return status;
}

static ssize_t vfsspi_write(struct file *filp, const char __user *buf,
			size_t count, loff_t *fPos)
{
	struct vfsspi_device_data *vfsspi_device = NULL;
	ssize_t               status = 0;

	
	

	if (count > DEFAULT_BUFFER_SIZE || count <= 0)
	{
		printk("[fp][SYN] vfsspi_write count incorrect (%zu)\n", count);
		return -EMSGSIZE;
	}

	vfsspi_device = filp->private_data;

	mutex_lock(&vfsspi_device->buffer_mutex);

	if (vfsspi_device->buffer) {
		unsigned long missing = 0;

		missing = copy_from_user(vfsspi_device->buffer, buf, count);

		if (missing == 0)
		{
			status = vfsspi_writeSync(vfsspi_device, count);
		}
		else
			status = -EFAULT;
	}

	mutex_unlock(&vfsspi_device->buffer_mutex);

	return status;
}

static ssize_t vfsspi_read(struct file *filp, char __user *buf,
			size_t count, loff_t *fPos)
{
	struct vfsspi_device_data *vfsspi_device = NULL;
	ssize_t                status    = 0;

	
	

	if (count > DEFAULT_BUFFER_SIZE || count <= 0)
	{
		printk("[fp][SYN] vfsspi_read count size incorrect (%zu)\n", count);
		return -EMSGSIZE;
	}
	if (buf == NULL)
	{
		printk("[fp][SYN] vfsspi_read buf is NULL\n");
		return -EFAULT;
	}


	vfsspi_device = filp->private_data;

	mutex_lock(&vfsspi_device->buffer_mutex);

	status  = vfsspi_readSync(vfsspi_device, count);


	if (status > 0) {
		unsigned long missing = 0;
		
		missing = copy_to_user(buf, vfsspi_device->buffer, status);

		if (missing == status) {
			
			printk("[fp][SYN] copy_to_user failed\n");
			
			status = -EFAULT;
		} else {
			status = status - missing;
		}
	}

	mutex_unlock(&vfsspi_device->buffer_mutex);

	return status;
}

static int vfsspi_xfer(struct vfsspi_device_data *vfsspi_device,
			struct vfsspi_ioctl_transfer *tr)
{
	int status = 0;
	struct spi_message m;
#ifdef MTK_PLATFORM
	struct spi_transfer cmd, data;
	struct mt_chip_conf *chip_config = NULL;
#else
	struct spi_transfer t;
#endif

	
	

	if (vfsspi_device == NULL || tr == NULL)
	{
		printk("[fp][SYN] vfsspi_xfer device or tr is NULL\n");
		return -EFAULT;
	}

	if (tr->len > DEFAULT_BUFFER_SIZE || tr->len <= 0)
	{
		printk("[fp][SYN] vfsspi_xfer len size incorrect (%d)\n", tr->len);
		return -EMSGSIZE;
	}

	if (tr->tx_buffer != NULL) {

		if (copy_from_user(vfsspi_device->null_buffer,
				tr->tx_buffer, tr->len) != 0)
		{
			printk("[fp][SYN] vfsspi_xfer copy_from_user fail\n");
			return -EFAULT;
		}
	}

	spi_message_init(&m);

#ifdef MTK_PLATFORM

	if(tr->len > 32)
	{
		chip_config = ( struct mt_chip_conf * ) vfsspi_device->spi->controller_data;
		chip_config->com_mod = DMA_TRANSFER;
		chip_config->pause = 1;
		spi_setup(vfsspi_device->spi);
	}

	if(tr->len > 1024 && tr->len % 1024 != 0)
	{
		

		memset(&cmd, 0, sizeof(cmd));
		memset(&data, 0, sizeof(data));

		cmd.cs_change = 0;
		cmd.tx_buf    = vfsspi_device->null_buffer;
		cmd.rx_buf    = vfsspi_device->buffer;
		cmd.len       = 1024 * (tr->len/1024);
		cmd.speed_hz  = vfsspi_device->current_spi_speed;

		data.cs_change = 0;
		data.len       = tr->len % 1024;
		data.tx_buf    = vfsspi_device->null_buffer + cmd.len;
		data.rx_buf    = vfsspi_device->buffer + cmd.len;
		data.speed_hz  = vfsspi_device->current_spi_speed;

		spi_message_add_tail(&cmd, &m);
		spi_message_add_tail(&data, &m);
		
	}
	else
	{
		memset(&data, 0, sizeof(data));

		data.len       = tr->len;
		data.tx_buf    = vfsspi_device->null_buffer;
		data.rx_buf    = vfsspi_device->buffer;
		data.speed_hz  = vfsspi_device->current_spi_speed;

		spi_message_add_tail(&data, &m);
	}
#else
	memset(&t, 0, sizeof(t));

	t.tx_buf = vfsspi_device->null_buffer;
	t.rx_buf = vfsspi_device->buffer;
	t.len = len;
	t.speed_hz = vfsspi_device->current_spi_speed;

	spi_message_add_tail(&t, &m);
#endif
	

#if DO_CHIP_SELECT
	gpio_set_value(vfsspi_device->cs_pin, 0);
#endif
	status = spi_sync(vfsspi_device->spi, &m);

#ifdef MTK_PLATFORM
	if(tr->len > 32)
	{
		chip_config = ( struct mt_chip_conf * ) vfsspi_device->spi->controller_data;
		chip_config->com_mod = FIFO_TRANSFER;
		chip_config->pause = 0;
		spi_setup(vfsspi_device->spi);
	}
#endif

#if DO_CHIP_SELECT
	gpio_set_value(vfsspi_device->cs_pin, 1);
#endif
	if (status == 0) {
		if (tr->rx_buffer != NULL) {
			unsigned missing = 0;

			missing = copy_to_user(tr->rx_buffer,
					       vfsspi_device->buffer, tr->len);

			if (missing != 0)
				tr->len = tr->len - missing;
		}
	}
	else
		printk("[fp][SYN] vfsspi_xfer fail, status = %d\n", status);
	
	
	return status;

} 

static int vfsspi_rw_spi_message(struct vfsspi_device_data *vfsspi_device,
				 unsigned long arg)
{
	struct vfsspi_ioctl_transfer   *dup  = NULL;
#ifdef VFSSPI_32BIT
    struct vfsspi_compat_ioctl_transfer   dup_compat;
#endif	
	dup = kmalloc(sizeof(struct vfsspi_ioctl_transfer), GFP_KERNEL);
	if (dup == NULL)
		return -ENOMEM;
#ifdef VFSSPI_32BIT
	if (copy_from_user(&dup_compat, (void __user *)arg,
			   sizeof(struct vfsspi_compat_ioctl_transfer)) != 0)  {
#else
	if (copy_from_user(dup, (void __user *)arg,
			   sizeof(struct vfsspi_ioctl_transfer)) != 0)  {
#endif
		return -EFAULT;
	} else {
		int err;
#ifdef VFSSPI_32BIT		
		dup->rx_buffer = (unsigned char *)(unsigned long)dup_compat.rx_buffer;
		dup->tx_buffer = (unsigned char *)(unsigned long)dup_compat.tx_buffer;
		dup->len = dup_compat.len;
#endif
		err = vfsspi_xfer(vfsspi_device, dup);
		if (err != 0) {
			kfree(dup);
			return err;
		}
	}
#ifdef VFSSPI_32BIT
    dup_compat.len = dup->len;
	if (copy_to_user((void __user *)arg, &dup_compat,
			 sizeof(struct vfsspi_compat_ioctl_transfer)) != 0){
#else
	if (copy_to_user((void __user *)arg, dup,
			 sizeof(struct vfsspi_ioctl_transfer)) != 0){
#endif
		kfree(dup);
		return -EFAULT;
	}
	kfree(dup);
	return 0;
}

static int vfsspi_set_clk(struct vfsspi_device_data *vfsspi_device,
			  unsigned long arg)
{
	unsigned short clock = 0;
	struct spi_device *spidev = NULL;

	if (copy_from_user(&clock, (void __user *)arg,
			   sizeof(unsigned short)) != 0)
		return -EFAULT;

	spin_lock_irq(&vfsspi_device->vfs_spi_lock);
#if DO_CHIP_SELECT
	gpio_set_value(vfsspi_device->cs_pin, 0);
#endif
	spidev = spi_dev_get(vfsspi_device->spi);
#if DO_CHIP_SELECT
	gpio_set_value(vfsspi_device->cs_pin, 1);
#endif
	spin_unlock_irq(&vfsspi_device->vfs_spi_lock);
	if (spidev != NULL) {
		switch (clock) {
		case 0:	
			
			printk("[fp][SYN] Running baud rate.(%d)\n", MAX_BAUD_RATE);
			spidev->max_speed_hz = MAX_BAUD_RATE;
			vfsspi_device->current_spi_speed = MAX_BAUD_RATE;
			break;
		case 0xFFFF: 
			
			printk("[fp][SYN] slow baud rate. (%d)\n", SLOW_BAUD_RATE);
			spidev->max_speed_hz = SLOW_BAUD_RATE;
			vfsspi_device->current_spi_speed = SLOW_BAUD_RATE;
			break;
		default:
			
			printk("[fp][SYN] baud rate is %d .\n", clock * BAUD_RATE_COEF);
			vfsspi_device->current_spi_speed =
				clock * BAUD_RATE_COEF;
			if (vfsspi_device->current_spi_speed > MAX_BAUD_RATE)
				vfsspi_device->current_spi_speed =
					MAX_BAUD_RATE;
			spidev->max_speed_hz = vfsspi_device->current_spi_speed;
			break;
		}
		spi_dev_put(spidev);
	}
	return 0;
}

static int vfsspi_register_drdy_signal(struct vfsspi_device_data *vfsspi_device,
				       unsigned long arg)
{
	struct vfsspi_ioctl_register_signal usr_signal;
	if (copy_from_user(&usr_signal, (void __user *)arg, sizeof(usr_signal)) != 0) {
		
		printk("[fp][SYN] Failed copy from user.\n");
		return -EFAULT;
	} else {
		vfsspi_device->user_pid = usr_signal.user_pid;
		vfsspi_device->signal_id = usr_signal.signal_id;
	}
	return 0;
}

static irqreturn_t vfsspi_irq(int irq, void *context)
{
	struct vfsspi_device_data *vfsspi_device = context;

#ifdef MTK_PLATFORM
	if (mt_get_gpio_in(vfsspi_device->drdy_pin) == DRDY_ACTIVE_STATUS) {
#else
	if (gpio_get_value(vfsspi_device->drdy_pin) == DRDY_ACTIVE_STATUS) {
#endif
		vfsspi_sendDrdyNotify(vfsspi_device);
	}

	return IRQ_HANDLED;
}

static int vfsspi_sendDrdyEventFd(struct vfsspi_device_data *vfsSpiDev)
{
    struct task_struct *t;
    struct file *efd_file = NULL;
    struct eventfd_ctx *efd_ctx = NULL;	int ret = 0;

    
    printk("[fp][SYN]vfsspi_sendDrdyEventFd\n");

    if (vfsSpiDev->user_pid != 0) {
        rcu_read_lock();
        
        
        
        t = pid_task(find_pid_ns(vfsSpiDev->user_pid, &init_pid_ns),
            PIDTYPE_PID);
        if (t == NULL) {
            
            printk("[fp][SYN] No such pid\n");
            rcu_read_unlock();
            return -ENODEV;
        }
        efd_file = fcheck_files(t->files, vfsSpiDev->signal_id);
        rcu_read_unlock();

        if (efd_file == NULL) {
            
            printk("[fp][SYN] No such efd_file\n");
            return -ENODEV;
        }
        
        efd_ctx = eventfd_ctx_fileget(efd_file);
        if (efd_ctx == NULL) {
            
            printk("[fp][SYN] eventfd_ctx_fileget is failed\n");
            return -ENODEV;
        }

        
        eventfd_signal(efd_ctx, 1);

        
        eventfd_ctx_put(efd_ctx);
    }

    return ret;
}

static int vfsspi_sendDrdyNotify(struct vfsspi_device_data *vfsSpiDev)
{
    int ret = 0;

    if (vfsSpiDev->drdy_ntf_type == VFSSPI_DRDY_NOTIFY_TYPE_EVENTFD) {
	printk("[fp][SYN] in vfsspi_sendDrdyNotify - vfsSpiDev->drdy_ntf_type == VFSSPI_DRDY_NOTIFY_TYPE_EVENTFD\n");
        ret = vfsspi_sendDrdyEventFd(vfsSpiDev);
    } else {
	printk("[fp][SYN] in vfsspi_sendDrdyNotify - vfsSpiDev->drdy_ntf_type = %x\n", vfsSpiDev->drdy_ntf_type);
        ret = vfsspi_send_drdy_signal(vfsSpiDev);
    }

    return ret;
}

static int vfsspi_enableIrq(struct vfsspi_device_data *vfsspi_device)
{
	
	printk("[fp][SYN] vfsspi_enableIrq +++\n");

	if (vfsspi_device->is_drdy_irq_enabled == DRDY_IRQ_ENABLE) {
		
		printk("[fp][SYN] DRDY irq already enabled\n");
		return -EINVAL;
	}

	enable_irq(gpio_irq);
	vfsspi_device->is_drdy_irq_enabled = DRDY_IRQ_ENABLE;
	printk("[fp][SYN] vfsspi_enableIrq ---\n");

	return 0;
}

static int vfsspi_disableIrq(struct vfsspi_device_data *vfsspi_device)
{
	
	printk("[fp][SYN] vfsspi_disableIrq +++\n");

	if (vfsspi_device->is_drdy_irq_enabled == DRDY_IRQ_DISABLE) {
		
		printk("[fp][SYN] DRDY irq already disabled\n");
		return -EINVAL;
	}

	disable_irq_nosync(gpio_irq);
	vfsspi_device->is_drdy_irq_enabled = DRDY_IRQ_DISABLE;
	printk("[fp][SYN] vfsspi_disableIrq.\n");

	return 0;
}
static int vfsspi_set_drdy_int(struct vfsspi_device_data *vfsspi_device,
			       unsigned long arg)
{
	unsigned short drdy_enable_flag;
	if (copy_from_user(&drdy_enable_flag, (void __user *)arg,
			   sizeof(drdy_enable_flag)) != 0) {
		
		printk("[fp][SYN] Failed copy from user.\n");
		return -EFAULT;
	}
	if (drdy_enable_flag == 0)
			vfsspi_disableIrq(vfsspi_device);
	else {
			vfsspi_enableIrq(vfsspi_device);
#ifdef MTK_PLATFORM
			if (mt_get_gpio_in(vfsspi_device->drdy_pin) ==
#else
			if (gpio_get_value(vfsspi_device->drdy_pin) ==
#endif
				DRDY_ACTIVE_STATUS) {
				vfsspi_sendDrdyNotify(vfsspi_device);
			}
	}
	return 0;
}

#ifdef MTK_PLATFORM
static void vfsspi_hardReset(struct vfsspi_device_data *vfsspi_device)
{
	
	printk("[fp][SYN] vfsspi_hardReset MTK\n");

	if (vfsspi_device != NULL) {
		int error = 0;
		
		error = mt_set_gpio_out(GPIO_FP_RST_PIN,GPIO_OUT_ZERO);
		if (error)
  			printk(KERN_ERR "[fp][SYN] gpio_direction_output(reset) failed.\n");
		mdelay(1);
		error = mt_set_gpio_out(GPIO_FP_RST_PIN,GPIO_OUT_ONE);
		if (error) 
  			printk(KERN_ERR "[fp][SYN] gpio_direction_output(reset) failed.\n");
		mdelay(10);
	}
}

static void vfsspi_suspend(struct vfsspi_device_data *vfsspi_device)
{
	
	printk("[fp][SYN] vfsspi_suspend MTK\n");

	if (vfsspi_device != NULL) {
		
		int error = 0;
		error = mt_set_gpio_out(GPIO_FP_RST_PIN,GPIO_OUT_ZERO);
		if (error)
  			printk(KERN_ERR "[fp][SYN] gpio_direction_output(reset) failed.\n");
		
	}
}
#else

static void vfsspi_hardReset(struct vfsspi_device_data *vfsspi_device)
{
	
	printk("[fp][SYN] vfsspi_hardReset\n");

	if (vfsspi_device != NULL) {
		gpio_set_value(vfsspi_device->sleep_pin, 0);
		mdelay(1);
		gpio_set_value(vfsspi_device->sleep_pin, 1);
		mdelay(10);
	}
}

static void vfsspi_suspend(struct vfsspi_device_data *vfsspi_device)
{
	
	printk("[fp][SYN] vfsspi_suspend\n");

	if (vfsspi_device != NULL) {
		
		gpio_set_value(vfsspi_device->sleep_pin, 0);
		
	}
}

#endif



static long vfsspi_ioctl(struct file *filp, unsigned int cmd,
			unsigned long arg)
{
	int ret_val = 0;
	struct vfsspi_device_data *vfsspi_device = NULL;

	
	

	if (_IOC_TYPE(cmd) != VFSSPI_IOCTL_MAGIC) {
		
		
		printk("[fp][SYN] invalid magic. cmd=0x%X Received=0x%X Expected=0x%X\n",
			cmd, _IOC_TYPE(cmd), VFSSPI_IOCTL_MAGIC);
		return -ENOTTY;
	}

	vfsspi_device = filp->private_data;
	mutex_lock(&vfsspi_device->buffer_mutex);
	switch (cmd) {
	case VFSSPI_IOCTL_DEVICE_RESET:
		
		
		vfsspi_hardReset(vfsspi_device);
		break;
	case VFSSPI_IOCTL_DEVICE_SUSPEND:
	{
		pr_debug("VFSSPI_IOCTL_DEVICE_SUSPEND:\n");
		vfsspi_suspend(vfsspi_device);
		break;
	}		
	case VFSSPI_IOCTL_RW_SPI_MESSAGE:
		
		
		ret_val = vfsspi_rw_spi_message(vfsspi_device, arg);
		break;
	case VFSSPI_IOCTL_SET_CLK:
		
		
		ret_val = vfsspi_set_clk(vfsspi_device, arg);
		break;
	case VFSSPI_IOCTL_REGISTER_DRDY_SIGNAL:
		
		
		ret_val = vfsspi_register_drdy_signal(vfsspi_device, arg);
		break;
	case VFSSPI_IOCTL_SET_DRDY_INT:
		
		
		ret_val = vfsspi_set_drdy_int(vfsspi_device, arg);
		break;
	case VFSSPI_IOCTL_SELECT_DRDY_NTF_TYPE:
        {
            vfsspi_iocSelectDrdyNtfType_t drdyTypes;

            
	    

            if (copy_from_user(&drdyTypes, (void __user *)arg,
                sizeof(vfsspi_iocSelectDrdyNtfType_t)) != 0) {
                    
	            printk("[fp][SYN] copy from user failed.\n");
                    ret_val = -EFAULT;
            } else {
                if (0 != (drdyTypes.supportedTypes & VFSSPI_DRDY_NOTIFY_TYPE_EVENTFD)) {
                    vfsspi_device->drdy_ntf_type = VFSSPI_DRDY_NOTIFY_TYPE_EVENTFD;
		    printk("[fp][SYN] vfsspi_device->drdy_ntf_type = VFSSPI_DRDY_NOTIFY_TYPE_EVENTFD\n");
                } else {
                    vfsspi_device->drdy_ntf_type = VFSSPI_DRDY_NOTIFY_TYPE_SIGNAL;
		    printk("[fp][SYN] vfsspi_device->drdy_ntf_type = VFSSPI_DRDY_NOTIFY_TYPE_SIGNAL\n");
                }
                drdyTypes.selectedType = vfsspi_device->drdy_ntf_type;
                if (copy_to_user((void __user *)arg, &(drdyTypes),
                    sizeof(vfsspi_iocSelectDrdyNtfType_t)) == 0) {
                        ret_val = 0;
			printk("[fp][SYN] sizeof(vfsspi_iocSelectDrdyNtfType_t) = %zu\n", sizeof(vfsspi_iocSelectDrdyNtfType_t));
                } else {
                    
	            printk("[fp][SYN] copy to user failed\n");
                }
            }
            break;
        }
	default:
		printk("[fp][SYN] Not define IOCTL cmd =0x%x\n", cmd);
		ret_val = -EFAULT;
		break;
	}
	mutex_unlock(&vfsspi_device->buffer_mutex);
	return ret_val;
}

static int vfsspi_open(struct inode *inode, struct file *filp)
{
	struct vfsspi_device_data *vfsspi_device = NULL;
	int status = -ENXIO;

	
	printk("[fp][SYN] vfsspi_open\n");

	mutex_lock(&device_list_mutex);

	list_for_each_entry(vfsspi_device, &device_list, device_entry) {
		if (vfsspi_device->devt == inode->i_rdev) {
			status = 0;
			break;
		}
	}

	if (status == 0) {
		mutex_lock(&vfsspi_device->kernel_lock);
		if (vfsspi_device->is_opened != 0) {
			status = -EBUSY;
			
			printk("[fp][SYN] vfsspi_open: is_opened != 0, -EBUSY\n");
			goto vfsspi_open_out;
		}
		vfsspi_device->user_pid = 0;
        vfsspi_device->drdy_ntf_type = VFSSPI_DRDY_NOTIFY_TYPE_SIGNAL;
		if (vfsspi_device->buffer != NULL) {
			
			printk("[fp][SYN] vfsspi_open: buffer != NULL\n");
			goto vfsspi_open_out;
		}
		vfsspi_device->null_buffer =
			kmalloc(DEFAULT_BUFFER_SIZE, GFP_KERNEL);
		if (vfsspi_device->null_buffer == NULL) {
			status = -ENOMEM;
			
			printk("[fp][SYN] vfsspi_open: null_buffer == NULL, -ENOMEM\n");
			goto vfsspi_open_out;
		}
		vfsspi_device->buffer =
			kmalloc(DEFAULT_BUFFER_SIZE, GFP_KERNEL);
		if (vfsspi_device->buffer == NULL) {
			status = -ENOMEM;
			kfree(vfsspi_device->null_buffer);
			
			printk("[fp][SYN] vfsspi_open: buffer == NULL, -ENOMEM\n");
			goto vfsspi_open_out;
		}
		vfsspi_device->is_opened = 1;
		filp->private_data = vfsspi_device;
		nonseekable_open(inode, filp);

vfsspi_open_out:
		mutex_unlock(&vfsspi_device->kernel_lock);
	}
	mutex_unlock(&device_list_mutex);
	return status;
}


static int vfsspi_release(struct inode *inode, struct file *filp)
{
	struct vfsspi_device_data *vfsspi_device = NULL;
	int                   status     = 0;

	
	printk("[fp][SYN] vfsspi_release\n");

	mutex_lock(&device_list_mutex);
	vfsspi_device = filp->private_data;
	filp->private_data = NULL;
	vfsspi_device->is_opened = 0;
	if (vfsspi_device->buffer != NULL) {
		kfree(vfsspi_device->buffer);
		vfsspi_device->buffer = NULL;
	}

	if (vfsspi_device->null_buffer != NULL) {
		kfree(vfsspi_device->null_buffer);
		vfsspi_device->null_buffer = NULL;
	}

	mutex_unlock(&device_list_mutex);
	return status;
}

static const struct file_operations vfsspi_fops = {
	.owner   = THIS_MODULE,
	.write   = vfsspi_write,
	.read    = vfsspi_read,
	.unlocked_ioctl   = vfsspi_ioctl,
        .compat_ioctl = vfsspi_ioctl,
	.open    = vfsspi_open,
	.release = vfsspi_release,
};

#ifdef MTK_PLATFORM
void PerformReset(void)
{
	int error = 0;
		
	error = mt_set_gpio_out(GPIO_FP_RST_PIN,GPIO_OUT_ZERO);
	if (error) {
  		printk(KERN_ERR "[fp][SYN] gpio_direction_output(reset) failed.\n");
	}
	mdelay(1);
	error = mt_set_gpio_out(GPIO_FP_RST_PIN,GPIO_OUT_ONE);
	if (error) {
  		printk(KERN_ERR "[fp][SYN] gpio_direction_output(reset) failed.\n");
	}
	mdelay(5);
}
#else
void PerformReset(void)
{
	gpio_set_value(vfsspi_pdata->reset_gpio, 0);
	mdelay(1);
	gpio_set_value(vfsspi_pdata->reset_gpio, 1);
	mdelay(5);
}
#endif

static ssize_t fpr_test(struct spi_device *spi)
{
  char tx_buf[64] = {1};
  char  rx_buf[64] = {0};
  struct spi_transfer t;
  struct spi_message m;
  int i = 0, pos_ind = 0;
  int retry_time = 0;
#ifdef MTK_PLATFORM
 struct mt_chip_conf *chip_config = NULL;
 chip_config = ( struct mt_chip_conf * ) spi->controller_data;
 chip_config->com_mod = FIFO_TRANSFER;
 chip_config->pause = 0;
#endif

  while(retry_time < 5)
  {
  
  printk(KERN_ERR "[fp][SYN] fingerprint get firmware test (retry time = %d)\n", retry_time);
  pos_ind = 0;
  memset(&tx_buf, 1, sizeof(tx_buf));
  memset(&rx_buf, 0, sizeof(rx_buf));

  tx_buf[0]=1;   
  tx_buf[1]=0; 

  spi->bits_per_word = 8;
  spi->max_speed_hz= 9600000;
  spi->mode = SPI_MODE_0;
  spi_setup(spi);

  PerformReset();
  mdelay(5);

  
  memset(&t, 0, sizeof(t));
  t.tx_buf = tx_buf;
  t.rx_buf = rx_buf;
  t.len = 6;

  spi_message_init(&m);
  spi_message_add_tail(&t, &m);
  printk(KERN_ERR "[fp][SYN] ValiditySensor: spi_sync returned %d \n", spi_sync(spi, &m));
  
  
  
  
  
  mdelay(10);

  tx_buf[0]=1;   
  tx_buf[1]=0; 
  memset(&t, 0, sizeof(t));

  t.tx_buf = tx_buf; 
  t.rx_buf = rx_buf; 
  t.len = 6; 
  spi_message_init(&m); 
  spi_message_add_tail(&t, &m); 
  
  printk(KERN_ERR "[fp][SYN] ValiditySensor: spi_sync returned %d \n", spi_sync(spi, &m)); 
  
  
  
  
  mdelay(5); 
 
  tx_buf[0]=2;   
  tx_buf[1]=1; 
 
  memset(&t, 0, sizeof(t)); 
  t.tx_buf = tx_buf; 
  t.rx_buf = rx_buf; 
  t.len = 2; 
  spi_message_init(&m); 
  spi_message_add_tail(&t, &m); 
  printk(KERN_ERR "[fp][SYN] ValiditySensor: spi_sync returned %d \n", spi_sync(spi, &m)); 
  mdelay(5); 

#ifdef MTK_PLATFORM
 chip_config = ( struct mt_chip_conf * ) spi->controller_data;
 chip_config->com_mod = DMA_TRANSFER;
 chip_config->pause = 1;
 spi_setup(spi);
#endif
 
  tx_buf[0]=3; 
  tx_buf[1]=0; 
 
  memset(&t, 0, sizeof(t)); 
  t.tx_buf = tx_buf; 
  t.rx_buf = rx_buf; 
  t.len = 40; 
  spi_message_init(&m); 
  spi_message_add_tail(&t, &m); 
  printk(KERN_ERR "[fp][SYN] ValiditySensor: spi_sync returned %d \n", spi_sync(spi, &m)); 
  
  
  
  

  pos_ind += snprintf(firmware_syn+pos_ind, sizeof(firmware_syn), "[FP]FW Ver:[ ");
  for(i = 0; i < 20; i++)
  	pos_ind += snprintf(firmware_syn+pos_ind, sizeof(firmware_syn), "%0x ", rx_buf[i]);

  pos_ind += snprintf(firmware_syn+pos_ind, sizeof(firmware_syn), "]");
  if((rx_buf[0]  == 0xff || rx_buf[0]  == 0x7f)&& rx_buf[1]  == 0xff && rx_buf[2]  == 0x0  && rx_buf[3]  == 0x0) 
  {
  	printk(KERN_ERR "[fp][SYN] ValiditySensor: firmware version verify PASS\n"); 
  	pos_ind += snprintf(firmware_syn+pos_ind, sizeof(firmware_syn), "[PASS]\n");
  	fp_sensor = 2;
	retry_time = 7;

  }
  else
  {
  	printk(KERN_ERR "[fp][SYN] ValiditySensor: firmware version verify FAIL\n"); 
  	pos_ind += snprintf(firmware_syn+pos_ind, sizeof(firmware_syn), "[FAIL]\n");
  	fp_sensor = 0;
	retry_time++;
	if(retry_time > 5)
		printk(KERN_ERR "[fp][SYN] ValiditySensor: retry more than 5 times still not get firmware\n");
  }

#ifdef MTK_PLATFORM
 chip_config = ( struct mt_chip_conf * ) spi->controller_data;
 chip_config->com_mod = FIFO_TRANSFER;
 chip_config->pause = 0;
 spi_setup(spi);
#endif

  }
  return pos_ind;

}
#ifdef CONFIG_OF
static int vfsspi_get_of_pdata(struct device *dev, struct vfsspi_platform_data *pdata)
{
	struct device_node *node = dev->of_node;
	printk("[fp][SYN] %s +++ \n", __func__);

#ifdef GPIO_POWER

	if (node == NULL) {
		dev_err(dev, "%s: Could not find OF device node\n", __func__);
	}
	
	pdata->irq_gpio     = GPIO_FP_EINT_PIN;
	pdata->reset_gpio   = GPIO_FP_RST_PIN;
	pdata->cs_gpio      = -EINVAL;

#else
	pdata->irq_gpio     = of_get_named_gpio(node, "fpr_irq", 0);
	pdata->reset_gpio   = of_get_named_gpio(node, "fpr_reset", 0);
	pdata->cs_gpio      = of_get_named_gpio(node, "fpr_cs0", 0);
	pdata->pwr_1v8_gpio = of_get_named_gpio(node, "fpr_1v8", 0);
	pdata->pwr_3v3_gpio = of_get_named_gpio(node, "fpr_3v3", 0);
	printk("[fp][SYN] %s irq#%d reset#%d 1v8#%d 3v3#%d\n", __func__, pdata->irq_gpio, pdata->reset_gpio, pdata->pwr_1v8_gpio, pdata->pwr_3v3_gpio);
#endif

	printk("[fp][SYN] %s --- \n", __func__);
	return 0;
}

#else
static int vfsspi_get_of_pdata(struct device *dev, struct vfsspi_platform_data *pdata)
{
	pdata->reset_gpio = -EINVAL;
	pdata->irq_gpio   = -EINVAL;
	pdata->cs_gpio    = -EINVAL;

	return -ENODEV;
}
#endif

#ifdef MTK_PLATFORM
static int fpr_power_control(int power_state, struct vfsspi_platform_data *pdata)
{
	static int pin_initialization = 0;
	if(pin_initialization == 0)
	{
		mt_set_gpio_mode(GPIO_FP_RST_PIN, GPIO_FP_RST_PIN_M_GPIO);
		mt_set_gpio_dir(GPIO_FP_RST_PIN, GPIO_DIR_OUT);
		mt_set_gpio_out(GPIO_FP_RST_PIN,GPIO_OUT_ONE);
		pin_initialization = 1;
	}

	switch(power_state)
	{
		case 0: 
			printk("[fp][SYN] Disable power source.\n");
			hwPowerDown(MT6331_POWER_LDO_VGP1, "FP_3V3");
		       	mdelay(10);
			hwPowerDown(MT6331_POWER_LDO_VCAMA, "FP_1V8");
		       	mdelay(100);
		       	break;

		case 1: 
			printk("[fp][SYN] Enable power source.\n");
			hwPowerOn(MT6331_POWER_LDO_VCAMA, VOL_1800, "FP_1V8");
			mdelay(10);
			hwPowerOn(MT6331_POWER_LDO_VGP1, VOL_3300, "FP_3V3");
			mdelay(50);
			mt_set_gpio_out(GPIO_FP_RST_PIN,GPIO_OUT_ZERO);
			mdelay(100);
			mt_set_gpio_out(GPIO_FP_RST_PIN,GPIO_OUT_ONE);
		       	break;

		case 3: 
		printk("[fp][SYN] Device power-on sequence.\n");
			hwPowerDown(MT6331_POWER_LDO_VCAMA, "FP_1V8");
		       	mdelay(10);
			hwPowerDown(MT6331_POWER_LDO_VGP1, "FP_3V3");
		       	mdelay(10);
			mt_set_gpio_out(GPIO_FP_RST_PIN,GPIO_OUT_ONE);
		       	mdelay(100);
			hwPowerOn(MT6331_POWER_LDO_VCAMA, VOL_1800, "FP_1V8");
		       	mdelay(10);
			hwPowerOn(MT6331_POWER_LDO_VGP1, VOL_3300, "FP_3V3");
		       	mdelay(50);
			mt_set_gpio_out(GPIO_FP_RST_PIN,GPIO_OUT_ZERO);
			mdelay(100);
			mt_set_gpio_out(GPIO_FP_RST_PIN,GPIO_OUT_ONE);
		       	mdelay(100);
		       break;
	}

	return 0; 
}
#else
static int fpr_power_control(int power_state, struct vfsspi_platform_data *pdata)
{
	static int pin_initialization = 0;
        int retval;

	if(pin_initialization == 0)
	{
		retval = gpio_request(pdata->pwr_1v8_gpio, "fpc_1v8");
		if (retval)
			printk("[fp][SYN] gpio_request (1v8) failed.\n");

		retval = gpio_request(pdata->pwr_3v3_gpio, "fpc_3v3");
		if (retval)
			printk("[fp][SYN] gpio_request (3v3) failed.\n");

		retval = gpio_request(pdata->reset_gpio, "fpc_reset");
		if (retval)
			printk("[fp][SYN] gpio_request (3v3) failed.\n");

		retval = gpio_direction_output(pdata->pwr_1v8_gpio, 0);
		if (retval)
			printk("[fp][SYN] gpio_direction_output (1v8) failed.\n");

		retval = gpio_direction_output(pdata->pwr_3v3_gpio, 0);
		if (retval)
			printk("[fp][SYN] gpio_direction_output (3v3) failed.\n");

		retval = gpio_direction_output(pdata->reset_gpio, 1);
		if (retval)
			printk("[fp][SYN] gpio_direction_output (reset) failed.\n");

		pin_initialization = 1;
	}

	switch(power_state)
	{
		case 0: 
		printk("[fp][SYN] Disable power source.\n");
		       gpio_set_value(pdata->pwr_3v3_gpio, 0);
		       mdelay(10);
		       gpio_set_value(pdata->pwr_1v8_gpio, 0);
		       mdelay(100);
		       break;

		case 1: 
		printk("[fp][SYN] Enable power source.\n");
		       gpio_set_value(pdata->pwr_1v8_gpio, 1);
		       mdelay(10);
		       gpio_set_value(pdata->pwr_3v3_gpio, 1);
		       mdelay(50);
		       gpio_set_value(pdata->reset_gpio, 0);
		       mdelay(100);
		       gpio_set_value(pdata->reset_gpio, 1);
		       break;

		case 3: 
		printk("[fp][SYN] Device power-on sequence.\n");
		       gpio_set_value(pdata->pwr_1v8_gpio, 0);
		       mdelay(10);
		       gpio_set_value(pdata->pwr_3v3_gpio, 0);
		       mdelay(10);
		       gpio_set_value(pdata->reset_gpio, 1);
		       mdelay(100);
		       gpio_set_value(pdata->pwr_1v8_gpio, 1);
		       mdelay(10);
		       gpio_set_value(pdata->pwr_3v3_gpio, 1);
		       mdelay(50);
		       gpio_set_value(pdata->reset_gpio, 0);
		       mdelay(100);
		       gpio_set_value(pdata->reset_gpio, 1);
		       mdelay(100);
		       break;
	}

	return 0; 

}
#endif

static int vfsspi_probe(struct spi_device *spi)
{
	int status = 0;
	struct vfsspi_device_data *vfsspi_device;
	struct device *dev = &spi->dev;
	
	
	printk("[fp][SYN] vfsspi_probe+++\n");

	if(fp_sensor == 0)
		fingerprint_sysfs_init(); 
	else
	{
		printk("[fp][SYN] another sensor embedded!\n");
		return 0;
	}

    	fingerprint_pdata = spi->controller_data;

	vfsspi_get_of_pdata(dev, &pdata_of);
	vfsspi_pdata = &pdata_of;
	fpr_power_control(3, vfsspi_pdata); 

	vfsspi_device = kzalloc(sizeof(*vfsspi_device), GFP_KERNEL);

	if (vfsspi_device == NULL)
	{
		printk("[fp][SYN] vfsspi_probe: vfsspi_device is NULL\n");
		return -ENOMEM;
	}

	vfsspi_device->drdy_pin  = vfsspi_pdata->irq_gpio;
	vfsspi_device->sleep_pin = vfsspi_pdata->reset_gpio;
	fp_spi = spi;
	fpr_test(spi); 

	if(fp_sensor == 0) 
	{
		printk("[fp][SYN] vfsspi_probe: Cannot find any fingerprint sensor\n");
		
		kfree(vfsspi_device);
		return 0;
	}

	
	vfsspi_device->current_spi_speed = SLOW_BAUD_RATE;
	vfsspi_device->spi = spi;
	spin_lock_init(&vfsspi_device->vfs_spi_lock);
	mutex_init(&vfsspi_device->buffer_mutex);
	mutex_init(&vfsspi_device->kernel_lock);

	INIT_LIST_HEAD(&vfsspi_device->device_entry);

#ifndef MTK_PLATFORM
	printk("[fp][SYN] vfsspi_device->drdy_pin = %d\n", vfsspi_device->drdy_pin);
	if (gpio_request(vfsspi_device->drdy_pin, "vfsspi_drdy") < 0) {
		status = -EBUSY;
		printk("[fp][SYN] vfsspi_probe: gpio request DRDY pin fail\n");
		goto vfsspi_probe_drdy_failed;
	}
#if DO_CHIP_SELECT
	pr_debug("HANDLING CHIP SELECT");
	vfsspi_device->cs_pin  = VFSSPI_CS_PIN;
	if (gpio_request(vfsspi_device->cs_pin, "vfsspi_cs") < 0) {
		status = -EBUSY;
		printk("[fp][SYN] vfsspi_probe: gpio request CS pin fail\n");
		goto vfsspi_probe_cs_failed;
	}
	status = gpio_direction_output(vfsspi_device->cs_pin, 1);
	if (status < 0) {
		printk("[fp][SYN] vfsspi_probe: gpio_direction_input CS failed\n");
		
		status = -EBUSY;
		goto vfsspi_probe_gpio_init_failed;
	}
	gpio_set_value(vfsspi_device->cs_pin, 1);
#endif


	status = gpio_direction_input(vfsspi_device->drdy_pin);
	if (status < 0) {
		
		printk("[fp][SYN] vfsspi_probe: gpio_direction_output DRDY failed\n");
		status = -EBUSY;
		goto vfsspi_probe_gpio_init_failed;
	}

	gpio_irq = gpio_to_irq(vfsspi_device->drdy_pin);

	if (gpio_irq < 0) {
		
		printk("[fp][SYN] vfsspi_probe: gpio_to_irq failed (drdy)\n");
		status = -EBUSY;
		goto vfsspi_probe_gpio_init_failed;
	}

#else
	struct device_node *node = NULL;
	u32 ints[2] = {0,0};
	printk("[fp][SYN] vfsspi_device->drdy_pin = %d\n", vfsspi_device->drdy_pin);
	printk("[fp][SYN] vfsspi_device->sleep_pin = %d\n", vfsspi_device->sleep_pin);

	if (vfsspi_pdata->irq_gpio != -EINVAL) {

		node = of_find_compatible_node(NULL, NULL, "mediatek, FP-eint");

		of_property_read_u32_array(node , "debounce", ints, ARRAY_SIZE(ints));
		gpio_set_debounce(ints[0], ints[1]);

		gpio_irq = irq_of_parse_and_map(node, 0);
		
		if (gpio_irq < 0) {
			printk("[fp][SYN] gpio_to_irq failed.\n");
			goto vfsspi_probe_irq_failed;
		}

		
	} else {
		printk("[fp][SYN] vfsspi_probe: vfsspi_probe_irq_failed\n");
		goto vfsspi_probe_irq_failed;
	}

#endif
	if (request_irq(gpio_irq, vfsspi_irq, IRQF_TRIGGER_RISING,
			"vfsspi_irq", vfsspi_device) < 0) {
		
		printk("[fp][SYN] vfsspi_probe: request_irq failed (drdy)\n");
		status = -EBUSY;
		goto vfsspi_probe_irq_failed;
	}

    	vfsspi_device->is_drdy_irq_enabled = DRDY_IRQ_ENABLE;
	spi->bits_per_word = BITS_PER_WORD;
	spi->max_speed_hz = MAX_BAUD_RATE;
	spi->mode = SPI_MODE_0;

	status = spi_setup(spi);

	if (status != 0)
	{
		printk("[fp][SYN] vfsspi_probe: spi_setup fail\n");
		goto vfsspi_probe_failed;
	}

	mutex_lock(&device_list_mutex);
	
	
	status = alloc_chrdev_region(&(vfsspi_device->devt),
				     0, 1, VALIDITY_PART_NAME);
	if (status < 0) {
		
		printk("[fp][SYN] vfsspi_probe: alloc_chrdev_region failed\n");
		goto vfsspi_probe_alloc_chardev_failed;
	}

	cdev_init(&(vfsspi_device->cdev), &vfsspi_fops);
	vfsspi_device->cdev.owner = THIS_MODULE;
	status = cdev_add(&(vfsspi_device->cdev), vfsspi_device->devt, 1);
	if (status < 0) {
		
		printk("[fp][SYN] vfsspi_probe: cdev_add failed\n");
		unregister_chrdev_region(vfsspi_device->devt, 1);
		goto vfsspi_probe_cdev_add_failed;
	}

	vfsspi_device_class = class_create(THIS_MODULE, "validity_fingerprint");

	if (IS_ERR(vfsspi_device_class)) {
		
		printk("[fp][SYN] vfsspi_probe: class_create() is failed - unregister chrdev.\n");
		cdev_del(&(vfsspi_device->cdev));
		unregister_chrdev_region(vfsspi_device->devt, 1);
		status = PTR_ERR(vfsspi_device_class);
		goto vfsspi_probe_class_create_failed;
	}

	dev = device_create(vfsspi_device_class, &spi->dev,
			    vfsspi_device->devt, vfsspi_device, "vfsspi");
	status = IS_ERR(dev) ? PTR_ERR(dev) : 0;
	if (status == 0)
		list_add(&vfsspi_device->device_entry, &device_list);
	mutex_unlock(&device_list_mutex);

	if (status != 0)
	{
		printk("[fp][SYN] vfsspi_probe: status fail.\n");
		goto vfsspi_probe_failed;
	}

	spi_set_drvdata(spi, vfsspi_device);

	
	printk("[fp][SYN] vfsspi_probe successful");

	return 0;

vfsspi_probe_failed:
vfsspi_probe_class_create_failed:
	cdev_del(&(vfsspi_device->cdev));
vfsspi_probe_cdev_add_failed:
	unregister_chrdev_region(vfsspi_device->devt, 1);
vfsspi_probe_alloc_chardev_failed:
vfsspi_probe_irq_failed:
	free_irq(gpio_irq, vfsspi_device);
vfsspi_probe_gpio_init_failed:
#if DO_CHIP_SELECT
		gpio_free(vfsspi_device->cs_pin);
vfsspi_probe_cs_failed:
#endif
	gpio_free(vfsspi_device->sleep_pin);
vfsspi_probe_drdy_failed:
	mutex_destroy(&vfsspi_device->buffer_mutex);
	mutex_destroy(&vfsspi_device->kernel_lock);
	kfree(vfsspi_device);
	
	printk("[fp][SYN] vfsspi_probe failed!!\n");
	return status;
}

static int vfsspi_remove(struct spi_device *spi)
{
	int status = 0;

	struct vfsspi_device_data *vfsspi_device = NULL;

	
	printk("[fp][SYN] vfsspi_remove\n");

	vfsspi_device = spi_get_drvdata(spi);

	if (vfsspi_device != NULL) {
		spin_lock_irq(&vfsspi_device->vfs_spi_lock);
		vfsspi_device->spi = NULL;
		spi_set_drvdata(spi, NULL);
		spin_unlock_irq(&vfsspi_device->vfs_spi_lock);

		mutex_lock(&device_list_mutex);

		free_irq(gpio_irq, vfsspi_device);
#if DO_CHIP_SELECT
		gpio_free(vfsspi_device->cs_pin);
#endif
		gpio_free(vfsspi_device->sleep_pin);
		gpio_free(vfsspi_device->drdy_pin);

		
		list_del(&vfsspi_device->device_entry);
		device_destroy(vfsspi_device_class, vfsspi_device->devt);
		class_destroy(vfsspi_device_class);
		cdev_del(&(vfsspi_device->cdev));
		unregister_chrdev_region(vfsspi_device->devt, 1);

		mutex_destroy(&vfsspi_device->buffer_mutex);
		mutex_destroy(&vfsspi_device->kernel_lock);

		kfree(vfsspi_device);
		mutex_unlock(&device_list_mutex);
	}

	return status;
}

struct spi_driver vfsspi_spi = {
	.driver = {
		.name  = VALIDITY_PART_NAME,
		.owner = THIS_MODULE,
		.bus	= &spi_bus_type,
		.of_match_table = validity_metallica_table,
	},
		.probe  = vfsspi_probe,
		.remove = vfsspi_remove,
};

#ifdef MTK_PLATFORM
static struct spi_board_info spi_board_devs[] __initdata = {
	[0] = {
        .modalias="validity_fingerprint",
		.bus_num = 0,
		.chip_select= 0,
		.mode = SPI_MODE_0,
	},
};
#endif
static int __init vfsspi_init(void)
{
	int status = 0;

	
	printk("[fp][SYN] vfsspi_init\n");

#ifdef MTK_PLATFORM
	printk("[fp][SYN] vfsspi_spi_register_board_info!\n");
	spi_register_board_info(spi_board_devs, ARRAY_SIZE(spi_board_devs));
#endif

	status = spi_register_driver(&vfsspi_spi);
	if (status < 0) {
		
		printk("[fp][SYN] vfsspi_init: spi_register_driver() is failed - unregister chrdev.\n");
		return status;
	}
	
	printk("[fp][SYN] init is successful\n");

	return status;
}

static void __exit vfsspi_exit(void)
{
	pr_debug("vfsspi_exit\n");
	printk("[fp][SYN] vfsspi_exit\n");
	spi_unregister_driver(&vfsspi_spi);
}

module_init(vfsspi_init);
module_exit(vfsspi_exit);

MODULE_DESCRIPTION("Validity FPS sensor");
MODULE_LICENSE("GPL");
