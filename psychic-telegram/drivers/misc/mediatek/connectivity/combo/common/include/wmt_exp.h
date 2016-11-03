


#ifndef _WMT_EXP_H_
#define _WMT_EXP_H_

#include <mach/mtk_wcn_cmb_stub.h>
#include "wmt_stp_exp.h"
#include "osal.h"


#if 1				
#ifndef DFT_TAG
#define DFT_TAG         "[WMT-DFT]"
#endif

#define WMT_LOUD_FUNC(fmt, arg...)   if (gWmtDbgLvl >= WMT_LOG_LOUD) { osal_dbg_print(DFT_TAG "[L]%s:"  fmt, __FUNCTION__ ,##arg);}
#define WMT_INFO_FUNC(fmt, arg...)   if (gWmtDbgLvl >= WMT_LOG_INFO) { osal_info_print(DFT_TAG "[I]%s:"  fmt, __FUNCTION__ ,##arg);}
#define WMT_WARN_FUNC(fmt, arg...)   if (gWmtDbgLvl >= WMT_LOG_WARN) { osal_warn_print(DFT_TAG "[W]%s:"  fmt, __FUNCTION__ ,##arg);}
#define WMT_ERR_FUNC(fmt, arg...)    if (gWmtDbgLvl >= WMT_LOG_ERR) { osal_err_print(DFT_TAG "[E]%s(%d):"  fmt, __FUNCTION__ , __LINE__, ##arg);}
#define WMT_DBG_FUNC(fmt, arg...)    if (gWmtDbgLvl >= WMT_LOG_DBG) { osal_dbg_print(DFT_TAG "[D]%s:"  fmt, __FUNCTION__ ,##arg);}
#define WMT_TRC_FUNC(f)              if (gWmtDbgLvl >= WMT_LOG_DBG) { osal_dbg_print(DFT_TAG "<%s> <%d>\n", __FUNCTION__, __LINE__);}
#endif

#if 1				
extern UINT32 gWmtDbgLvl;
#endif

#if 1				
#define WMT_LOG_LOUD    4
#define WMT_LOG_DBG     3
#define WMT_LOG_INFO    2
#define WMT_LOG_WARN    1
#define WMT_LOG_ERR     0
#endif

#define CFG_WMT_PS_SUPPORT 1	
#ifndef MTK_WCN_WMT_STP_EXP_SYMBOL_ABSTRACT

typedef enum _ENUM_WMTDRV_TYPE_T {
	WMTDRV_TYPE_BT = 0,
	WMTDRV_TYPE_FM = 1,
	WMTDRV_TYPE_GPS = 2,
	WMTDRV_TYPE_WIFI = 3,
	WMTDRV_TYPE_WMT = 4,
	WMTDRV_TYPE_ANT = 5,
	WMTDRV_TYPE_STP = 6,
	WMTDRV_TYPE_SDIO1 = 7,
	WMTDRV_TYPE_SDIO2 = 8,
	WMTDRV_TYPE_LPBK = 9,
	WMTDRV_TYPE_COREDUMP = 10,
#if MTK_WCN_CMB_FOR_SDIO_1V_AUTOK
    WMTDRV_TYPE_AUTOK = 11,
#endif
	WMTDRV_TYPE_MAX
} ENUM_WMTDRV_TYPE_T, *P_ENUM_WMTDRV_TYPE_T;

typedef enum _ENUM_WMTHWVER_TYPE_T {
	WMTHWVER_E1 = 0x0,
	WMTHWVER_E2 = 0x1,
	WMTHWVER_E3 = 0x2,
	WMTHWVER_E4 = 0x3,
	WMTHWVER_E5 = 0x4,
	WMTHWVER_E6 = 0x5,
	WMTHWVER_E7 = 0x6,
	WMTHWVER_MAX,
	WMTHWVER_INVALID = 0xff
} ENUM_WMTHWVER_TYPE_T, *P_ENUM_WMTHWVER_TYPE_T;

