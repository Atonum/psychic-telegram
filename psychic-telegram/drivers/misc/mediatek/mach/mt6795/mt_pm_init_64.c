#include <linux/pm.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/xlog.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#include "mach/irqs.h"
#include "mach/sync_write.h"
#include "mach/mt_reg_base.h"
#include "mach/mt_typedefs.h"
#include "mach/mt_spm.h"
#include "mach/mt_sleep.h"
#include "mach/mt_dcm.h"
#include "mach/mt_clkmgr.h"
#include "mach/mt_cpufreq.h"
#include "mach/mt_gpufreq.h"
#include "mach/mt_dormant.h"
#include "mach/mt_cpuidle.h"
#include "mach/hotplug.h"
#include <mach/mt_boot.h>                   
#include <mach/upmu_common.h>               
#include <mach/mt_spm_mtcmos_internal.h>    
#include <mach/mt_clkbuf_ctl.h>

#define pminit_write(addr, val)         mt_reg_sync_writel((val), ((void *)(addr)))
#define pminit_read(addr)               __raw_readl(IOMEM(addr))

extern int mt_clkmgr_init(void);
extern void mt_idle_init(void);
extern void mt_power_off(void);
extern void mt_dcm_init(void);
extern int set_da9210_buck_en(int en_bit); 
extern void bigcore_power_off(void); 
extern void bigcore_power_on(void); 

#if 0
static void _power_off_ca15l_vproc_vsram(void)
{
    
    pminit_write(SPM_SLEEP_DUAL_VCORE_PWR_CON, pminit_read(SPM_SLEEP_DUAL_VCORE_PWR_CON) | VCA15_PWR_ISO);
#if 1
    printk("bigcore_power_off\n");
    bigcore_power_off();
#else
    printk("_power_off_ca15l_vproc_vsram\n");

    
    set_da9210_buck_en(0);

    
    mt6331_upmu_set_rg_vsram_dvfs1_en(0);
#endif
}

static void _power_on_ca15l_vproc_vsram(void)
{
#if 1
    printk("bigcore_power_on\n");
    bigcore_power_on();
#else
    printk("_power_on_ca15l_vproc_vsram\n");

    
    mt6331_upmu_set_rg_vsram_dvfs1_en(1);

    
    set_da9210_buck_en(1);
#endif
    
    pminit_write(SPM_SLEEP_DUAL_VCORE_PWR_CON, pminit_read(SPM_SLEEP_DUAL_VCORE_PWR_CON) & ~VCA15_PWR_ISO);
}

static int ext_buck_read(struct seq_file *m, void *v)
{
    if ((pminit_read(SPM_SLEEP_DUAL_VCORE_PWR_CON) & VCA15_PWR_ISO) == VCA15_PWR_ISO)
        seq_printf(m, "0\n");
    else
        seq_printf(m, "1\n");

    return 0;
}

static int ext_buck_write(struct file *file, const char __user *buffer,
                size_t count, loff_t *data)
{
    char desc[128];
    int len = 0;
    int val;

    len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
    if (copy_from_user(desc, buffer, len)) {
        return 0;
    }
    desc[len] = '\0';

    if (sscanf(desc, "%d", &val) == 1) {
        switch(val)
        {
            case 0:
                _power_off_ca15l_vproc_vsram();
                break;
            case 1:
                _power_on_ca15l_vproc_vsram();
                break;
            default:
                break;
        }
    }
    return count;
}

static int proc_ext_buck_open(struct inode *inode, struct file *file)
{
    return single_open(file, ext_buck_read, NULL);
}

static const struct file_operations ext_buck_fops = {
    .owner = THIS_MODULE,
    .open  = proc_ext_buck_open,
    .read  = seq_read,
    .write = ext_buck_write,
};
#endif


#define TOPCK_LDVT

