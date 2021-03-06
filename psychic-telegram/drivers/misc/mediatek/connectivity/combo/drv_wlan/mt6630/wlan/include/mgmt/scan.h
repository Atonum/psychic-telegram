




#ifndef _SCAN_H
#define _SCAN_H



/*! Maximum buffer size of SCAN list */
#define SCN_MAX_BUFFER_SIZE                 (CFG_MAX_NUM_BSS_LIST * ALIGN_4(sizeof(BSS_DESC_T)))

#define SCN_RM_POLICY_EXCLUDE_CONNECTED     BIT(0)	
#define SCN_RM_POLICY_TIMEOUT               BIT(1)	
#define SCN_RM_POLICY_OLDEST_HIDDEN         BIT(2)	
#define SCN_RM_POLICY_SMART_WEAKEST         BIT(3)	
#define SCN_RM_POLICY_ENTIRE                BIT(4)	

#define SCN_BSS_DESC_SAME_SSID_THRESHOLD    3	
#if 1
#define SCN_BSS_DESC_REMOVE_TIMEOUT_SEC     30 
#define SCN_BSS_DESC_STALE_SEC				10 
#else
#define SCN_BSS_DESC_REMOVE_TIMEOUT_SEC     5	

#endif


#define SCN_PROBE_DELAY_MSEC                0

#define SCN_ADHOC_BSS_DESC_TIMEOUT_SEC      5	

#define SCN_NLO_NETWORK_CHANNEL_NUM         (4)

#define SCAN_DONE_DIFFERENCE                3


#define SCAN_REQ_SSID_WILDCARD              BIT(0)
#define SCAN_REQ_SSID_P2P_WILDCARD          BIT(1)
#define SCAN_REQ_SSID_SPECIFIED             BIT(2)


#define SCN_SSID_MAX_NUM                    CFG_SCAN_SSID_MAX_NUM
#define SCN_SSID_MATCH_MAX_NUM              CFG_SCAN_SSID_MATCH_MAX_NUM

#if CFG_SUPPORT_AGPS_ASSIST
#define SCN_AGPS_AP_LIST_MAX_NUM					32
#endif

#if CFG_SUPPORT_BATCH_SCAN
#define SCAN_BATCH_REQ_START                BIT(0)
#define SCAN_BATCH_REQ_STOP                 BIT(1)
#define SCAN_BATCH_REQ_RESULT               BIT(2)
#endif

#define SCAN_NLO_CHECK_SSID_ONLY    0x00000001
#define SCAN_NLO_DEFAULT_INTERVAL           30000
typedef enum _ENUM_SCAN_TYPE_T {
	SCAN_TYPE_PASSIVE_SCAN = 0,
	SCAN_TYPE_ACTIVE_SCAN,
	SCAN_TYPE_NUM
} ENUM_SCAN_TYPE_T, *P_ENUM_SCAN_TYPE_T;

typedef enum _ENUM_SCAN_STATE_T {
	SCAN_STATE_IDLE = 0,
	SCAN_STATE_SCANNING,
	SCAN_STATE_NUM
} ENUM_SCAN_STATE_T;


typedef enum _ENUM_FW_SCAN_STATE_T {
    FW_SCAN_STATE_IDLE = 0,					
    FW_SCAN_STATE_SCAN_START,					
    FW_SCAN_STATE_REQ_CHANNEL,					
    FW_SCAN_STATE_SET_CHANNEL,					
    FW_SCAN_STATE_DELAYED_ACTIVE_PROB_REQ,		        
    FW_SCAN_STATE_ACTIVE_PROB_REQ,				
    FW_SCAN_STATE_LISTEN,					
    FW_SCAN_STATE_SCAN_DONE,					
    FW_SCAN_STATE_NLO_START,					
    FW_SCAN_STATE_NLO_HIT_CHECK,				
    FW_SCAN_STATE_NLO_STOP,					       
    FW_SCAN_STATE_BATCH_START,					
    FW_SCAN_STATE_BATCH_CHECK,					
    FW_SCAN_STATE_BATCH_STOP,					
    FW_SCAN_STATE_NUM							
} ENUM_FW_SCAN_STATE_T;


