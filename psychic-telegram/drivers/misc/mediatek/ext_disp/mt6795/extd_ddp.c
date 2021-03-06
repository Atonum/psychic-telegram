#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/module.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/vmalloc.h>


#include "mtkfb_info.h"
#include "mtkfb.h"

#include "ddp_hal.h"
#include "ddp_dump.h"
#include "ddp_path.h"
#include "ddp_drv.h"
#include "ddp_info.h"

#include <mach/m4u.h>
#include <mach/m4u_port.h>
#include "cmdq_def.h"
#include "cmdq_record.h"
#include "cmdq_reg.h"
#include "cmdq_core.h"

#include "ddp_manager.h"
#include "ddp_mmp.h"
#include "ddp_dpi.h"

#include "extd_platform.h"
#include "extd_drv.h"
#include "extd_drv_log.h"
#include "extd_lcm.h"
#include "extd_utils.h"
#include "extd_ddp.h"
#include "extd_kernel_drv.h"

#include "disp_session.h"
#include "display_recorder.h"

extern int dprec_mmp_dump_ovl_layer(OVL_CONFIG_STRUCT *ovl_layer,unsigned int l,unsigned int session);

int ext_disp_use_cmdq = CMDQ_ENABLE;
int ext_disp_use_m4u = 1;
EXT_DISP_PATH_MODE ext_disp_mode = EXTD_DIRECT_LINK_MODE;


#define ALIGN_TO(x, n)  \
    (((x) + ((n) - 1)) & ~((n) - 1))

typedef enum
{
	EXTD_DEINIT = 0,
	EXTD_INIT,
	EXTD_RESUME,
	EXTD_SUSPEND
}EXTD_POWER_STATE;

typedef struct
{
	EXTD_POWER_STATE				state;
	int 							init;
	unsigned int                    session;
	int							    need_trigger_overlay;
	EXT_DISP_PATH_MODE 	            mode;
	unsigned int					last_vsync_tick;
	struct mutex 					lock;
	extd_drv_handle *				plcm;
	cmdqRecHandle 				    cmdq_handle_config;
	cmdqRecHandle 				    cmdq_handle_trigger;
    disp_path_handle 				dpmgr_handle;
    disp_path_handle 				ovl2mem_path_handle;
}ext_disp_path_context;

#define pgc	_get_context()

static int is_context_inited = 0;

static ext_disp_path_context* _get_context(void)
{	
	static ext_disp_path_context g_context;
	if(!is_context_inited)
	{
		memset((void*)&g_context, 0, sizeof(ext_disp_path_context));
		is_context_inited = 1;
		DISPMSG("_get_context set is_context_inited\n");
	}

	return &g_context;
}

EXT_DISP_PATH_MODE get_ext_disp_path_mode()
{
    return ( ext_disp_mode );
}

static void _ext_disp_path_lock(void)
{
	extd_sw_mutex_lock(NULL);
}

static void _ext_disp_path_unlock(void)
{
	extd_sw_mutex_unlock(NULL);
}

static DISP_MODULE_ENUM _get_dst_module_by_lcm(extd_drv_handle *plcm)
{
	if(plcm == NULL)
	{
		DISPERR("plcm is null\n");
		return DISP_MODULE_UNKNOWN;
	}
	
	if(plcm->params->type == LCM_TYPE_DSI)
	{
		if(plcm->lcm_if_id == LCM_INTERFACE_DSI0)
		{
			return DISP_MODULE_DSI0;
		}
		else if(plcm->lcm_if_id == LCM_INTERFACE_DSI1)
		{
			return DISP_MODULE_DSI1;
		}
		else if(plcm->lcm_if_id == LCM_INTERFACE_DSI_DUAL)
		{
			return DISP_MODULE_DSIDUAL;
		}
		else
		{
			return DISP_MODULE_DSI0;
		}
	}
	else if(plcm->params->type == LCM_TYPE_DPI)
	{
		return DISP_MODULE_DPI;
	}
	else
	{
		DISPERR("can't find ext display path dst module\n");
		return DISP_MODULE_UNKNOWN;
	}
}


static int _should_wait_path_idle(void)
{
	if(ext_disp_cmdq_enabled())
	{
		if(ext_disp_is_video_mode())
		{
			return 0;
		}
		else
		{
			return 0;
		}
	}
	else
	{
		if(ext_disp_is_video_mode())
		{
			return dpmgr_path_is_busy(pgc->dpmgr_handle);
		}
		else
		{
			return dpmgr_path_is_busy(pgc->dpmgr_handle);
		}
	}
}

static int _should_update_lcm(void)
{
	if(ext_disp_cmdq_enabled())
	{
		if(ext_disp_is_video_mode())
		{
			return 0;
		}
		else
		{
			
			return 0;
		}
	}
	else
	{
		if(ext_disp_is_video_mode())
		{
			return 0;
		}
		else
		{
			return 1;
		}
	}
}