#ifdef TOPCK_LDVT
unsigned int ckgen_meter(int val)
{
	int output = 0, i = 0;
    unsigned int temp, clk26cali_0, clk_cfg_9, clk_misc_cfg_1;

    clk26cali_0 = DRV_Reg32(CLK26CALI_0);
    pminit_write(CLK26CALI_0, clk26cali_0 | 0x80); 

    clk_misc_cfg_1 = DRV_Reg32(CLK_MISC_CFG_1);
    pminit_write(CLK_MISC_CFG_1, 0x00FFFFFF); 

    clk_cfg_9 = DRV_Reg32(CLK_CFG_9);
    pminit_write(CLK_CFG_9, (val << 16)); 

    temp = DRV_Reg32(CLK26CALI_0);
    pminit_write(CLK26CALI_0, temp | 0x10); 

    
    while (DRV_Reg32(CLK26CALI_0) & 0x10)
    {
        printk("%d, wait for frequency meter finish, CLK26CALI = 0x%x\n", val, DRV_Reg32(CLK26CALI_0));
        mdelay(10);
        i++;
        if(i > 10)
        	break;
    }

    temp = DRV_Reg32(CLK26CALI_2) & 0xFFFF;

    output = (temp * 26000) / 1024; 

    pminit_write(CLK_CFG_9, clk_cfg_9);
    pminit_write(CLK_MISC_CFG_1, clk_misc_cfg_1);
    pminit_write(CLK26CALI_0, clk26cali_0);

    if(i>10)
        return 0;
    else
        return output;
}

unsigned int abist_meter(int val)
{
    int output = 0, i = 0;
    unsigned int temp, clk26cali_0, clk_cfg_8, clk_misc_cfg_1,  ap_pll_con0;

    ap_pll_con0 = DRV_Reg32(AP_PLL_CON0);
    pminit_write(AP_PLL_CON0, ap_pll_con0 | 0x50);

    clk26cali_0 = DRV_Reg32(CLK26CALI_0);
    pminit_write(CLK26CALI_0, clk26cali_0 | 0x80); 

    clk_misc_cfg_1 = DRV_Reg32(CLK_MISC_CFG_1);
    pminit_write(CLK_MISC_CFG_1, 0xFFFFFF00); 

    clk_cfg_8 = DRV_Reg32(CLK_CFG_8);
    pminit_write(CLK_CFG_8, (val << 8)); 

    temp = DRV_Reg32(CLK26CALI_0);
    pminit_write(CLK26CALI_0, temp | 0x1); 

    
    while (DRV_Reg32(CLK26CALI_0) & 0x1)
    {
        printk("%d, wait for frequency meter finish, CLK26CALI = 0x%x\n", val, DRV_Reg32(CLK26CALI_0));
        mdelay(10);
        i++;
        if(i > 10)
        	break;
    }

    temp = DRV_Reg32(CLK26CALI_1) & 0xFFFF;

    output = (temp * 26000) / 1024; 

    pminit_write(CLK_CFG_8, clk_cfg_8);
    pminit_write(CLK_MISC_CFG_1, clk_misc_cfg_1);
    pminit_write(CLK26CALI_0, clk26cali_0);
    pminit_write(AP_PLL_CON0, ap_pll_con0);

    if(i>10)
        return 0;
    else
        return output;
}

const char *ckgen_array[] = 
{
    "hd_faxi_ck", "hd_faxi_ck", "hf_fdpi0_ck", "hf_fddrphycfg_ck", 
    "hf_fmm_ck", "hf_fpwm_ck", "hf_fvdec_ck",  "hf_fvenc_ck",
    "hf_fmfg_ck", "hf_fcamtg_ck", "hf_fuart_ck", "hf_fspi_ck", 
    "f_fusb20_ck", "f_fusb30_ck", "hf_fmsdc50_0_hclk_ck", "hf_fmsdc50_0_ck", 
    "hf_fmsdc30_1_ck", "hf_fmsdc30_2_ck", "hf_fmsdc30_3_ck", "hf_faudio_ck", 
    "hf_faud_intbus_ck", "hf_fpmicspi_ck", "hf_fscp_ck", "hf_fatb_ck",
    "hf_fmjc_ck", "hf_firda_ck", "hf_fcci400_ck","hf_faud_1_ck", 
    "hf_faud_2_ck", "hf_fmem_mfg_in_as_ck", "hf_faxi_mfg_in_as_ck", "f_frtc_ck",
    "f_f26m_ck", "f_f32k_md1_ck", "f_frtc_conn_ck", "hg_fmipicfg_ck", 
    "hd_haxi_nli_ck", "NULL", "NULL", "NULL",
    "NULL", "hf_fscam_ck"
};