typedef enum _ENUM_SCAN_CHANNEL_T {
	SCAN_CHANNEL_FULL = 0,
	SCAN_CHANNEL_2G4,
	SCAN_CHANNEL_5G,
	SCAN_CHANNEL_P2P_SOCIAL,
	SCAN_CHANNEL_SPECIFIED,
	SCAN_CHANNEL_NUM
} ENUM_SCAN_CHANNEL, *P_ENUM_SCAN_CHANNEL;

typedef struct _MSG_SCN_FSM_T {
	MSG_HDR_T rMsgHdr;	
	UINT_32 u4Dummy;
} MSG_SCN_FSM_T, *P_MSG_SCN_FSM_T;



struct _BSS_DESC_T {
	LINK_ENTRY_T rLinkEntry;

	UINT_8 aucBSSID[MAC_ADDR_LEN];
	UINT_8 aucSrcAddr[MAC_ADDR_LEN];	

	BOOLEAN fgIsConnecting;	
	BOOLEAN fgIsConnected;	

	BOOLEAN fgIsHiddenSSID;	
	UINT_8 ucSSIDLen;
	UINT_8 aucSSID[ELEM_MAX_LEN_SSID];

	OS_SYSTIME rUpdateTime;

	ENUM_BSS_TYPE_T eBSSType;

	UINT_16 u2CapInfo;

	UINT_16 u2BeaconInterval;
	UINT_16 u2ATIMWindow;

	UINT_16 u2OperationalRateSet;
	UINT_16 u2BSSBasicRateSet;
	BOOLEAN fgIsUnknownBssBasicRate;

	BOOLEAN fgIsERPPresent;
	BOOLEAN fgIsHTPresent;
	BOOLEAN fgIsVHTPresent;

	UINT_8 ucPhyTypeSet;	

	UINT_8 ucChannelNum;

	ENUM_CHNL_EXT_T eSco;	
	ENUM_CHANNEL_WIDTH_T eChannelWidth;	
	UINT_8 ucCenterFreqS1;
	UINT_8 ucCenterFreqS2;
	ENUM_BAND_T eBand;

	UINT_8 ucDTIMPeriod;

	BOOLEAN fgIsLargerTSF;	

	UINT_8 ucRCPI;

	UINT_8 ucWmmFlag;	

	UINT_32 u4RsnSelectedGroupCipher;
	UINT_32 u4RsnSelectedPairwiseCipher;
	UINT_32 u4RsnSelectedAKMSuite;

	UINT_16 u2RsnCap;

	RSN_INFO_T rRSNInfo;
	RSN_INFO_T rWPAInfo;
#if 1				
	WAPI_INFO_T rIEWAPI;
	BOOL fgIEWAPI;
#endif
	BOOL fgIERSN;
	BOOL fgIEWPA;

	
	UINT_8 ucEncLevel;

#if CFG_ENABLE_WIFI_DIRECT
	BOOLEAN fgIsP2PPresent;
	BOOLEAN fgIsP2PReport;	
	P_P2P_DEVICE_DESC_T prP2pDesc;

	UINT_8 aucIntendIfAddr[MAC_ADDR_LEN];	
 
 

	LINK_T rP2pDeviceList;


#endif

	BOOLEAN fgIsIEOverflow;	
	UINT_16 u2RawLength;	
	UINT_16 u2IELength;	

	ULARGE_INTEGER u8TimeStamp;	
	UINT_8 aucRawBuf[CFG_RAW_BUFFER_SIZE];
	UINT_8 aucIEBuf[CFG_IE_BUFFER_SIZE];
};