static int _should_start_path(void)
{
	if(ext_disp_cmdq_enabled())
	{
		if(ext_disp_is_video_mode())
		{
			return dpmgr_path_is_idle(pgc->dpmgr_handle);
		}
		else
		{
			return 1;
		}
	}
	else
	{
		if(ext_disp_is_video_mode())
		{
			return dpmgr_path_is_idle(pgc->dpmgr_handle);
		}
		else
		{
			return 1;
		}
	}
}

static int _should_trigger_path(void)
{

	
	
	if(ext_disp_cmdq_enabled())
	{
		if(ext_disp_is_video_mode())
		{
			return dpmgr_path_is_idle(pgc->dpmgr_handle);
		}
		else
		{
			return 0;
		}
	}
	else
	{
		if(ext_disp_is_video_mode())
		{
			return dpmgr_path_is_idle(pgc->dpmgr_handle);
		}
		else
		{
			return 1;
		}
	}
}

static int _should_set_cmdq_dirty(void)
{
	if(ext_disp_cmdq_enabled())
	{
		if(ext_disp_is_video_mode())
		{
			return 0;
		}
		else
		{
			return 1;
		}
	}
	else
	{
		if(ext_disp_is_video_mode())
		{
			return 0;
		}
		else
		{
			return 0;
		}
	}
}

static int _should_flush_cmdq_config_handle(void)
{
	if(ext_disp_cmdq_enabled())
	{
		if(ext_disp_is_video_mode())
		{
			return 1;
		}
		else
		{
			return 1;
		}
	}
	else
	{
		if(ext_disp_is_video_mode())
		{
			return 0;
		}
		else
		{
			return 0;
		}
	}
}

static int _should_reset_cmdq_config_handle(void)
{
	if(ext_disp_cmdq_enabled())
	{
		if(ext_disp_is_video_mode())
		{
			return 1;
		}
		else
		{
			return 1;
		}
	}
	else
	{
		if(ext_disp_is_video_mode())
		{
			return 0;
		}
		else
		{
			return 0;
		}
	}
}

static int _should_insert_wait_frame_done_token(void)
{
  
	if(ext_disp_cmdq_enabled())
	{
		if(ext_disp_is_video_mode())
		{
			return 1;
		}
		else
		{
			return 1;
		}
	}
	else
	{
		if(ext_disp_is_video_mode())
		{
			return 0;
		}
		else
		{
			return 0;
		}
	}
}

static int _should_trigger_interface(void)
{
	if(pgc->mode == EXTD_DECOUPLE_MODE)
	{
		return 0;
	}
	else
	{
		return 1;
	}
}

static int _should_config_ovl_input(void)
{
	
	if(pgc->mode == EXTD_SINGLE_LAYER_MODE ||pgc->mode == EXTD_DEBUG_RDMA_DPI_MODE)
		return 0;
	else
		return 1;

}

#define OOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOO
static long int get_current_time_us(void)
{
    struct timeval t;
    do_gettimeofday(&t);
    return (t.tv_sec & 0xFFF) * 1000000 + t.tv_usec;
}

static enum hrtimer_restart _DISP_CmdModeTimer_handler(struct hrtimer *timer)
{
	DISPMSG("fake timer, wake up\n");
	dpmgr_signal_event(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC);		
#if 0
	if((get_current_time_us() - pgc->last_vsync_tick) > 16666)
	{
		dpmgr_signal_event(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC);		
		pgc->last_vsync_tick = get_current_time_us();
	}
#endif
		return HRTIMER_RESTART;
}