typedef enum _ENUM_WMTDSNS_TYPE_T {
	WMTDSNS_FM_DISABLE = 0,
	WMTDSNS_FM_ENABLE = 1,
	WMTDSNS_FM_GPS_DISABLE = 2,
	WMTDSNS_FM_GPS_ENABLE = 3,
	WMTDSNS_MAX
} ENUM_WMTDSNS_TYPE_T, *P_ENUM_WMTDSNS_TYPE_T;

typedef enum _ENUM_WMTTHERM_TYPE_T {
	WMTTHERM_ZERO = 0,
	WMTTHERM_ENABLE = WMTTHERM_ZERO + 1,
	WMTTHERM_READ = WMTTHERM_ENABLE + 1,
	WMTTHERM_DISABLE = WMTTHERM_READ + 1,
	WMTTHERM_MAX
} ENUM_WMTTHERM_TYPE_T, *P_ENUM_WMTTHERM_TYPE_T;

typedef enum _ENUM_WMTMSG_TYPE_T {
	WMTMSG_TYPE_POWER_ON = 0,
	WMTMSG_TYPE_POWER_OFF = 1,
	WMTMSG_TYPE_RESET = 2,
	WMTMSG_TYPE_STP_RDY = 3,
	WMTMSG_TYPE_HW_FUNC_ON = 4,
	WMTMSG_TYPE_MAX
} ENUM_WMTMSG_TYPE_T, *P_ENUM_WMTMSG_TYPE_T;

typedef VOID (*PF_WMT_CB) (ENUM_WMTDRV_TYPE_T,	
			   ENUM_WMTDRV_TYPE_T,	
			   ENUM_WMTMSG_TYPE_T,	
			   PVOID,	
			   UINT32	
    );

typedef enum _SDIO_PS_OP {
	OWN_SET = 0,
	OWN_CLR = 1,
	OWN_STATE = 2,
} SDIO_PS_OP;


typedef INT32(*PF_WMT_SDIO_PSOP) (SDIO_PS_OP);

typedef enum _ENUM_WMTCHIN_TYPE_T {
	WMTCHIN_CHIPID = 0x0,
	WMTCHIN_HWVER = WMTCHIN_CHIPID + 1,
	WMTCHIN_MAPPINGHWVER = WMTCHIN_HWVER + 1,
	WMTCHIN_FWVER = WMTCHIN_MAPPINGHWVER + 1,
	WMTCHIN_MAX,

} ENUM_WMT_CHIPINFO_TYPE_T, *P_ENUM_WMT_CHIPINFO_TYPE_T;


#endif


typedef enum _ENUM_WMTRSTMSG_TYPE_T {
	WMTRSTMSG_RESET_START = 0x0,
	WMTRSTMSG_RESET_END = 0x1,
    WMTRSTMSG_RESET_END_FAIL = 0x2,
	WMTRSTMSG_RESET_MAX,
	WMTRSTMSG_RESET_INVALID = 0xff
} ENUM_WMTRSTMSG_TYPE_T, *P_ENUM_WMTRSTMSG_TYPE_T;

#if 1				
typedef enum {
	WMT_SDIO_SLOT_INVALID = 0,
	WMT_SDIO_SLOT_SDIO1 = 1,	
	WMT_SDIO_SLOT_SDIO2 = 2,
	WMT_SDIO_SLOT_MAX
} WMT_SDIO_SLOT_NUM;

typedef enum {
	WMT_SDIO_FUNC_STP = 0,
	WMT_SDIO_FUNC_WIFI = 1,
	WMT_SDIO_FUNC_MAX
} WMT_SDIO_FUNC_TYPE;
#endif


typedef enum _ENUM_WMT_ANT_RAM_CTRL_T {
	WMT_ANT_RAM_GET_STATUS = 0,
	WMT_ANT_RAM_DOWNLOAD = WMT_ANT_RAM_GET_STATUS + 1,
	WMT_ANT_RAM_CTRL_MAX
} ENUM_WMT_ANT_RAM_CTRL, *P_ENUM_WMT_ANT_RAM_CTRL;