typedef struct _SCAN_PARAM_T {	
	
	ENUM_SCAN_TYPE_T eScanType;

	
	UINT_8 ucBssIndex;

	
	UINT_8 ucSSIDType;
	UINT_8 ucSSIDNum;

	
	UINT_8 ucSpecifiedSSIDLen[SCN_SSID_MAX_NUM];

	
	UINT_8 aucSpecifiedSSID[SCN_SSID_MAX_NUM][ELEM_MAX_LEN_SSID];

#if CFG_ENABLE_WIFI_DIRECT
	BOOLEAN fgFindSpecificDev;	
	UINT_8 aucDiscoverDevAddr[MAC_ADDR_LEN];
	BOOLEAN fgIsDevType;
	P2P_DEVICE_TYPE_T rDiscoverDevType;

	
#endif				

	UINT_16 u2ChannelDwellTime;
	UINT_16 u2TimeoutValue;

	BOOLEAN fgIsObssScan;
	BOOLEAN fgIsScanV2;

	
	UINT_16 u2ProbeDelayTime;

	
	ENUM_SCAN_CHANNEL eScanChannel;
	UINT_8 ucChannelListNum;
	RF_CHANNEL_INFO_T arChnlInfoList[MAXIMUM_OPERATION_CHANNEL_LIST];

	
	UINT_8 ucSeqNum;

	
	UINT_16 u2IELen;
	UINT_8 aucIE[MAX_IE_LENGTH];

} SCAN_PARAM_T, *P_SCAN_PARAM_T;

typedef struct _NLO_PARAM_T {	
	SCAN_PARAM_T rScanParam;

	
	BOOLEAN fgStopAfterIndication;
	UINT_8 ucFastScanIteration;
	UINT_16 u2FastScanPeriod;
	UINT_16 u2SlowScanPeriod;

	
	UINT_8 ucMatchSSIDNum;
	UINT_8 ucMatchSSIDLen[SCN_SSID_MATCH_MAX_NUM];
	UINT_8 aucMatchSSID[SCN_SSID_MATCH_MAX_NUM][ELEM_MAX_LEN_SSID];

	UINT_8 aucCipherAlgo[SCN_SSID_MATCH_MAX_NUM];
	UINT_16 au2AuthAlgo[SCN_SSID_MATCH_MAX_NUM];
	UINT_8 aucChannelHint[SCN_SSID_MATCH_MAX_NUM][SCN_NLO_NETWORK_CHANNEL_NUM];

} NLO_PARAM_T, *P_NLO_PARAM_T;


typedef struct _SCAN_INFO_T {
	ENUM_SCAN_STATE_T eCurrentState;	

	OS_SYSTIME rLastScanCompletedTime;

	SCAN_PARAM_T rScanParam;
	NLO_PARAM_T rNloParam;

	UINT_32 u4NumOfBssDesc;

	UINT_8 aucScanBuffer[SCN_MAX_BUFFER_SIZE];

	LINK_T rBSSDescList;

	LINK_T rFreeBSSDescList;

	LINK_T rPendingMsgList;

	
	BOOLEAN fgIsSparseChannelValid;
	RF_CHANNEL_INFO_T rSparseChannel;

	
	BOOLEAN fgNloScanning;

} SCAN_INFO_T, *P_SCAN_INFO_T;


typedef struct _MSG_SCN_SCAN_REQ_T {
	MSG_HDR_T rMsgHdr;	
	UINT_8 ucSeqNum;
	UINT_8 ucBssIndex;
	ENUM_SCAN_TYPE_T eScanType;
	UINT_8 ucSSIDType;	
	UINT_8 ucSSIDLength;
	UINT_8 aucSSID[PARAM_MAX_LEN_SSID];
	UINT_16 u2ChannelDwellTime;	
	UINT_16 u2TimeoutValue;	
	ENUM_SCAN_CHANNEL eScanChannel;
	UINT_8 ucChannelListNum;
	RF_CHANNEL_INFO_T arChnlInfoList[MAXIMUM_OPERATION_CHANNEL_LIST];
	UINT_16 u2IELen;
	UINT_8 aucIE[MAX_IE_LENGTH];
} MSG_SCN_SCAN_REQ, *P_MSG_SCN_SCAN_REQ;