static int ckgen_meter_read(struct seq_file *m, void *v)
{
	int i;

	for(i=1; i<43; i++)
    	seq_printf(m, "%s: %d\n", ckgen_array[i-1], ckgen_meter(i));

    return 0;
}

static int ckgen_meter_write(struct file *file, const char __user *buffer,
                size_t count, loff_t *data)
{
    char desc[128];
    int len = 0;
    int val;

    len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
    if (copy_from_user(desc, buffer, len)) {
        return 0;
    }
    desc[len] = '\0';

    if (sscanf(desc, "%d", &val) == 1) {
        printk("ckgen_meter %d is %d\n", val, ckgen_meter(val));
    }
    return count;
}


static int abist_meter_read(struct seq_file *m, void *v)
{
	int i;

	for(i=1; i<48; i++)
    	seq_printf(m, "%d\n", abist_meter(i));

    return 0;
}
static int abist_meter_write(struct file *file, const char __user *buffer,
                size_t count, loff_t *data)
{
    char desc[128];
    int len = 0;
    int val;

    len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
    if (copy_from_user(desc, buffer, len)) {
        return 0;
    }
    desc[len] = '\0';

    if (sscanf(desc, "%d", &val) == 1) {
        printk("abist_meter %d is %d\n", val, abist_meter(val));
    }
    return count;
}

static int proc_abist_meter_open(struct inode *inode, struct file *file)
{
    return single_open(file, abist_meter_read, NULL);
}
static const struct file_operations abist_meter_fops = {
    .owner = THIS_MODULE,
    .open  = proc_abist_meter_open,
    .read  = seq_read,
    .write = abist_meter_write,
};

static int proc_ckgen_meter_open(struct inode *inode, struct file *file)
{
    return single_open(file, ckgen_meter_read, NULL);
}
static const struct file_operations ckgen_meter_fops = {
    .owner = THIS_MODULE,
    .open  = proc_ckgen_meter_open,
    .read  = seq_read,
    .write = ckgen_meter_write,
};

#endif


unsigned int mt_get_emi_freq(void)
{
    int output = 0, i = 0;
    unsigned int temp, clk26cali_0, clk_cfg_8, clk_misc_cfg_1;

    clk26cali_0 = DRV_Reg32(CLK26CALI_0);
    pminit_write(CLK26CALI_0, clk26cali_0 | 0x80); 

    clk_misc_cfg_1 = DRV_Reg32(CLK_MISC_CFG_1);
    pminit_write(CLK_MISC_CFG_1, 0xFFFFFF00); 

    clk_cfg_8 = DRV_Reg32(CLK_CFG_8);
    pminit_write(CLK_CFG_8, (24 << 8)); 

    temp = DRV_Reg32(CLK26CALI_0);
    pminit_write(CLK26CALI_0, temp | 0x1); 

    
    while (DRV_Reg32(CLK26CALI_0) & 0x1)
    {
        printk("wait for frequency meter finish, CLK26CALI = 0x%x\n", DRV_Reg32(CLK26CALI_0));
        mdelay(10);
        i++;
        if(i > 10)
                break;
    }

    temp = DRV_Reg32(CLK26CALI_1) & 0xFFFF;

    output = (temp * 26000) / 1024; 

    pminit_write(CLK_CFG_8, clk_cfg_8);
    pminit_write(CLK_MISC_CFG_1, clk_misc_cfg_1);
    pminit_write(CLK26CALI_0, clk26cali_0);

    

    return output;
}
EXPORT_SYMBOL(mt_get_emi_freq);

