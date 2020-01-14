#include "hydrologytask.h"
#include "Sampler.h"
#include "adc.h"
#include "common.h"
#include "main.h"
#include "math.h"
#include "memoryleakcheck.h"
#include "msp430common.h"
#include "packet.h"
#include "rtc.h"
#include "stdint.h"
#include "store.h"
#include <stdio.h>

#include "FreeRTOS.h"
#include "communication_opr.h"
#include "gprs.h"
#include "hydrologycommand.h"
#include "message.h"
#include "semphr.h"
#include "string.h"
#include "task.h"
#include "timer.h"
#include "uart1.h"
#include "uart3.h"
#include "uart_config.h"
#include <stdbool.h>

extern int		 UserElementCount;
extern int		 RS485RegisterCount;
extern int		 IsDebug;
extern int		 DataPacketLen;
extern hydrologyElement  inputPara[ MAX_ELEMENT ];
extern hydrologyElement  outputPara[ MAX_ELEMENT ];
uint16_t		 time_10min = 0, time_5min = 0, time_1min = 1, time_1s = 0;
extern SemaphoreHandle_t GPRS_Lock;

void HydrologyTimeBase() {
	time_1min++;
	time_5min++;
	time_10min++;
}

void convertSampleTimetoHydrology(char* src, char* dst) {
	dst[ 0 ] = _DECtoBCD(src[ 0 ]);
	dst[ 1 ] = _DECtoBCD(src[ 1 ]);
	dst[ 2 ] = _DECtoBCD(src[ 2 ]);
	dst[ 3 ] = _DECtoBCD(src[ 3 ]);
	dst[ 4 ] = _DECtoBCD(src[ 4 ]);
}

void convertSendTimetoHydrology(char* src, char* dst) {
	dst[ 0 ] = _DECtoBCD(src[ 0 ]);
	dst[ 1 ] = _DECtoBCD(src[ 1 ]);
	dst[ 2 ] = _DECtoBCD(src[ 2 ]);
	dst[ 3 ] = _DECtoBCD(src[ 3 ]);
	dst[ 4 ] = _DECtoBCD(src[ 4 ]);
	dst[ 5 ] = _DECtoBCD(src[ 5 ]);
}

float ConvertAnalog(int v, int range) {
	float tmp;

	// tmp = v / (4096.0) * range;
	tmp = (v - 4096.0) / (4096.0) * range;
	return tmp;
}

void ADC_Element(char* value, int index) {
	// int range[5] = {1,20,100,5000,4000};     //模拟量范围
	int   range[ 16 ] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
	float floatvalue  = 0;

	// floatvalue = ConvertAnalog(A[index+1],range[index]);
	floatvalue = ConvertAnalog(A[ index ], range[ index ]); /*++++++++++++++++*/
	memcpy(value, ( char* )(&floatvalue), 4);
}

char value[ 4 ] = { 0, 0, 0, 0 };