static int _init_vsync_fake_monitor(int fps)
{
	static struct hrtimer cmd_mode_update_timer;
	static ktime_t cmd_mode_update_timer_period;

	if(fps == 0) 
		fps = 60;
	
       cmd_mode_update_timer_period = ktime_set(0 , 1000/fps*1000);
        DISPMSG("[MTKFB] vsync timer_period=%d \n", 1000/fps);
        hrtimer_init(&cmd_mode_update_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
        cmd_mode_update_timer.function = _DISP_CmdModeTimer_handler;

	return 0;
}

static int _build_path_direct_link(void)
{
	int ret = 0;

	DISP_MODULE_ENUM dst_module = 0;
	DISPFUNC(); 
	pgc->mode = EXTD_DIRECT_LINK_MODE;
	
	pgc->dpmgr_handle = dpmgr_create_path(DDP_SCENARIO_SUB_DISP, pgc->cmdq_handle_config);
	if(pgc->dpmgr_handle)
	{
		DISPCHECK("dpmgr create path SUCCESS(%p)\n", pgc->dpmgr_handle);
	}
	else
	{
		DISPCHECK("dpmgr create path FAIL\n");
		return -1;
	}
	
	dst_module = DISP_MODULE_DPI;
	dpmgr_path_set_dst_module(pgc->dpmgr_handle, dst_module);
	DISPCHECK("dpmgr set dst module FINISHED(%s)\n", ddp_get_module_name(dst_module));
	{
		M4U_PORT_STRUCT sPort;
		sPort.ePortID = M4U_PORT_DISP_OVL1;
		sPort.Virtuality = ext_disp_use_m4u;					   
		sPort.Security = 0;
		sPort.Distance = 1;
		sPort.Direction = 0;
		ret = m4u_config_port(&sPort);
		if(ret == 0)
		{
			DISPCHECK("config M4U Port %s to %s SUCCESS\n",ddp_get_module_name(M4U_PORT_DISP_OVL1), ext_disp_use_m4u?"virtual":"physical");
		}
		else
		{
			DISPCHECK("config M4U Port %s to %s FAIL(ret=%d)\n",ddp_get_module_name(M4U_PORT_DISP_OVL1), ext_disp_use_m4u?"virtual":"physical", ret);
			return -1;
		}
	}
	
	
	dpmgr_set_lcm_utils(pgc->dpmgr_handle, pgc->plcm->drv);

	dpmgr_enable_event(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC);
	dpmgr_enable_event(pgc->dpmgr_handle, DISP_PATH_EVENT_FRAME_DONE);
	dpmgr_enable_event(pgc->dpmgr_handle, DISP_PATH_EVENT_FRAME_START);

	return ret;
}


static int _build_path_decouple(void)
{}

static int _build_path_single_layer(void)
{}

static int _build_path_debug_rdma_dpi(void)
{
	int ret = 0;

	DISP_MODULE_ENUM dst_module = 0;
	
	pgc->mode = EXTD_DEBUG_RDMA_DPI_MODE;  
	
	pgc->dpmgr_handle = dpmgr_create_path(DDP_SCENARIO_SUB_RDMA2_DISP, pgc->cmdq_handle_config);
	if(pgc->dpmgr_handle)
	{
		DISPCHECK("dpmgr create path SUCCESS(%p)\n", pgc->dpmgr_handle);
	}
	else
	{
		DISPCHECK("dpmgr create path FAIL\n");
		return -1;
	}
	
	dst_module = _get_dst_module_by_lcm(pgc->plcm);
	dpmgr_path_set_dst_module(pgc->dpmgr_handle, dst_module);
	DISPCHECK("dpmgr set dst module FINISHED(%s)\n", ddp_get_module_name(dst_module));
	
	{
		M4U_PORT_STRUCT sPort;
		sPort.ePortID = M4U_PORT_DISP_RDMA2;
		sPort.Virtuality = ext_disp_use_m4u;					   
		sPort.Security = 0;
		sPort.Distance = 1;
		sPort.Direction = 0;
		ret = m4u_config_port(&sPort);
		if(ret == 0)
		{
			DISPCHECK("config M4U Port %s to %s SUCCESS\n",ddp_get_module_name(DISP_MODULE_RDMA2), ext_disp_use_m4u?"virtual":"physical");
		}
		else
		{
			DISPCHECK("config M4U Port %s to %s FAIL(ret=%d)\n",ddp_get_module_name(DISP_MODULE_RDMA2), ext_disp_use_m4u?"virtual":"physical", ret);
			return -1;
		}
	}
	
	dpmgr_set_lcm_utils(pgc->dpmgr_handle, pgc->plcm->drv);

	
	dpmgr_enable_event(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC);
	dpmgr_enable_event(pgc->dpmgr_handle, DISP_PATH_EVENT_FRAME_DONE);

	return ret;
}

static void _cmdq_build_trigger_loop(void)
{
	int ret = 0;
	cmdqRecCreate(CMDQ_SCENARIO_TRIGGER_LOOP, &(pgc->cmdq_handle_trigger));
	DISPMSG("ext_disp path trigger thread cmd handle=%p\n", pgc->cmdq_handle_trigger);
	cmdqRecReset(pgc->cmdq_handle_trigger);  

	if(ext_disp_is_video_mode())
	{
		
		
		ret = cmdqRecWait(pgc->cmdq_handle_trigger, dpmgr_path_get_mutex(pgc->dpmgr_handle) + CMDQ_EVENT_MUTEX0_STREAM_EOF);  


		
		dpmgr_path_build_cmdq(pgc->dpmgr_handle, pgc->cmdq_handle_trigger,CMDQ_AFTER_STREAM_EOF, 0);
	}
	else
	{
		
		ret = cmdqRecWait(pgc->cmdq_handle_trigger, CMDQ_SYNC_TOKEN_CONFIG_DIRTY);

		
		
		dpmgr_path_build_cmdq(pgc->dpmgr_handle, pgc->cmdq_handle_trigger,CMDQ_BEFORE_STREAM_SOF, 0);

		
		
		
		ret = cmdqRecClearEventToken(pgc->cmdq_handle_trigger, CMDQ_SYNC_TOKEN_STREAM_EOF);

		
		
		dpmgr_path_trigger(pgc->dpmgr_handle, pgc->cmdq_handle_trigger, CMDQ_ENABLE);
		

		
		
		ret = cmdqRecWait(pgc->cmdq_handle_trigger, CMDQ_EVENT_DISP_RDMA1_EOF);  
		dpmgr_path_build_cmdq(pgc->dpmgr_handle, pgc->cmdq_handle_trigger,CMDQ_WAIT_STREAM_EOF_EVENT, 0);

		
		
		dpmgr_path_build_cmdq(pgc->dpmgr_handle, pgc->cmdq_handle_trigger,CMDQ_CHECK_IDLE_AFTER_STREAM_EOF, 0);
		
		
		dpmgr_path_build_cmdq(pgc->dpmgr_handle, pgc->cmdq_handle_trigger,CMDQ_AFTER_STREAM_EOF, 0);

		
		
		
		

		
		ret = cmdqRecSetEventToken(pgc->cmdq_handle_trigger, CMDQ_SYNC_TOKEN_STREAM_EOF);

		
		BUG_ON(ret < 0);
	}

	
	cmdqRecDumpCommand(pgc->cmdq_handle_trigger);
	DISPCHECK("ext display BUILD cmdq trigger loop finished\n");

	return;
}

static void _cmdq_start_trigger_loop(void)
{
	int ret = 0;
	
	
	ret = cmdqRecStartLoop(pgc->cmdq_handle_trigger);
	if(!ext_disp_is_video_mode())
	{
		
		cmdqCoreSetEvent(CMDQ_SYNC_TOKEN_STREAM_EOF);
		
	}
	
	DISPCHECK("START cmdq trigger loop finished\n");
}

static void _cmdq_stop_trigger_loop(void)
{
	int ret = 0;
	
	
	ret = cmdqRecStopLoop(pgc->cmdq_handle_trigger);
	
	DISPCHECK("ext display STOP cmdq trigger loop finished\n");
}


static void _cmdq_set_config_handle_dirty(void)
{
	if(!ext_disp_is_video_mode())
	{
		
		cmdqRecSetEventToken(pgc->cmdq_handle_config, CMDQ_SYNC_TOKEN_CONFIG_DIRTY);
		
	}
}

static void _cmdq_reset_config_handle(void)
{
	cmdqRecReset(pgc->cmdq_handle_config);
	
}

static void _cmdq_flush_config_handle(int blocking, void *callback, unsigned int userdata)
{
    if(blocking )
    	cmdqRecFlush(pgc->cmdq_handle_config);  
	else
    {
        if(callback)
			cmdqRecFlushAsyncCallback(pgc->cmdq_handle_config, callback, userdata);
		else
			cmdqRecFlushAsync(pgc->cmdq_handle_config);
    }
	
}

static void _cmdq_insert_wait_frame_done_token(void)
{
	if(ext_disp_is_video_mode())
	{
		cmdqRecWaitNoClear(pgc->cmdq_handle_config, dpmgr_path_get_mutex(pgc->dpmgr_handle) + CMDQ_EVENT_MUTEX0_STREAM_EOF);  

	}
	else
	{
		cmdqRecWaitNoClear(pgc->cmdq_handle_config, CMDQ_SYNC_TOKEN_STREAM_EOF);
	}
	
	
}

static int _convert_disp_input_to_rdma(RDMA_CONFIG_STRUCT *dst, ext_disp_input_config* src)
{
	if(src && dst)
	{    		
		dst->inputFormat = src->fmt;		
		dst->address = src->addr;  
		dst->width = src->src_w;
		dst->height = src->src_h;
		dst->pitch = src->src_pitch;

		return 0;
	}
	else
	{
		DISPERR("src(%p) or dst(%p) is null\n", src, dst);
		return -1;
	}
}

static int _convert_disp_input_to_ovl(OVL_CONFIG_STRUCT *dst, ext_disp_input_config* src)
{
	if(src && dst)
	{
		dst->layer = src->layer;
		dst->layer_en = src->layer_en;
		dst->source = src->buffer_source;        
		dst->fmt = src->fmt;
		dst->addr = src->addr;  
		dst->vaddr = src->vaddr;
		dst->src_x = src->src_x;
		dst->src_y = src->src_y;
		dst->src_w = src->src_w;
		dst->src_h = src->src_h;
		dst->src_pitch = src->src_pitch;
		dst->dst_x = src->dst_x;
		dst->dst_y = src->dst_y;
		dst->dst_w = src->dst_w;
		dst->dst_h = src->dst_h;
		dst->keyEn = src->keyEn;
		dst->key = src->key; 
		dst->aen = src->aen; 
		dst->alpha = src->alpha;

        dst->sur_aen = src->sur_aen;
        dst->src_alpha = src->src_alpha;
        dst->dst_alpha = src->dst_alpha;

		dst->isDirty = src->isDirty;

		dst->buff_idx = src->buff_idx;
		dst->identity = src->identity;
		dst->connected_type = src->connected_type;
		dst->security = src->security;
		dst->yuv_range = src->yuv_range;

		return 0;
	}
	else
	{
		DISPERR("src(%p) or dst(%p) is null\n", src, dst);
		return -1;
	}
}


static int _trigger_display_interface(int blocking, void *callback, unsigned int userdata)
{
    
    int i = 0;
	
    bool reg_flush = false;
	if(_should_wait_path_idle())
	{
		dpmgr_wait_event_timeout(pgc->dpmgr_handle, DISP_PATH_EVENT_FRAME_DONE, HZ/2);
	}
	
	if(_should_update_lcm())
	{
		extd_drv_update(pgc->plcm, 0, 0, pgc->plcm->params->width, pgc->plcm->params->height, 0);
	}

	if(_should_start_path())
	{ 
        reg_flush = true;
		dpmgr_path_start(pgc->dpmgr_handle, ext_disp_cmdq_enabled());	
		MMProfileLogEx(ddp_mmp_get_events()->Extd_State, MMProfileFlagPulse, Trigger, 1 );
	}
	
	if(_should_trigger_path())
	{	
		
		dpmgr_path_trigger(pgc->dpmgr_handle, NULL, ext_disp_cmdq_enabled());		
	}
	
	if(_should_set_cmdq_dirty())
	{
		_cmdq_set_config_handle_dirty();
	}
    
	{
	    #if 0
	    if(reg_flush == false)
    	{
        	if(_should_insert_wait_frame_done_token())
        	{
        		_cmdq_insert_wait_frame_done_token();
        	}
        }
        
    	if(_should_flush_cmdq_config_handle())
    	{
    		_cmdq_flush_config_handle(reg_flush);  
    	}

    	if(_should_reset_cmdq_config_handle())
    	{
    		_cmdq_reset_config_handle();
    	}

    	if(reg_flush == true)
    	{
        	if(_should_insert_wait_frame_done_token())
        	{
        		_cmdq_insert_wait_frame_done_token();
        	}
        }

        
        #else

        if(_should_flush_cmdq_config_handle())
    	{    	    
    	    if(reg_flush)
    	    {    	        
                MMProfileLogEx(ddp_mmp_get_events()->Extd_State, MMProfileFlagPulse, Trigger, 2 );
	        }
    		_cmdq_flush_config_handle(blocking, callback, userdata);
    		
    		
    	}
    	
    	if(_should_reset_cmdq_config_handle())
    	{
    		_cmdq_reset_config_handle();
    	}
    	
    	if(_should_insert_wait_frame_done_token())
    	{
    		_cmdq_insert_wait_frame_done_token();
    	}
        #endif
	}

	return 0;
}

static int _trigger_overlay_engine(void)
{
	
	dpmgr_path_trigger(pgc->ovl2mem_path_handle, NULL, ext_disp_use_cmdq);
}

static unsigned int cmdqDdpClockOn(uint64_t engineFlag)
{
    return 0;
}

static unsigned int cmdqDdpClockOff(uint64_t engineFlag)
{
    return 0;
}

static unsigned int cmdqDdpDumpInfo(uint64_t engineFlag,
                        char     *pOutBuf,
                        unsigned int bufSize)
{
	DISPERR("extd cmdq timeout:%llu\n", engineFlag);
    ext_disp_diagnose();
    return 0;
}

static unsigned int cmdqDdpResetEng(uint64_t engineFlag)
{
    return 0;
}


int ext_disp_init(char *lcm_name, unsigned int session)
{
	DISPFUNC();
	EXT_DISP_STATUS ret = EXT_DISP_STATUS_OK;
	DISP_MODULE_ENUM dst_module = 0;
	
	LCM_PARAMS *lcm_param = NULL;
	LCM_INTERFACE_ID lcm_id = LCM_INTERFACE_NOTDEFINED;

	dpmgr_init();

	extd_mutex_init(&(pgc->lock));
	_ext_disp_path_lock();

	pgc->plcm = extd_drv_probe( lcm_name, LCM_INTERFACE_NOTDEFINED);
	if(pgc->plcm == NULL)
	{
		DISPCHECK("disp_lcm_probe returns null\n");
		ret = EXT_DISP_STATUS_ERROR;
		goto done;
	}
	else
	{
		DISPCHECK("disp_lcm_probe SUCCESS\n");
	}


	lcm_param = extd_drv_get_params(pgc->plcm);

	if(lcm_param == NULL)
	{
		DISPERR("get lcm params FAILED\n");
		ret = EXT_DISP_STATUS_ERROR;
		goto done;
	}

	#if 0
	ret = cmdqCoreRegisterCB(CMDQ_GROUP_DISP,	cmdqDdpClockOn,cmdqDdpDumpInfo,cmdqDdpResetEng,cmdqDdpClockOff);
	if(ret)
	{
		DISPERR("cmdqCoreRegisterCB failed, ret=%d \n", ret);
		ret = EXT_DISP_STATUS_ERROR;
		goto done;
	}					 
	#endif
	
	ret = cmdqRecCreate(CMDQ_SCENARIO_MHL_DISP, &(pgc->cmdq_handle_config));
	if(ret)
	{
		DISPCHECK("cmdqRecCreate FAIL, ret=%d \n", ret);
		ret = EXT_DISP_STATUS_ERROR;
		goto done;
	}
	else
	{
		DISPCHECK("cmdqRecCreate SUCCESS, g_cmdq_handle=%p \n", pgc->cmdq_handle_config);
	}

	if(ext_disp_mode == EXTD_DIRECT_LINK_MODE)
	{
		_build_path_direct_link();
		
		DISPCHECK("ext_disp display is DIRECT LINK MODE\n");
	}
	else if(ext_disp_mode == EXTD_DECOUPLE_MODE)
	{
		_build_path_decouple();
		
		DISPCHECK("ext_disp display is DECOUPLE MODE\n");
	}
	else if(ext_disp_mode == EXTD_SINGLE_LAYER_MODE)
	{
		_build_path_single_layer();
		
		DISPCHECK("ext_disp display is SINGLE LAYER MODE\n");
	}
	else if(ext_disp_mode == EXTD_DEBUG_RDMA_DPI_MODE)
	{
		_build_path_debug_rdma_dpi();
		
		DISPCHECK("ext_disp display is DEBUG RDMA to dpi MODE\n");
	}
	else
	{
		DISPCHECK("ext_disp display mode is WRONG\n");
	}

    if(ext_disp_use_cmdq == CMDQ_ENABLE)
	{
    	_cmdq_build_trigger_loop();
    	
    	DISPCHECK("ext_disp display BUILD cmdq trigger loop finished\n");
    	
    	_cmdq_start_trigger_loop();
	}

	pgc->session = session;
	
	DISPCHECK("ext_disp display START cmdq trigger loop finished\n");
	
	dpmgr_path_set_video_mode(pgc->dpmgr_handle, ext_disp_is_video_mode());

	dpmgr_path_init(pgc->dpmgr_handle, CMDQ_DISABLE);
    
    disp_ddp_path_config *data_config = (disp_ddp_path_config *)vmalloc(sizeof(disp_ddp_path_config));	
    if(data_config)
    {
        memset((void*)data_config, 0, sizeof(disp_ddp_path_config));
        memcpy(&(data_config->dispif_config), &(lcm_param), sizeof(LCM_PARAMS));

        data_config->dst_w = lcm_param->width;
        data_config->dst_h = lcm_param->height;
        data_config->dst_dirty = 1;

        ret = dpmgr_path_config(pgc->dpmgr_handle, data_config, CMDQ_DISABLE);
        vfree(data_config);
    }
    else
    {
        DISPCHECK("allocate buffer data_config failed!!!\n");
        ret = EXT_DISP_STATUS_ERROR;
        goto done;
    }
	if(!extd_drv_is_inited(pgc->plcm))
	{
		ret = extd_drv_init(pgc->plcm);
	}

	
	if(ext_disp_is_video_mode())
	{
		
		if(ext_disp_mode == EXTD_DEBUG_RDMA_DPI_MODE)
		    dpmgr_map_event_to_irq(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC, DDP_IRQ_RDMA2_DONE);
		else		
		    dpmgr_map_event_to_irq(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC, DDP_IRQ_RDMA1_DONE);
	}
	
	if(ext_disp_use_cmdq == CMDQ_ENABLE)
	{
		_cmdq_reset_config_handle();
		_cmdq_insert_wait_frame_done_token();
	}
		
	pgc->state = EXTD_INIT;

done:

    

	_ext_disp_path_unlock();

    ext_disp_resume();

    dpmgr_path_stop(pgc->dpmgr_handle, CMDQ_DISABLE);
            
	DISPMSG("ext_disp_init done \n");
	return ret;
}


int ext_disp_deinit(char *lcm_name)
{
    DISPFUNC();    

    if(pgc->state != EXTD_SUSPEND)
        ext_disp_suspend();

    _ext_disp_path_lock();

    if(pgc->state == EXTD_DEINIT)
        goto deinit_exit;
        
    dpmgr_path_deinit(pgc->dpmgr_handle, CMDQ_DISABLE);
    dpmgr_destroy_path(pgc->dpmgr_handle,NULL);

    cmdqRecDestroy(pgc->cmdq_handle_config);
    cmdqRecDestroy(pgc->cmdq_handle_trigger);    

    pgc->state = EXTD_DEINIT;


deinit_exit:    
    _ext_disp_path_unlock();
    is_context_inited = 0;
    
    hdmi_smb_kpd_disable();
    
    DISPMSG("ext_disp_deinit done \n" );
    
}

int ext_disp_wait_for_idle(void)
{	
	EXT_DISP_STATUS ret = EXT_DISP_STATUS_OK;

	DISPFUNC();

	_ext_disp_path_lock();
	
done:
	_ext_disp_path_unlock();
	return ret;
}

int ext_disp_wait_for_dump(void)
{
	
}

int ext_disp_wait_for_vsync(void *config)
{
	disp_session_vsync_config *c = (disp_session_vsync_config *)config;
	int ret = 0;
	ret = dpmgr_wait_event(pgc->dpmgr_handle, DISP_PATH_EVENT_IF_VSYNC);	
    
	if(ret == -2)
	{
		DISPCHECK("vsync for ext display path not enabled yet\n");
		return -1;
	}
	
	c->vsync_ts = get_current_time_us();
	c->vsync_cnt ++;
	
	return ret;
}

int ext_disp_suspend(void)
{
	EXT_DISP_STATUS ret = EXT_DISP_STATUS_OK;

	DISPFUNC();
	    
	_ext_disp_path_lock();
	
	if(pgc->state == EXTD_DEINIT || pgc->state == EXTD_SUSPEND)
	{
		DISPERR("EXTD_DEINIT or EXTD_SUSPEND\n");
		goto done;
	}

    pgc->need_trigger_overlay = 0;
    
	if(dpmgr_path_is_busy(pgc->dpmgr_handle))
	{
	    dpmgr_wait_event_timeout(pgc->dpmgr_handle, DISP_PATH_EVENT_FRAME_DONE, HZ/30);
	}
	    
    if(ext_disp_use_cmdq == CMDQ_ENABLE)
        _cmdq_stop_trigger_loop();

    dpmgr_path_stop(pgc->dpmgr_handle, CMDQ_DISABLE);
    dpmgr_path_power_off(pgc->dpmgr_handle, CMDQ_DISABLE);
    if(dpmgr_path_is_busy(pgc->dpmgr_handle))
    {
        dpmgr_wait_event_timeout(pgc->dpmgr_handle, DISP_PATH_EVENT_FRAME_DONE, HZ/30);

    }

    
    dpmgr_path_reset(pgc->dpmgr_handle, CMDQ_DISABLE);

    #if 0
	{		   
        dpmgr_path_reset(pgc->dpmgr_handle, CMDQ_DISABLE);
        dpmgr_check_status(pgc->dpmgr_handle);	    
    }
    #endif
    
	extd_drv_suspend(pgc->plcm);
	

	pgc->state = EXTD_SUSPEND;
	
	
done:
	_ext_disp_path_unlock();
	
	DISPMSG("ext_disp_suspend done \n");
	return ret;
}

int ext_disp_resume(void)
{
	EXT_DISP_STATUS ret = EXT_DISP_STATUS_OK;
        
	_ext_disp_path_lock();
	
	if(pgc->state < EXTD_INIT)
	{
		DISPERR("EXTD_DEINIT \n");
		goto done;
	}
	
	dpmgr_path_power_on(pgc->dpmgr_handle, CMDQ_DISABLE);

	extd_drv_resume(pgc->plcm);

	

     if(ext_disp_use_cmdq == CMDQ_ENABLE)
    	_cmdq_start_trigger_loop();


	if(dpmgr_path_is_busy(pgc->dpmgr_handle))
	{
		DISPCHECK("stop display path failed, still busy\n");
		ret = -1;
		goto done;
	}

	pgc->state = EXTD_RESUME;
	
done:
	_ext_disp_path_unlock();
	DISPMSG("ext_disp_resume done \n");
	return ret;
}


int ext_disp_trigger(int blocking, void *callback, unsigned int userdata)
{
	int ret = 0;
	
	
	if((is_hdmi_active() == false)|| (pgc->state != EXTD_RESUME) || pgc->need_trigger_overlay < 1)
	{
		DISPMSG("trigger ext display is already sleeped\n");
		MMProfileLogEx(ddp_mmp_get_events()->Extd_ErrorInfo, MMProfileFlagPulse, Trigger, 0 );
		return -1;
	}
	    
	_ext_disp_path_lock();
	
	if(_should_trigger_interface())
	{	
		_trigger_display_interface(blocking, callback, userdata);
	}
	else
	{
		_trigger_overlay_engine();
	}
	
	_ext_disp_path_unlock();
    DISPMSG("ext_disp_trigger done \n");

	return ret;
}

extern disp_ddp_path_config extd_dpi_params;
extern unsigned int hdmi_va;
extern unsigned int hdmi_mva_r;

int ext_disp_config_input(ext_disp_input_config* input)
{
	int ret = 0;
	int i=0;
	int layer =0;
	
	
	disp_ddp_path_config *data_config;	

	
	
	if((is_hdmi_active() == false)|| ext_disp_is_sleepd())
	{
		DISPMSG("ext disp is already sleeped\n");		
		return 0;
	}

	_ext_disp_path_lock();
	data_config = dpmgr_path_get_last_config(pgc->dpmgr_handle);
	data_config->dst_dirty = 0;
	data_config->ovl_dirty = 0;
	data_config->rdma_dirty = 0;
	data_config->wdma_dirty = 0;
	
	if(input->layer_en)
	{
		if(input->vaddr)
		{
			
		}	
		else
		{
			
		}
	}

#ifdef EXTD_DBG_USE_INNER_BUF
    if(input->fmt == eYUY2)    
    {
        
        
        input->layer_en = 1;
        input->addr = hdmi_mva_r ;
        input->vaddr = hdmi_va ;
        input->fmt = eRGB888;   
        input->src_w = 1280;
        input->src_h = 720;
        input->src_x = 0;
        input->src_y = 0;
        input->src_pitch = 1280*3;        
        input->dst_w = 1280;
        input->dst_h = 720;
        input->dst_x = 0;
        input->dst_y = 0;
        input->aen = 0;
        input->alpha = 0xff;
    }
#endif


	
	if(_should_config_ovl_input())
	{
		ret = _convert_disp_input_to_ovl(&(data_config->ovl_config[input->layer]), input);
		data_config->ovl_dirty = 1;
	}
	else
	{
		ret = _convert_disp_input_to_rdma(&(data_config->rdma_config), input);
		data_config->rdma_dirty= 1;
	}

	
	if(_should_wait_path_idle())
	{
		dpmgr_wait_event_timeout(pgc->dpmgr_handle, DISP_PATH_EVENT_FRAME_DONE, HZ/2);
	}

	memcpy(&(data_config->dispif_config), &(extd_dpi_params.dispif_config), sizeof(LCM_PARAMS));
	ret = dpmgr_path_config(pgc->dpmgr_handle, data_config, ext_disp_cmdq_enabled()? pgc->cmdq_handle_config : NULL);

	
	pgc->need_trigger_overlay = 1;
    

	_ext_disp_path_unlock();
	
	
	return ret;
}


int ext_disp_config_input_multiple(ext_disp_input_config* input, int idx)
{
	int ret = 0;
	int i=0;
	int layer =0;
	
	
	disp_ddp_path_config *data_config;	

	if((is_hdmi_active() == false) || (pgc->state != EXTD_RESUME) )
	{
		DISPMSG("config ext disp is already sleeped\n");	
		MMProfileLogEx(ddp_mmp_get_events()->Extd_ErrorInfo, MMProfileFlagPulse, Config, idx );
		return 0;
	}

    _ext_disp_path_lock();

	
	data_config = dpmgr_path_get_last_config(pgc->dpmgr_handle);
	data_config->dst_dirty = 0;
	data_config->ovl_dirty = 0;
	data_config->rdma_dirty = 0;
	data_config->wdma_dirty = 0;

    
	
	if(_should_config_ovl_input())
	{
		for(i = 0;i<HW_OVERLAY_COUNT;i++)
		{	


			if(input[i].dirty)
			{
				dprec_mmp_dump_ovl_layer(&(data_config->ovl_config[input[i].layer]), input[i].layer, 2);
				ret = _convert_disp_input_to_ovl(&(data_config->ovl_config[input[i].layer]), &input[i]);
			}
			data_config->ovl_dirty = 1;	

			
			

		}
	}
	else
	{
		ret = _convert_disp_input_to_rdma(&(data_config->rdma_config), input);
		data_config->rdma_dirty= 1;
	}

	if(_should_wait_path_idle())
	{
		dpmgr_wait_event_timeout(pgc->dpmgr_handle, DISP_PATH_EVENT_FRAME_DONE, HZ/2);
	}

	memcpy(&(data_config->dispif_config), &(extd_dpi_params.dispif_config), sizeof(LCM_PARAMS));
	ret = dpmgr_path_config(pgc->dpmgr_handle, data_config, ext_disp_cmdq_enabled()? pgc->cmdq_handle_config : NULL);

	
	pgc->need_trigger_overlay = 1;

	_ext_disp_path_unlock();
	DISPMSG("config_input_multiple idx %x -w %d, h %d\n", idx ,data_config->ovl_config[0].src_w, data_config->ovl_config[0].src_h);	
	return ret;
}

int ext_disp_is_alive(void)
{
	unsigned int temp = 0;
	DISPFUNC();
	_ext_disp_path_lock();
	temp = pgc->state;
	_ext_disp_path_unlock();
	
	return temp;
}
int ext_disp_is_sleepd(void)
{
	unsigned int temp = 0;
	
	_ext_disp_path_lock();
	temp = !pgc->state;
	_ext_disp_path_unlock();
	
	return temp;
}



int ext_disp_get_width(void)
{
	if(pgc->plcm == NULL)
	{
		DISPERR("lcm handle is null\n");
		return 0;
	}
	
	if(pgc->plcm->params)
	{
		return pgc->plcm->params->width;
	}
	else
	{
		DISPERR("lcm_params is null!\n");
		return 0;
	}
}

int ext_disp_get_height(void)
{
	if(pgc->plcm == NULL)
	{
		DISPERR("lcm handle is null\n");
		return 0;
	}
	
	if(pgc->plcm->params)
	{
		return pgc->plcm->params->height;
	}
	else
	{
		DISPERR("lcm_params is null!\n");
		return 0;
	}
}

int ext_disp_get_bpp(void)
{
	return 32;
}

int ext_disp_get_info(void *info)
{
    return 0;
}

unsigned int ext_disp_get_sess_id()
{
    if(is_context_inited  > 0)
        return pgc->session;
    else
        return 0;
}

int ext_disp_get_pages(void)
{
	return 3;
}

int ext_disp_is_video_mode(void)
{
	
	return extd_drv_is_video_mode(pgc->plcm);
}

int ext_disp_diagnose(void)
{
	int ret = 0;
	if(is_context_inited  > 0)
	{
        DISPCHECK("ext_disp_diagnose, is_context_inited --%d\n", is_context_inited);
        dpmgr_check_status(pgc->dpmgr_handle);
        
    }
    else
    	ddp_dpi_dump(DISP_MODULE_DPI , 0);
	
	return ret;
}
CMDQ_SWITCH ext_disp_cmdq_enabled(void)
{
	return ext_disp_use_cmdq;
}

int ext_disp_switch_cmdq_cpu(CMDQ_SWITCH use_cmdq)
{
	_ext_disp_path_lock();

	ext_disp_use_cmdq = use_cmdq;
	DISPCHECK("display driver use %s to config register now\n", (use_cmdq==CMDQ_ENABLE)?"CMDQ":"CPU");

	_ext_disp_path_unlock();
	return ext_disp_use_cmdq;
}



    