typedef enum _ENUM_WMT_ANT_RAM_SEQ_T {
	WMT_ANT_RAM_START_PKT = 1,
	WMT_ANT_RAM_CONTINUE_PKT = WMT_ANT_RAM_START_PKT + 1,
	WMT_ANT_RAM_END_PKT = WMT_ANT_RAM_CONTINUE_PKT + 1,
	WMT_ANT_RAM_SEQ_MAX
} ENUM_WMT_ANT_RAM_SEQ, *P_ENUM_WMT_ANT_RAM_SEQ;


typedef enum _ENUM_WMT_ANT_RAM_STATUS_T {
	WMT_ANT_RAM_NOT_EXIST = 0,
	WMT_ANT_RAM_EXIST = WMT_ANT_RAM_NOT_EXIST + 1,
	WMT_ANT_RAM_DOWN_OK = WMT_ANT_RAM_EXIST + 1,
	WMT_ANT_RAM_DOWN_FAIL = WMT_ANT_RAM_DOWN_OK + 1,
	WMT_ANT_RAM_PARA_ERR = WMT_ANT_RAM_DOWN_FAIL + 1,
	WMT_ANT_RAM_OP_ERR = WMT_ANT_RAM_PARA_ERR + 1,
	WMT_ANT_RAM_MAX
} ENUM_WMT_ANT_RAM_STATUS, *P_ENUM_WMT_ANT_RAM_STATUS;









#ifndef MTK_WCN_WMT_STP_EXP_SYMBOL_ABSTRACT
#define WMT_EXP_HID_API_EXPORT 0

extern MTK_WCN_BOOL mtk_wcn_wmt_func_off(ENUM_WMTDRV_TYPE_T type);

extern MTK_WCN_BOOL mtk_wcn_wmt_func_on(ENUM_WMTDRV_TYPE_T type);

extern MTK_WCN_BOOL mtk_wcn_wmt_dsns_ctrl(ENUM_WMTDSNS_TYPE_T eType);

extern MTK_WCN_BOOL mtk_wcn_wmt_assert(ENUM_WMTDRV_TYPE_T type, UINT32 reason);

extern INT32 mtk_wcn_wmt_msgcb_reg(ENUM_WMTDRV_TYPE_T eType, PF_WMT_CB pCb);

extern INT32 mtk_wcn_wmt_msgcb_unreg(ENUM_WMTDRV_TYPE_T eType);

extern INT32 mtk_wcn_stp_wmt_sdio_op_reg(PF_WMT_SDIO_PSOP own_cb);

extern INT32 mtk_wcn_stp_wmt_sdio_host_awake(VOID);
extern INT8 mtk_wcn_wmt_therm_ctrl(ENUM_WMTTHERM_TYPE_T eType);

extern ENUM_WMTHWVER_TYPE_T mtk_wcn_wmt_hwver_get(VOID);

extern UINT32 mtk_wcn_wmt_ic_info_get(ENUM_WMT_CHIPINFO_TYPE_T type);


extern INT32 mtk_wcn_wmt_chipid_query(VOID);

extern INT32 mtk_wcn_wmt_psm_ctrl(MTK_WCN_BOOL flag);

#else
#define WMT_EXP_HID_API_EXPORT 1

#endif

extern INT32 wmt_lib_set_aif(CMB_STUB_AIF_X aif, MTK_WCN_BOOL share);	
extern VOID wmt_lib_ps_irq_cb(VOID);

extern ENUM_WMT_ANT_RAM_STATUS mtk_wcn_wmt_ant_ram_ctrl(ENUM_WMT_ANT_RAM_CTRL ctrlId, PUINT8 pBuf,
							UINT32 length, ENUM_WMT_ANT_RAM_SEQ seq);

extern MTK_WCN_BOOL mtk_wcn_wmt_do_reset(ENUM_WMTDRV_TYPE_T type);

#ifdef MTK_WCN_WMT_STP_EXP_SYMBOL_ABSTRACT
extern VOID mtk_wcn_wmt_exp_init(VOID);
extern VOID mtk_wcn_wmt_exp_deinit(VOID);
#endif

#endif				