void HydrologyUpdateElementTable() {
	int  i     = 0;
	char user  = 0;
	char rs485 = 0;

	Hydrology_ReadStoreInfo(HYDROLOGY_USER_ElEMENT_COUNT, &user,
				HYDROLOGY_USER_ElEMENT_COUNT_LEN);
	Hydrology_ReadStoreInfo(HYDROLOGY_RS485_REGISTER_COUNT, &rs485,
				HYDROLOGY_RS485_REGISTER_COUNT_LEN);
	UserElementCount   = ( int )user;
	RS485RegisterCount = ( int )rs485;
	TraceInt4(UserElementCount, 1);
	TraceInt4(RS485RegisterCount, 1);
	for (i = 0; i < UserElementCount; i++) {

		Hydrology_ReadStoreInfo(HYDROLOGY_ELEMENT1_ID + i * HYDROLOGY_ELEMENT_ID_LEN,
					&Element_table[ i ].ID, HYDROLOGY_ELEMENT_ID_LEN);
		Hydrology_ReadStoreInfo(HYDROLOGY_ELEMENT1_TYPE + i * HYDROLOGY_ELEMENT_TYPE_LEN,
					&Element_table[ i ].type, HYDROLOGY_ELEMENT_TYPE_LEN);
		Hydrology_ReadStoreInfo(HYDROLOGY_ELEMENT1_MODE + i * HYDROLOGY_ELEMENT_MODE_LEN,
					&Element_table[ i ].Mode, HYDROLOGY_ELEMENT_MODE_LEN);
		Hydrology_ReadStoreInfo(HYDROLOGY_ELEMENT1_CHANNEL
						+ i * HYDROLOGY_ELEMENT_CHANNEL_LEN,
					&Element_table[ i ].Channel, HYDROLOGY_ELEMENT_CHANNEL_LEN);
		// Hydrology_ReadStoreInfo(HYDROLOGY_SWITCH1,temp_value,HYDROLOGY_SWITCH_LEN);
		getElementDd(
			Element_table[ i ].ID, &Element_table[ i ].D,
			&Element_table[ i ].d);  // D,d存了取还是直接取，可以先测直接取的，看可行否
	}
	Element_table[ i ].ID      = NULL;
	Element_table[ i ].type    = NULL;
	Element_table[ i ].D       = NULL;
	Element_table[ i ].d       = NULL;
	Element_table[ i ].Mode    = NULL;
	Element_table[ i ].Channel = NULL;
}
char s_isr_count_flag = 0;
void HydrologyDataPacketInit() {
	char packet_len = 0;
	int  i		= 0;
	packet_len += HYDROLOGY_DATA_SEND_FLAG_LEN;
	packet_len += HYDROLOGY_DATA_TIME_LEN;
	while (Element_table[ i ].ID != 0) {
		mypvPortMallocElement(Element_table[ i ].ID, Element_table[ i ].D,
				      Element_table[ i ].d, &inputPara[ i ]);
		packet_len += inputPara[ i ].num;
		i++;
	}
	DataPacketLen = packet_len;
	Hydrology_WriteStoreInfo(HYDROLOGY_DATA_PACKET_LEN, &packet_len,
				 HYDROLOGY_DATA_PACKET_LEN_LEN);
	Hydrology_ReadStoreInfo(HYDROLOGY_ISR_COUNT_FLAG, &s_isr_count_flag,
				HYDROLOGY_ISR_COUNT_FLAG_LEN);
	/*初始化的时候更新下时间*/
	lock_communication_dev();
	communication_module_t* comm_dev = get_communication_dev();
	rtc_time_t		rtc_time = comm_dev->get_real_time();
	unlock_communication_dev();
	System_Delayms(50);
	if (rtc_time.year == 0) {
		printf("update rtc through gprs module failed \r\n");
		return;
	}
	printf("update rtc time, %d/%d/%d %d:%d:%d \r\n", rtc_time.year, rtc_time.month,
	       rtc_time.date, rtc_time.hour, rtc_time.min, rtc_time.sec);
	_RTC_SetTime(( char )rtc_time.sec, ( char )rtc_time.min, ( char )rtc_time.hour,
		     ( char )rtc_time.date, ( char )rtc_time.month, 1, ( char )rtc_time.year, 0);
}

