
#define PROTOCOL_EOS		1
#define PROTOCOL_NIKON		2
#define PROTOCOL_GENERIC	3

#define PIMA_OPERATION_GETDEVICEINFO         0x1001
#define PIMA_OPERATION_OPENSESSION           0x1002
#define PIMA_OPERATION_CLOSESESSION          0x1003

#define EOS_OC_PC_CONNECT 0x9114
#define EOS_OC_CAPTURE 0x910F
#define EOS_OC_PROPERTY_SET 0x9110
#define EOS_OC_PROPERTY_GET 0x9127
#define EOS_OC_EXTENDED_EVENT_INFO_SET 0x9115
#define EOS_OC_EVENT_GET 0x9116

#define EOS_OC_SETUILOCK 0x911B
#define EOS_OC_RESETUILOCK 0x911C
#define EOS_OC_BULBSTART 0x9125
#define EOS_OC_BULBEND 0x9126
#define EOS_OC_REMOTE_RELEASE_ON 0x9128
#define EOS_OC_REMOTE_RELEASE_OFF 0x9129
#define EOS_OC_KeepDeviceOn 0x9003
#define EOS_OC_DepthOfFieldPreview 0x9156
#define EOS_OC_MoveFocus 0x9155

#define EOS_OC_LV_START 0x9133
#define EOS_OC_LV_STOP 0x9134
#define EOS_OC_VIDEO_START 0x9153
#define EOS_OC_VIDEO_STOP 0x9154

#define EOS_DPC_APERTURE 0xD101
#define EOS_DPC_SHUTTER 0xD102
#define EOS_DPC_ISO 0xD103
#define EOS_DPC_MODE 0xD105
#define EOS_DPC_AFMode 0xD108
#define EOS_DPC_LiveView 0xD1B0
#define EOS_DPC_VideoMode 0xD1C2
#define EOS_DPC_LiveViewShow 0xD1B3
#define EOS_DPC_Video 0xD1B8
//4=video start record
//3=live view show / stop record
//0=video stop / live view stop showing
#define EOS_DPC_PhotosRemaining 0xD11B

#define EOS_EC_PROPERTY_CHANGE 0xC189
#define EOS_EC_PROPERTY_VALUES 0xC18A
#define EOS_EC_OBJECT_CREATED 0xC181
#define EOS_EC_WillShutdownSoon 0xC18D


#define NIKON_OC_CAPTURE 0x90C0
#define NIKON_OC_CAPTURE2 0x9207
#define NIKON_OC_EVENT_GET 0x90C7
#define NIKON_OC_CAMERA_READY 0x90C8

#define PTP_OC_CAPTURE 0x100E
#define PTP_OC_PROPERTY_SET 0x1016
#define PTP_OC_PROPERTY_GET 0x1015
#define PTP_OC_PROPERTY_LIST 0x1014
#define PTP_OC_GET_THUMB 0x100A

#define PTP_EC_OBJECT_CREATED 0x4002
#define PTP_EC_PROPERTY_CHANGED 0x4006

#define NIKON_DPC_APERTURE 0x5007
#define NIKON_DPC_SHUTTER 0x500D
#define NIKON_DPC_ISO 0x500F
#define NIKON_DPC_AutofocusMode 0xD161
//#define NIKON_DPC_AutofocusMode 0x500A

#define NIKON_OC_StartLiveView 0x9201
#define NIKON_OC_EndLiveView 0x9202
#define NIKON_OC_MoveFocus 0x9204

#define NIKON_OC_BULBSTART 0x9207
#define NIKON_OC_BULBEND 0x920C

#define PTP_OFC_Text 0x3004
#define PTP_OC_SendObjectInfo 0x100C
#define PTP_OC_SendObject 0x100D

#define PTP_RESPONSE_OK 0x2001
#define PTP_RESPONSE_BUSY 0x2019