unsigned int mt_get_bus_freq(void)
{
#if 1
    int output = 0, i = 0;
    unsigned int temp, clk26cali_0, clk_cfg_9, clk_misc_cfg_1;

    clk26cali_0 = DRV_Reg32(CLK26CALI_0);
    pminit_write(CLK26CALI_0, clk26cali_0 | 0x80); 

    clk_misc_cfg_1 = DRV_Reg32(CLK_MISC_CFG_1);
    pminit_write(CLK_MISC_CFG_1, 0x00FFFFFF); 

    clk_cfg_9 = DRV_Reg32(CLK_CFG_9);
    pminit_write(CLK_CFG_9, (1 << 16)); 

    temp = DRV_Reg32(CLK26CALI_0);
    pminit_write(CLK26CALI_0, temp | 0x10); 

    
    while (DRV_Reg32(CLK26CALI_0) & 0x10)
    {
        printk("wait for bus frequency meter finish, CLK26CALI = 0x%x\n", DRV_Reg32(CLK26CALI_0));
        mdelay(10);
        i++;
        if(i > 10)
                break;
    }

    temp = DRV_Reg32(CLK26CALI_2) & 0xFFFF;

    output = (temp * 26000) / 1024; 

    pminit_write(CLK_CFG_9, clk_cfg_9);
    pminit_write(CLK_MISC_CFG_1, clk_misc_cfg_1);
    pminit_write(CLK26CALI_0, clk26cali_0);

    

    return output;
#else
    unsigned int mainpll_con0, mainpll_con1, main_diff;
    unsigned int clk_cfg_0, bus_clk;
    unsigned int output_freq = 0;

    clk_cfg_0 = DRV_Reg32(CLK_CFG_0);

    mainpll_con0 = DRV_Reg32(MAINPLL_CON0);
    mainpll_con1 = DRV_Reg32(MAINPLL_CON1);

    
    main_diff = (((mainpll_con1 & 0x1FFFFF) >> 12) - 0x9A) / 2;

    if ((mainpll_con0 & 0xFF) == 0x01)
    {
        output_freq = 1001 + (main_diff * 13); 
    }

    if ((clk_cfg_0 & 0x7) == 1) 
    {
        bus_clk = ((output_freq * 1000) / 2) / 2;
    }
    else if ((clk_cfg_0 & 0x7) == 2) 
    {
        bus_clk = (output_freq * 1000) / 5;
    }
    else if ((clk_cfg_0 & 0x7) == 3) 
    {
        bus_clk = ((output_freq * 1000) / 2) / 4;
    }
    else if ((clk_cfg_0 & 0x7) == 4) 
    {
        bus_clk = (1248 * 1000) / 5;
    }
    else if ((clk_cfg_0 & 0x7) == 5) 
    {
        bus_clk = ((1248 * 1000) / 3) / 2;
    }
    else if ((clk_cfg_0 & 0x7) == 6) 
    {
        bus_clk = (533 * 1000) / 2;
    }
    else if ((clk_cfg_0 & 0x7) == 7) 
    {
        bus_clk = ((533 * 1000) / 2) / 2 ;
    }
    else 
    {
        bus_clk = 26 * 1000;
    }

    

    return bus_clk; 
#endif
}
EXPORT_SYMBOL(mt_get_bus_freq);

