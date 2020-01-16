# RTU-FREERTOS

# git注意事项
需要忽略的目录:Debug_Large_Data_Model/ ， 如果不忽略，git的时候会产生冲突，并且冲突无法解决

# 代码维护注意事项
1. 使用printf/err_printf/debug_printf时，请在末尾加两个换行(\n\n)，以方便查看串口打印消息
2. MSP430数据类型double默认4字节，可以在Option/General Option/Floating-point中修改


# 工程目录说明

## Apps
放置上层应用

## Common
FreeRTOS所需要的硬件库

## Debug_Large_Data_Model
IAR生成的中间文件

## Documents
与RTU工程相关的文档

## Driverlib
MSP430F5438A的官方HAL库, 文档: http://software-dl.ti.com/msp430/msp430_public_sw/mcu/msp430/MSP430_Driver_Library/latest/exports/driverlib/msp430_driverlib_2_91_11_01/doc/MSP430F5xx_6xx/html/index.html

## Drivers
自己写的外设驱动

## F5XX_6XX_Core_Lib
MSP430F5438时钟相关的驱动

## Oldproject
老RTU工程的代码

## Source
FreeRTOS源代码




# 配置报文: (必须要配置，否则会无限重启)
1.恢复遥测站出厂设置  
7E7E00445566776204D248800A020000191126151933980005C25F0D0A

2.中心站修改遥测站基本配置表 (需要修改设备号,在连续的四个66后的一个字节，并生成CRC16-MODEMBUS校验码，倒数第三第四字节)
7E 7E 00 68 15 33 33 00 12 34 40 80 27 02 00 00 18 01 21 15 00 33 02 28 66 66 66 66 79 04 50 02 03 91 08 19 00 66 00 99 99 05 50 02 04 70 97 21 81 24 00 66 66 05 71 31 0d 0a

[151, 全部发47服务器]
7E 7E 00 68 15 33 33 00 12 34 40 80 27 02 00 00 18 01 21 15 00 33 02 28 66 66 66 61 51 05 50 02 04 70 97 21 81 24 00 66 66 05 50 02 04 70 97 21 81 24 00 66 66 05 94 27

3.中心站修改遥测站运行参数配置表 (5分钟发一次)
7E7E006666666601123442803802000019112615193320080121080522080823100300240805250905260901270801282300001000301B0010003812012540120001411200030581B90d0a

4.自定义扩展配置要素报文
7E7E006666666601123442802F020000140121160134FF275001030101050104101002FF605002030101050104101802FF615002030101050105101802051E4A0d0a 




# BUG

## 拔掉调试器后配置报文被reset
拔掉调试器后需要重新4条报文

## pvPortMalloc/vPortFree 等FREERTOR提供的API只能在任务体里使用
推荐使用带内存泄漏检测的malloc与free

## 配置报文导致pirntf卡死
未解决，设想用uart3_send实现putchar