int HydrologySample(char* _saveTime) {

	int	  i = 0, j = 0;
	int	  adc_i = 0, isr_i = 0;
	int	  interval = 0;
	int	  a = 0, b = 0, c = 0;
	volatile int rs485_i		 = 0;
	long	 isr_count		 = 0;
	char	 io_i		 = 0;
	char	 isr_count_temp[ 5 ] = { 0 };
	char	 sampleinterval[ 2 ];
	char	 _temp_sampertime[ 6 ] = { 0 };
	static char  sampletime[ 6 ]       = { 0 };

	Hydrology_ReadStoreInfo(HYDROLOGY_SAMPLE_INTERVAL, sampleinterval,
				HYDROLOGY_SAMPLE_INTERVAL_LEN);  //??????????
	sampleinterval[ 0 ] = _BCDtoDEC(sampleinterval[ 0 ]);
	sampleinterval[ 1 ] = _BCDtoDEC(sampleinterval[ 1 ]);
	interval	    = sampleinterval[ 1 ] + sampleinterval[ 0 ] * 100;

	Utility_Strncpy(sampletime, _saveTime, 6);

	int tmp = sampletime[ 4 ] % (interval / 60);
	if (tmp != 0) {
		printf("Not Sample Time! now time is: %d/%d/%d  %d:%d:%d \r\n", sampletime[ 0 ],
		       sampletime[ 1 ], sampletime[ 2 ], sampletime[ 3 ], sampletime[ 4 ],
		       sampletime[ 5 ]);
		return -1;
	}

	TraceMsg(" Start Sample:   ", 0);
	UART1_Open_9600(UART1_U485_TYPE);
	while (Element_table[ j ].ID != 0) {
		if (Element_table[ j ].Mode == ADC) {
			ADC_Sample();
			break;
		}
		j++;
	}

	while (Element_table[ i ].ID != 0) {
		memset(value, 0, sizeof(value));
		switch (Element_table[ i ].Mode) {
		case ADC: {

			adc_i = ( int )Element_table[ i ].Channel;
			ADC_Element(value, adc_i);
			// adc_i++;
			break;
		}
		case ISR_COUNT: {
			isr_i = ( int )Element_table[ i ].Channel;
			Hydrology_ReadStoreInfo(HYDROLOGY_ISR_COUNT1
							+ (isr_i - 1) * HYDROLOGY_ISR_COUNT_LEN,
						isr_count_temp, HYDROLOGY_ISR_COUNT_LEN);
			isr_count = (isr_count_temp[ 4 ] * 0x100000000)
				    + (isr_count_temp[ 3 ] * 0x1000000)
				    + (isr_count_temp[ 2 ] * 0x10000)
				    + (isr_count_temp[ 1 ] * 0x100) + (isr_count_temp[ 0 ]);
			memcpy(value, ( char* )(&isr_count), 4);
			// isr_i++;
			break;
		}
		case IO_STATUS: {
			io_i = ( int )Element_table[ i ].Channel;
			Hydrology_ReadIO_STATUS(value, io_i);
			// io_i++;
			break;
		}
		case RS485: {
			// rs485_i = (int)Element_table[i].Channel;
			// //??????????????????485?????index???????channel??index
			Hydrology_ReadRS485(value, rs485_i);
			rs485_i++;
			break;
		}
		}

		switch (Element_table[ i ].type) {
		case ANALOG: {
			convertSampleTimetoHydrology(g_rtc_nowTime, _temp_sampertime);
			Hydrology_SetObservationTime(Element_table[ i ].ID, _temp_sampertime, i);
			Hydrology_WriteStoreInfo(HYDROLOGY_ANALOG1 + a * HYDROLOGY_ANALOG_LEN,
						 value, HYDROLOGY_ANALOG_LEN);
			a++;
			break;
		}
		case PULSE: {
			convertSampleTimetoHydrology(g_rtc_nowTime, _temp_sampertime);
			Hydrology_SetObservationTime(Element_table[ i ].ID, _temp_sampertime, i);
			Hydrology_WriteStoreInfo(HYDROLOGY_PULSE1 + b * HYDROLOGY_PULSE_LEN, value,
						 HYDROLOGY_PULSE_LEN);
			b++;
			break;
		}
		case SWITCH: {
			convertSampleTimetoHydrology(g_rtc_nowTime, _temp_sampertime);
			Hydrology_SetObservationTime(Element_table[ i ].ID, _temp_sampertime, i);
			Hydrology_WriteStoreInfo(HYDROLOGY_SWITCH1 + c * HYDROLOGY_SWITCH_LEN,
						 value, HYDROLOGY_SWITCH_LEN);
			c++;
			break;
		}
		}
		i++;
	}
	UART3_Open(UART3_CONSOLE_TYPE);
	UART1_Open(UART1_BT_TYPE);
	TraceMsg("Sample Done!  ", 0);

	return 0;
}

int HydrologyOnline() {
	// if (time_10min >= 1)
	// hydrologyProcessSend(LinkMaintenance);

	return 0;
}

int HydrologyOffline() {
	GPRS_Close_TCP_Link();
	GPRS_Close_GSM();

	return 0;
}