unsigned int mt_get_cpu_freq(void)
{
    int output = 0, i = 0;
    unsigned int temp, clk26cali_0, clk_cfg_8, clk_misc_cfg_1;
    unsigned int top_ckmuxsel, top_ckdiv1, ir_rosc_ctl;

    clk26cali_0 = DRV_Reg32(CLK26CALI_0);
    pminit_write(CLK26CALI_0, clk26cali_0 | 0x80); 

    clk_misc_cfg_1 = DRV_Reg32(CLK_MISC_CFG_1);
    pminit_write(CLK_MISC_CFG_1, 0xFFFFFF00); 

    clk_cfg_8 = DRV_Reg32(CLK_CFG_8);
    pminit_write(CLK_CFG_8, (46 << 8)); 

    
    DRV_WriteReg32(mcu_dbg, 0x1);

    top_ckmuxsel = DRV_Reg32(TOP_CKMUXSEL);
    pminit_write(TOP_CKMUXSEL, (top_ckmuxsel & 0xFFFFFFFC) | 0x1);

    top_ckdiv1 = DRV_Reg32(TOP_CKDIV1);
    pminit_write(TOP_CKDIV1, (top_ckdiv1 & 0xFFFFFFE0) | 0xb);

    ir_rosc_ctl = DRV_Reg32(IR_ROSC_CTL);
    pminit_write(IR_ROSC_CTL, ir_rosc_ctl | 0x08100000);

    temp = DRV_Reg32(CLK26CALI_0);
    pminit_write(CLK26CALI_0, temp | 0x1); 

    
    while (DRV_Reg32(CLK26CALI_0) & 0x1)
    {
        printk("wait for frequency meter finish, CLK26CALI = 0x%x\n", DRV_Reg32(CLK26CALI_0));
        mdelay(10);
        i++;
        if(i > 10)
                break;
    }

    temp = DRV_Reg32(CLK26CALI_1) & 0xFFFF;

    output = ((temp * 26000) / 1024) * 4; 

    pminit_write(CLK_CFG_8, clk_cfg_8);
    pminit_write(CLK_MISC_CFG_1, clk_misc_cfg_1);
    pminit_write(CLK26CALI_0, clk26cali_0);
    pminit_write(TOP_CKMUXSEL, top_ckmuxsel);
    pminit_write(TOP_CKDIV1, top_ckdiv1);
    pminit_write(IR_ROSC_CTL, ir_rosc_ctl);

    
    DRV_WriteReg32(mcu_dbg, 0x0);

    

    return output;
}
EXPORT_SYMBOL(mt_get_cpu_freq);
#if 0
unsigned int mt_get_bigcpu_freq(void)
{
    int output = 0, i = 0;
    unsigned int temp, clk26cali_0, clk_cfg_8, clk_misc_cfg_1;
    unsigned int top_ckmuxsel, top_ckdiv1, ir_rosc_ctl, ca15l_mon_sel;

    clk26cali_0 = DRV_Reg32(CLK26CALI_0);
    pminit_write(CLK26CALI_0, clk26cali_0 | 0x80); 

    clk_misc_cfg_1 = DRV_Reg32(CLK_MISC_CFG_1);
    pminit_write(CLK_MISC_CFG_1, 0xFFFFFF00); 

    clk_cfg_8 = DRV_Reg32(CLK_CFG_8);
    pminit_write(CLK_CFG_8, (46 << 8)); 

    top_ckmuxsel = DRV_Reg32(TOP_CKMUXSEL);
    pminit_write(TOP_CKMUXSEL, (top_ckmuxsel & 0xFFFFFFF3) | (0x1<<2));

    top_ckdiv1 = DRV_Reg32(TOP_CKDIV1);
    pminit_write(TOP_CKDIV1, (top_ckdiv1 & 0xFFFFFC1F) | (0xb<<5));

    ca15l_mon_sel = DRV_Reg32(CA15L_MON_SEL);
    DRV_WriteReg32(CA15L_MON_SEL, ca15l_mon_sel | 0x00000500);

    ir_rosc_ctl = DRV_Reg32(IR_ROSC_CTL);
    pminit_write(IR_ROSC_CTL, ir_rosc_ctl | 0x10000000);

    temp = DRV_Reg32(CLK26CALI_0);
    pminit_write(CLK26CALI_0, temp | 0x1); 

    
    while (DRV_Reg32(CLK26CALI_0) & 0x1)
    {
        printk("wait for frequency meter finish, CLK26CALI = 0x%x\n", DRV_Reg32(CLK26CALI_0));
        mdelay(10);
        i++;
        if(i > 10)
                break;
    }

    temp = DRV_Reg32(CLK26CALI_1) & 0xFFFF;

    output = ((temp * 26000) / 1024) * 4; 

    pminit_write(CLK_CFG_8, clk_cfg_8);
    pminit_write(CLK_MISC_CFG_1, clk_misc_cfg_1);
    pminit_write(CLK26CALI_0, clk26cali_0);
    pminit_write(TOP_CKMUXSEL, top_ckmuxsel);
    pminit_write(TOP_CKDIV1, top_ckdiv1);
    DRV_WriteReg32(CA15L_MON_SEL, ca15l_mon_sel);
    pminit_write(IR_ROSC_CTL, ir_rosc_ctl);

    

    return output;
}
EXPORT_SYMBOL(mt_get_bigcpu_freq);
#endif