typedef struct _MSG_SCN_SCAN_REQ_V2_T {
	MSG_HDR_T rMsgHdr;	
	UINT_8 ucSeqNum;
	UINT_8 ucBssIndex;
	ENUM_SCAN_TYPE_T eScanType;
	UINT_8 ucSSIDType;	
	UINT_8 ucSSIDNum;
	P_PARAM_SSID_T prSsid;
	UINT_16 u2ProbeDelay;
	UINT_16 u2ChannelDwellTime;	
	UINT_16 u2TimeoutValue;	
	ENUM_SCAN_CHANNEL eScanChannel;
	UINT_8 ucChannelListNum;
	RF_CHANNEL_INFO_T arChnlInfoList[MAXIMUM_OPERATION_CHANNEL_LIST];
	UINT_16 u2IELen;
	UINT_8 aucIE[MAX_IE_LENGTH];
} MSG_SCN_SCAN_REQ_V2, *P_MSG_SCN_SCAN_REQ_V2;

typedef struct _MSG_SCN_SCAN_CANCEL_T {
	MSG_HDR_T rMsgHdr;	
	UINT_8 ucSeqNum;
	UINT_8 ucBssIndex;
	BOOLEAN fgIsChannelExt;
} MSG_SCN_SCAN_CANCEL, *P_MSG_SCN_SCAN_CANCEL;

typedef struct _tagOFFLOAD_NETWORK {
	UINT_8 aucSsid[ELEM_MAX_LEN_SSID];
	UINT_8 ucSsidLen;
	UINT_8 ucUnicastCipher;	
	UINT_16 u2AuthAlgo;	
	UINT_8 aucChannelList[SCN_NLO_NETWORK_CHANNEL_NUM];
} OFFLOAD_NETWORK, *P_OFFLOAD_NETWORK;

typedef struct _MSG_SCN_NLO_REQ_T {
	MSG_HDR_T rMsgHdr;	
	BOOLEAN fgStopAfterIndication;
	UINT_8 ucSeqNum;
	UINT_8 ucBssIndex;
	UINT_32 u4FastScanPeriod;
	UINT_32 u4FastScanIterations;
	UINT_32 u4SlowScanPeriod;
	UINT_32 u4NumOfEntries;
	OFFLOAD_NETWORK arNetwork[CFG_SCAN_SSID_MAX_NUM];
} MSG_SCN_NLO_REQ, *P_MSG_SCN_NLO_REQ;

typedef struct _MSG_SCN_NLO_CANCEL_T {
	MSG_HDR_T rMsgHdr;	
	UINT_8 ucSeqNum;
	UINT_8 ucBssIndex;
} MSG_SCN_NLO_CANCEL, *P_MSG_SCN_NLO_CANCEL;

typedef enum _ENUM_SCAN_STATUS_T {
	SCAN_STATUS_DONE = 0,
	SCAN_STATUS_CANCELLED,
	SCAN_STATUS_FAIL,
	SCAN_STATUS_BUSY,
	SCAN_STATUS_NUM
} ENUM_SCAN_STATUS, *P_ENUM_SCAN_STATUS;

typedef struct _MSG_SCN_SCAN_DONE_T {
	MSG_HDR_T rMsgHdr;	
	UINT_8 ucSeqNum;
	UINT_8 ucBssIndex;
	ENUM_SCAN_STATUS eScanStatus;
} MSG_SCN_SCAN_DONE, *P_MSG_SCN_SCAN_DONE;

#if CFG_SUPPORT_AGPS_ASSIST
typedef enum {
	AGPS_PHY_A,
	AGPS_PHY_B,
	AGPS_PHY_G,
}AP_PHY_TYPE;

typedef struct _AGPS_AP_INFO_T{
    UINT_8 aucBSSID[MAC_ADDR_LEN]; 
    INT_16 i2ApRssi; 
    UINT_16 u2Channel;   
    AP_PHY_TYPE ePhyType;
} AGPS_AP_INFO_T, *P_AGPS_AP_INFO_T;