static int arrived_store_time(char *now_time) {
	char	storeinterval;
	static char storetime[ 6 ] = { 0, 0, 0, 0, 0, 0 };
	Hydrology_ReadStoreInfo(HYDROLOGY_WATERLEVEL_STORE_INTERVAL, &storeinterval,
				HYDROLOGY_WATERLEVEL_STORE_INTERVAL_LEN);  // ly
	storeinterval = _BCDtoDEC(storeinterval);
	Utility_Strncpy(storetime, now_time, 6);
	int tmp = storetime[ 4 ] % (storeinterval);
	if (tmp != 0) {
		return FALSE;
	}
	else
	{
		return TRUE;
	}
	
}

int HydrologySaveData(char *rtc_nowTime, char funcode)  // char *_saveTime
{
	int   i = 0, acount = 0, pocunt = 0;
	float floatvalue	= 0;
	long  intvalue1		= 0;
	int   intvalue2		= 0;
	int   type		= 0;
	int   cnt		= 0;
	int   _effect_count     = 0;
	char  switch_value[ 4 ] = { 0 };

	Hydrology_ReadDataPacketCount(&_effect_count);  //初始读取内存剩余未发送数据包数量

	type = hydrologyJudgeType(funcode);

	if (arrived_store_time(rtc_nowTime) == FALSE) {
		return FAILED;
	}

	TraceMsg("Start Store:   ", 0);
	if (type == 1) {
		while (Element_table[ i ].ID != 0) {
			switch (Element_table[ i ].type) {
			case ANALOG: {
				Hydrology_ReadAnalog(&floatvalue, acount++);
				mypvPortMallocElement(
					Element_table[ i ].ID, Element_table[ i ].D,
					Element_table[ i ].d,
					&inputPara[ i ]);  //获得id ，num，开辟value的空间
				converToHexElement(( double )floatvalue, Element_table[ i ].D,
						   Element_table[ i ].d, inputPara[ i ].value);
				break;
			}
			case PULSE: {
				Hydrology_ReadPulse(&intvalue1, pocunt++);
				mypvPortMallocElement(Element_table[ i ].ID, Element_table[ i ].D,
						      Element_table[ i ].d, &inputPara[ i ]);
				converToHexElement(( double )intvalue1, Element_table[ i ].D,
						   Element_table[ i ].d, inputPara[ i ].value);
				break;
			}
			case SWITCH: {
				// Hydrology_ReadSwitch(&intvalue2);
				Hydrology_ReadSwitch(switch_value);
				mypvPortMallocElement(Element_table[ i ].ID, Element_table[ i ].D,
						      Element_table[ i ].d, &inputPara[ i ]);
				// converToHexElement((double)intvalue2,Element_table[i].D,Element_table[i].d,inputPara[i].value);
				// memcpy(inputPara[i].value,switch_value,4);
				inputPara[ i ].value[ 0 ] = switch_value[ 3 ];
				inputPara[ i ].value[ 1 ] = switch_value[ 2 ];
				inputPara[ i ].value[ 2 ] = switch_value[ 1 ];
				inputPara[ i ].value[ 3 ] = switch_value[ 0 ];

				break;
			}
			case STORE: {
				inputPara[ i ].guide[ 0 ] = Element_table[ i ].ID;
				inputPara[ i ].guide[ 1 ] = Element_table[ i ].ID;
				inputPara[ i ].num	= SinglePacketSize;
				break;
			}
			default:
				break;
			}
			i++;
			cnt++;
		}
	}
	char _data[ HYDROLOGY_DATA_ITEM_LEN ] = { 0 };  //数据条为130个字节
	char observationtime[ 5 ];
	int  len   = 0;
	_data[ 0 ] = 0x00;  // 未发送标记 记为0x00
	Hydrology_ReadObservationTime(Element_table[ 0 ].ID, observationtime, 0);  // or save_time
	memcpy(&_data[ 1 ], observationtime, HYDROLOGY_OBSERVATION_TIME_LEN);
	len += 6;
	for (i = 0; i < cnt; i++) {
		memcpy(&_data[ len ], inputPara[ i ].value, inputPara[ i ].num);
		len += inputPara[ i ].num;
	}
	++_effect_count;  //存一条就加1
	Hydrology_SetDataPacketCount(_effect_count);

	if (Store_WriteDataItemAuto(_data) < 0) {
		return -1;
	}
	TraceMsg("Save Data Success", 1);
	return 0;
}
int HydrologyInstantWaterLevel(char* _saveTime)  //检查发送时间，判断上下标，组报文发送
{

	static char endtime[ 6 ] = { 0, 0, 0, 0, 0, 0 };
	Utility_Strncpy(endtime, _saveTime, 6);
	int ret = 0;
	ret     = Utility_Is_A_ReportTime(endtime);  //用于判断是否到发送时间

	if (!ret) {
		printf("Not Send Time, now time is: %d/%d/%d  %d:%d:%d \r\n", endtime[ 0 ],
		       endtime[ 1 ], endtime[ 2 ], endtime[ 3 ], endtime[ 4 ], endtime[ 5 ]);
		return -1;
	}
	int _effect_count = 0;  //存储在flash的有效未发送的数据包
	Hydrology_ReadDataPacketCount(&_effect_count);  //读取内存里剩余未发送数据包数量
	TraceInt4(_effect_count, 1);
	int _startIdx = 0;
	int _endIdx   = 0;

	char _send[ 200 ] = { 0 };
	int  _ret	 = 0;
	int  _seek_num    = 0;  //防止死循环
	int  sendlen      = 0;

	_ret = FlowCheckSampleData(&_startIdx, &_endIdx);  //获得startidx endidx
	if (_ret != 0) {
		return -1;
	}
	while (_effect_count != 0) {
		TraceMsg("read data in :", 0);
		TraceInt4(_startIdx, 1);
		TraceInt4(_effect_count, 1);
		if (_seek_num
		    > HYDROLOGY_DATA_MAX_IDX)  //寻找的数据条数已经超过最大值就退出，防止死循环
		{
			TraceMsg("seek num out of range", 1);
			// hydrologHEXmyvPortFree();
			System_Delayms(2000);
			System_Reset();
		}

		_ret = Store_ReadDataItem(_startIdx, _send,
					  0);  //读取数据，ret为读出的数据长度

		if (_ret < 0) {
			TraceMsg("can't read data ! very bad .", 1);
			return -1;  //无法读取数据 就直接退了
		}
		else if (_ret == 1) {
			TraceMsg("It's sended data", 1);
			if (_startIdx
			    >= HYDROLOGY_DATA_MAX_IDX) {  //如果读取的startidx超过可存的最大index，则重新置零
				_startIdx = HYDROLOGY_DATA_MIN_IDX;
			}
			else {
				++_startIdx;
			}  //下一数据
			++_seek_num;
			Hydrology_SetStartIdx(_startIdx);  //要更新_startIdx.
			TraceInt4(_startIdx, 1);
			TraceInt4(_endIdx, 1);
			// hydrologyExitSend();
		}
		else  //未发送的数据
		{
			sendlen = _ret;

			lock_communication_dev();
			hydrologyProcessSend(_send, TimerReport);
			unlock_communication_dev();

			Store_MarkDataItemSended(_startIdx);  //设置该数据已发送
			--_effect_count;
			Hydrology_SetDataPacketCount(_effect_count);  //发送完后要更新有效数据cnt
			if (_startIdx >= HYDROLOGY_DATA_MAX_IDX) {
				_startIdx = HYDROLOGY_DATA_MIN_IDX;
			}
			else {
				++_startIdx;  //下一数据
			}
			++_seek_num;

			TraceMsg(_send, 1);
			Hydrology_SetStartIdx(_startIdx);  //更新_startIdx.
		}
	}

	TraceMsg("Report done", 1);
	System_Delayms(2000);
	/**************************************************************************************/
	// hydrologyProcessSend(TimerReport);

	if (!IsDebug) {
		lock_communication_dev();
		System_Delayms(5000);
		JudgeServerDataArrived();
		Hydrology_ProcessGPRSReceieve();
		JudgeServerDataArrived();
		Hydrology_ProcessGPRSReceieve();
		JudgeServerDataArrived();
		Hydrology_ProcessGPRSReceieve();
		unlock_communication_dev();
	}

	time_10min = 0;

	endtime[ 0 ] = 0;
	endtime[ 1 ] = 0;
	endtime[ 2 ] = 0;
	endtime[ 3 ] = 0;
	endtime[ 4 ] = 0;
	endtime[ 5 ] = 0;

	return 0;
}