unsigned int mt_get_mmclk_freq(void)
{
    int output = 0, i = 0;
    unsigned int temp, clk26cali_0, clk_cfg_9, clk_misc_cfg_1;

    clk26cali_0 = DRV_Reg32(CLK26CALI_0);
    pminit_write(CLK26CALI_0, clk26cali_0 | 0x80); 

    clk_misc_cfg_1 = DRV_Reg32(CLK_MISC_CFG_1);
    pminit_write(CLK_MISC_CFG_1, 0x00FFFFFF); 

    clk_cfg_9 = DRV_Reg32(CLK_CFG_9);
    pminit_write(CLK_CFG_9, (5 << 16)); 

    temp = DRV_Reg32(CLK26CALI_0);
    pminit_write(CLK26CALI_0, temp | 0x10); 

    
    while (DRV_Reg32(CLK26CALI_0) & 0x10)
    {
        printk("wait for emi frequency meter finish, CLK26CALI = 0x%x\n", DRV_Reg32(CLK26CALI_0));
        mdelay(10);
        i++;
        if(i > 10)
                break;
    }

    temp = DRV_Reg32(CLK26CALI_2) & 0xFFFF;

    output = (temp * 26000) / 1024; 

    pminit_write(CLK_CFG_9, clk_cfg_9);
    pminit_write(CLK_MISC_CFG_1, clk_misc_cfg_1);
    pminit_write(CLK26CALI_0, clk26cali_0);

    return output;
}
EXPORT_SYMBOL(mt_get_mmclk_freq);

unsigned int mt_get_mfgclk_freq(void)
{
    int output = 0, i = 0;
    unsigned int temp, clk26cali_0, clk_cfg_9, clk_misc_cfg_1;

    clk26cali_0 = DRV_Reg32(CLK26CALI_0);
    pminit_write(CLK26CALI_0, clk26cali_0 | 0x80); 

    clk_misc_cfg_1 = DRV_Reg32(CLK_MISC_CFG_1);
    pminit_write(CLK_MISC_CFG_1, 0x00FFFFFF); 

    clk_cfg_9 = DRV_Reg32(CLK_CFG_9);
    pminit_write(CLK_CFG_9, (9 << 16)); 

    temp = DRV_Reg32(CLK26CALI_0);
    pminit_write(CLK26CALI_0, temp | 0x10); 

    
    while (DRV_Reg32(CLK26CALI_0) & 0x10)
    {
        printk("wait for emi frequency meter finish, CLK26CALI = 0x%x\n", DRV_Reg32(CLK26CALI_0));
        mdelay(10);
        i++;
        if(i > 10)
                break;
    }

    temp = DRV_Reg32(CLK26CALI_2) & 0xFFFF;

    output = (temp * 26000) / 1024; 

    pminit_write(CLK_CFG_9, clk_cfg_9);
    pminit_write(CLK_MISC_CFG_1, clk_misc_cfg_1);
    pminit_write(CLK26CALI_0, clk26cali_0);

    return output;
}
EXPORT_SYMBOL(mt_get_mfgclk_freq);

static int cpu_speed_dump_read(struct seq_file *m, void *v)
{
    seq_printf(m, "%d\n", mt_get_cpu_freq());
    return 0;
}
#if 0
static int bigcpu_speed_dump_read(struct seq_file *m, void *v)
{
    seq_printf(m, "%d\n", mt_get_bigcpu_freq());
    return 0;
}
#endif
static int emi_speed_dump_read(struct seq_file *m, void *v)
{
    seq_printf(m, "%d\n", mt_get_emi_freq());
    return 0;
}

static int bus_speed_dump_read(struct seq_file *m, void *v)
{
    seq_printf(m, "%d\n", mt_get_bus_freq());
    return 0;
}

static int mmclk_speed_dump_read(struct seq_file *m, void *v)
{
    seq_printf(m, "%d\n", mt_get_mmclk_freq());
    return 0;
}

static int mfgclk_speed_dump_read(struct seq_file *m, void *v)
{
    seq_printf(m, "%d\n", mt_get_mfgclk_freq());
    return 0;
}

static int proc_cpu_open(struct inode *inode, struct file *file)
{
    return single_open(file, cpu_speed_dump_read, NULL);
}
static const struct file_operations cpu_fops = {
    .owner = THIS_MODULE,
    .open  = proc_cpu_open,
    .read  = seq_read,
};
#if 0
static int proc_bigcpu_open(struct inode *inode, struct file *file)
{
    return single_open(file, bigcpu_speed_dump_read, NULL);
}
static const struct file_operations bigcpu_fops = {
    .owner = THIS_MODULE,
    .open  = proc_bigcpu_open,
    .read  = seq_read,
};
#endif
static int proc_emi_open(struct inode *inode, struct file *file)
{
    return single_open(file, emi_speed_dump_read, NULL);
}
static const struct file_operations emi_fops = {
    .owner = THIS_MODULE,
    .open  = proc_emi_open,
    .read  = seq_read,
};