typedef struct _AGPS_AP_LIST_T {
	UINT_8 ucNum;
	AGPS_AP_INFO_T arApInfo[SCN_AGPS_AP_LIST_MAX_NUM];
} AGPS_AP_LIST_T, *P_AGPS_AP_LIST_T;
#endif


typedef enum _ENUM_NLO_STATUS_T {
	NLO_STATUS_FOUND = 0,
	NLO_STATUS_CANCELLED,
	NLO_STATUS_FAIL,
	NLO_STATUS_BUSY,
	NLO_STATUS_NUM
} ENUM_NLO_STATUS, *P_ENUM_NLO_STATUS;

typedef struct _MSG_SCN_NLO_DONE_T {
	MSG_HDR_T rMsgHdr;	
	UINT_8 ucSeqNum;
	UINT_8 ucBssIndex;
	ENUM_NLO_STATUS eNloStatus;
} MSG_SCN_NLO_DONE, *P_MSG_SCN_NLO_DONE;




VOID scnInit(IN P_ADAPTER_T prAdapter);

VOID scnUninit(IN P_ADAPTER_T prAdapter);

P_BSS_DESC_T scanSearchBssDescByBssid(IN P_ADAPTER_T prAdapter, IN UINT_8 aucBSSID[]
    );

P_BSS_DESC_T
scanSearchBssDescByBssidAndSsid(IN P_ADAPTER_T prAdapter,
				IN UINT_8 aucBSSID[],
				IN BOOLEAN fgCheckSsid, IN P_PARAM_SSID_T prSsid);

P_BSS_DESC_T scanSearchBssDescByTA(IN P_ADAPTER_T prAdapter, IN UINT_8 aucSrcAddr[]
    );

P_BSS_DESC_T
scanSearchBssDescByTAAndSsid(IN P_ADAPTER_T prAdapter,
			     IN UINT_8 aucSrcAddr[],
			     IN BOOLEAN fgCheckSsid, IN P_PARAM_SSID_T prSsid);


P_BSS_DESC_T
scanSearchExistingBssDesc(IN P_ADAPTER_T prAdapter,
			  IN ENUM_BSS_TYPE_T eBSSType, IN UINT_8 aucBSSID[], IN UINT_8 aucSrcAddr[]
    );

P_BSS_DESC_T
scanSearchExistingBssDescWithSsid(IN P_ADAPTER_T prAdapter,
				  IN ENUM_BSS_TYPE_T eBSSType,
				  IN UINT_8 aucBSSID[],
				  IN UINT_8 aucSrcAddr[],
				  IN BOOLEAN fgCheckSsid, IN P_PARAM_SSID_T prSsid);


P_BSS_DESC_T scanAllocateBssDesc(IN P_ADAPTER_T prAdapter);

VOID scanRemoveBssDescsByPolicy(IN P_ADAPTER_T prAdapter, IN UINT_32 u4RemovePolicy);

VOID scanRemoveBssDescByBssid(IN P_ADAPTER_T prAdapter, IN UINT_8 aucBSSID[]
    );

VOID
scanRemoveBssDescByBandAndNetwork(IN P_ADAPTER_T prAdapter,
				  IN ENUM_BAND_T eBand, IN UINT_8 ucBssIndex);

VOID scanRemoveConnFlagOfBssDescByBssid(IN P_ADAPTER_T prAdapter, IN UINT_8 aucBSSID[]
    );

P_BSS_DESC_T scanAddToBssDesc(IN P_ADAPTER_T prAdapter, IN P_SW_RFB_T prSwRfb);

WLAN_STATUS scanProcessBeaconAndProbeResp(IN P_ADAPTER_T prAdapter, IN P_SW_RFB_T prSWRfb);

VOID
scanBuildProbeReqFrameCommonIEs(IN P_MSDU_INFO_T prMsduInfo,
				IN PUINT_8 pucDesiredSsid,
				IN UINT_32 u4DesiredSsidLen, IN UINT_16 u2SupportedRateSet);