int HydrologyVoltage() {
	//    char _temp_voltage[4];
	//
	//    _temp_voltage[0] = A[0] >> 8;
	//    _temp_voltage[1] = A[0] & 0x00FF;
	//
	//    Store_SetHydrologyVoltage(_temp_voltage);
	//
	return 0;
}

static bool check_if_rtc_time_format_correct(void) {

	for (int i = 0; i < 11; ++i) {
		RTC_ReadTimeBytes5(g_rtc_nowTime);
		if (RTC_IsBadTime(g_rtc_nowTime, 1) == 0) {
			return true;
		}
	}

	return false;
}

static void try_to_correct_rtc_time_via_network(void) {

	lock_communication_dev();
	communication_module_t* comm_dev = get_communication_dev();
	rtc_time_t		rtc_time = comm_dev->get_real_time();
	unlock_communication_dev();

	if (rtc_time.year == 0) {
		return;
	}
	printf("sync rtc time via network success : %d/%d/%d %d:%d:%d \r\n", rtc_time.year,
	       rtc_time.month, rtc_time.date, rtc_time.hour, rtc_time.min, rtc_time.sec);
	_RTC_SetTime(( char )rtc_time.sec, ( char )rtc_time.min, ( char )rtc_time.hour,
		     ( char )rtc_time.date, ( char )rtc_time.month, 1, ( char )rtc_time.year, 0);

	vTaskDelay(50 / portTICK_RATE_MS);
}