static int proc_bus_open(struct inode *inode, struct file *file)
{
    return single_open(file, bus_speed_dump_read, NULL);
}
static const struct file_operations bus_fops = {
    .owner = THIS_MODULE,
    .open  = proc_bus_open,
    .read  = seq_read,
};

static int proc_mmclk_open(struct inode *inode, struct file *file)
{
    return single_open(file, mmclk_speed_dump_read, NULL);
}
static const struct file_operations mmclk_fops = {
    .owner = THIS_MODULE,
    .open  = proc_mmclk_open,
    .read  = seq_read,
};

static int proc_mfgclk_open(struct inode *inode, struct file *file)
{
    return single_open(file, mfgclk_speed_dump_read, NULL);
}
static const struct file_operations mfgclk_fops = {
    .owner = THIS_MODULE,
    .open  = proc_mfgclk_open,
    .read  = seq_read,
};



static int __init mt_power_management_init(void)
{
    struct proc_dir_entry *entry = NULL;
    struct proc_dir_entry *pm_init_dir = NULL;
    CHIP_SW_VER ver = mt_get_chip_sw_ver();

    pm_power_off = mt_power_off;

    #if !defined (CONFIG_MTK_FPGA)
     
    #if 0
    xlog_printk(ANDROID_LOG_INFO, "Power/PM_INIT", "Bus Frequency = %d KHz\n", mt_get_bus_freq());
    #endif

    

    mt_cpu_dormant_init();

    
    spm_module_init();

    
    slp_module_init();
    mt_clkmgr_init();

#if 0
    
    if (ver == CHIP_SW_VER_02)
        enable_pll(ARMCA15PLL, "ca15pll");
#endif
    

    mt_dcm_init(); 

    pm_init_dir = proc_mkdir("pm_init", NULL);
    pm_init_dir = proc_mkdir("pm_init", NULL);
    if (!pm_init_dir)
    {
        pr_err("[%s]: mkdir /proc/pm_init failed\n", __FUNCTION__);
    }
    else
    {
        entry = proc_create("cpu_speed_dump", S_IRUGO, pm_init_dir, &cpu_fops);

        

        entry = proc_create("emi_speed_dump", S_IRUGO, pm_init_dir, &emi_fops);

        entry = proc_create("bus_speed_dump", S_IRUGO, pm_init_dir, &bus_fops);

        entry = proc_create("mmclk_speed_dump", S_IRUGO, pm_init_dir, &mmclk_fops);

        entry = proc_create("mfgclk_speed_dump", S_IRUGO, pm_init_dir, &mfgclk_fops);
#ifdef TOPCK_LDVT
        entry = proc_create("abist_meter_test", S_IRUGO|S_IWUSR, pm_init_dir, &abist_meter_fops);
        entry = proc_create("ckgen_meter_test", S_IRUGO|S_IWUSR, pm_init_dir, &ckgen_meter_fops);
#endif
#if 0
        entry = proc_create("ext_buck", S_IRUGO, pm_init_dir, &ext_buck_fops);
#endif        
    }

    #endif

    return 0;
}

arch_initcall(mt_power_management_init);

#if 0
static void _pmic_late_init(void)
{
    CHIP_SW_VER ver = mt_get_chip_sw_ver();

    pr_warn("mt_get_chip_sw_ver: %d\n", ver);
    if (ver == CHIP_SW_VER_01)
    {
#ifndef CONFIG_MTK_FORCE_CLUSTER1
        enable_pll(ARMCA15PLL, "pll");
        disable_pll(ARMCA15PLL, "pll");

        _power_off_ca15l_vproc_vsram();
#endif 
    }
}
#endif

static int __init mt_pm_late_init(void)
{
	
#if !defined (MT_DORMANT_UT)
    mt_idle_init ();
#endif 
    clk_buf_init();
#if 0
    _pmic_late_init();

#endif    
    return 0;
}

late_initcall(mt_pm_late_init);


MODULE_DESCRIPTION("MTK Power Management Init Driver");
MODULE_LICENSE("GPL");