WLAN_STATUS scanSendProbeReqFrames(IN P_ADAPTER_T prAdapter, IN P_SCAN_PARAM_T prScanParam);

VOID scanUpdateBssDescForSearch(IN P_ADAPTER_T prAdapter, IN P_BSS_DESC_T prBssDesc);

P_BSS_DESC_T scanSearchBssDescByPolicy(IN P_ADAPTER_T prAdapter, IN UINT_8 ucBssIndex);

WLAN_STATUS
scanAddScanResult(IN P_ADAPTER_T prAdapter, IN P_BSS_DESC_T prBssDesc, IN P_SW_RFB_T prSwRfb);

VOID
scanReportBss2Cfg80211(IN P_ADAPTER_T prAdapter,
		       IN ENUM_BSS_TYPE_T eBSSType, IN P_BSS_DESC_T SpecificprBssDesc);
VOID scnFsmSteps(IN P_ADAPTER_T prAdapter, IN ENUM_SCAN_STATE_T eNextState);

VOID scnSendScanReq(IN P_ADAPTER_T prAdapter);

VOID scnSendScanReqV2(IN P_ADAPTER_T prAdapter);

VOID scnEventScanDone(IN P_ADAPTER_T prAdapter, IN P_EVENT_SCAN_DONE prScanDone,  BOOLEAN fgIsNewVersion);

VOID scnEventNloDone(IN P_ADAPTER_T prAdapter, IN P_EVENT_NLO_DONE_T prNloDone);

VOID scnFsmMsgStart(IN P_ADAPTER_T prAdapter, IN P_MSG_HDR_T prMsgHdr);

VOID scnFsmMsgAbort(IN P_ADAPTER_T prAdapter, IN P_MSG_HDR_T prMsgHdr);

VOID scnFsmHandleScanMsg(IN P_ADAPTER_T prAdapter, IN P_MSG_SCN_SCAN_REQ prScanReqMsg);

VOID scnFsmHandleScanMsgV2(IN P_ADAPTER_T prAdapter, IN P_MSG_SCN_SCAN_REQ_V2 prScanReqMsg);

VOID scnFsmRemovePendingMsg(IN P_ADAPTER_T prAdapter, IN UINT_8 ucSeqNum, IN UINT_8 ucBssIndex);

VOID scnFsmNloMsgStart(IN P_ADAPTER_T prAdapter, IN P_MSG_HDR_T prMsgHdr);

VOID scnFsmNloMsgAbort(IN P_ADAPTER_T prAdapter, IN P_MSG_HDR_T prMsgHdr);

VOID scnFsmHandleNloMsg(IN P_ADAPTER_T prAdapter, IN P_MSG_SCN_NLO_REQ prNloReqMsg);

VOID
scnFsmGenerateScanDoneMsg(IN P_ADAPTER_T prAdapter,
			  IN UINT_8 ucSeqNum,
			  IN UINT_8 ucBssIndex, IN ENUM_SCAN_STATUS eScanStatus);

BOOLEAN
scnQuerySparseChannel(IN P_ADAPTER_T prAdapter,
		      P_ENUM_BAND_T prSparseBand, PUINT_8 pucSparseChannel);


BOOLEAN
scnFsmSchedScanRequest(IN P_ADAPTER_T prAdapter,
		       IN UINT_8 ucSsidNum,
		       IN P_PARAM_SSID_T prSsid,
		       IN UINT_32 u4IeLength, IN PUINT_8 pucIe, IN UINT_16 u2Interval);

BOOLEAN scnFsmSchedScanStopRequest(IN P_ADAPTER_T prAdapter);

#if CFG_SUPPORT_PASSPOINT
P_BSS_DESC_T
scanSearchBssDescByBssidAndLatestUpdateTime(IN P_ADAPTER_T prAdapter, IN UINT_8 aucBSSID[]
    );
#endif				

#if CFG_SUPPORT_AGPS_ASSIST
VOID scanReportScanResultToAgps(P_ADAPTER_T prAdapter);
#endif

#endif				