void check_rtc_time(void) {

	if (check_if_rtc_time_format_correct() == true) {
		return;
	}

	err_printf("rtc time format error, trying to correct rtc via network ... \r\n");
	try_to_correct_rtc_time_via_network();

	if (check_if_rtc_time_format_correct() == false) {
		err_printf("correct rtc failed, rebooting ... \r\n ");
		System_Reset();
	}
}

/*?????��???????????????????????????????*/
int Hydrology_TimeCheck() {
	RTC_ReadTimeBytes5(g_rtc_nowTime);  //??rtc???????g_rtc_nowtime

	int _time_error = 1;
	for (int i = 0; i < 11; ++i) {
		if (RTC_IsBadTime(g_rtc_nowTime, 1) == 0) {
			// TraceMsg("Time is OK .",1);
			_time_error = 0;
			break;  //????????
		}
		TraceMsg("Time is bad once .", 1);
		//????????,???
		RTC_ReadTimeBytes5(g_rtc_nowTime);
	}
	if (_time_error > 0) {  //??��????? ,?????????????????
		TraceMsg("Device time is bad !", 1);
		TraceMsg("Waiting for config !", 1);
		char newtime[ 6 ] = { 0 };

		lock_communication_dev();
		communication_module_t* comm_dev = get_communication_dev();

		rtc_time_t rtc_time = comm_dev->get_real_time();
		unlock_communication_dev();
		if (rtc_time.year == 0) {
			return -1;
		}
		printf("update rtc time, %d/%d/%d %d:%d:%d \r\n", rtc_time.year, rtc_time.month,
		       rtc_time.date, rtc_time.hour, rtc_time.min, rtc_time.sec);
		_RTC_SetTime(( char )rtc_time.sec, ( char )rtc_time.min, ( char )rtc_time.hour,
			     ( char )rtc_time.date, ( char )rtc_time.month, 1,
			     ( char )rtc_time.year, 0);

		System_Delayms(50);
	}
	if (RTC_IsBadTime(g_rtc_nowTime, 1) < 0) {  //????????????????,?????????????
		TraceMsg("Still bad time!", 1);
		System_Reset();
		return -2;
	}
	return 0;
}

void task_hydrology_run(void* pvParameters) {
	while (1) {
		TimerB_Clear();
		WatchDog_Clear();
		Hydrology_ProcessUARTReceieve();
		Hydrology_TimeCheck();

		if (RTC_IsBadTime(g_rtc_nowTime, 1) != 0) {
			vTaskDelay(100 / portTICK_PERIOD_MS);
			continue;
		}

		if (!IsDebug) {
			if (time_1min)
				time_1min = 0;
			else
				continue;
		}

		HydrologyTask();
		vTaskDelay(100 / portTICK_PERIOD_MS);
	}
}

