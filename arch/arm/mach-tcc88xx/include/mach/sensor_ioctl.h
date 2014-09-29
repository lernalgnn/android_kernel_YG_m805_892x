/*
 * File:        arch/arm/mach-tcc92xx/include/mach/sensor_ioctl.h
 */
 
#ifndef __TCC_SENSOR_DEV_H__
#define __TCC_SENSOR_DEV_H__

#define SENSOR_DEV_MAJOR_NUM 249
#define SENSOR_DEV_MINOR_NUM 1

/*
#define IOCTL_SENSOR_GET_DATA_ACCEL	0      
#define IOCTL_SENSOR_SET_DELAY_ACCEL	1
#define IOCTL_SENSOR_GET_STATE_ACCEL	2
#define IOCTL_SENSOR_SET_STATE_ACCEL	3

#define IOCTL_MSENSOR_GET_DATA_MAGNE	0      
#define IOCTL_MSENSOR_SET_DELAY_MAGNE	1
#define IOCTL_MSENSOR_GET_STATE_MAGNE	2
#define IOCTL_MSENSOR_SET_STATE_MAGNE	3

*/

#define IOCTL_SENSOR_SET_DELAY_ACCEL   	_IO(SENSOR_DEV_MAJOR_NUM, 100)
#define IOCTL_SENSOR_GET_DATA_ACCEL     _IO(SENSOR_DEV_MAJOR_NUM, 101)
#define IOCTL_SENSOR_GET_STATE_ACCEL   	_IO(SENSOR_DEV_MAJOR_NUM, 102)
#define IOCTL_SENSOR_SET_STATE_ACCEL    _IO(SENSOR_DEV_MAJOR_NUM, 103)
#define IOCTL_SENSOR_SET_CALIB_ACCEL	_IO(SENSOR_DEV_MAJOR_NUM, 104)
#define IOCTL_SENSOR_SET_INPUTDEVICE	_IO(SENSOR_DEV_MAJOR_NUM, 105)
#define IOCTL_SENSOR_GET_RESOLUTION		_IO(SENSOR_DEV_MAJOR_NUM, 106)
#define IOCTL_SENSOR_GET_TEMP_INFO		_IO(SENSOR_DEV_MAJOR_NUM, 107)

#define IOCTL_MSENSOR_SET_DELAY_MAGNE   _IO(SENSOR_DEV_MAJOR_NUM, 200)
#define IOCTL_MSENSOR_GET_DATA_MAGNE    _IO(SENSOR_DEV_MAJOR_NUM, 201)
#define IOCTL_MSENSOR_GET_STATE_MAGNE   _IO(SENSOR_DEV_MAJOR_NUM, 202)
#define IOCTL_MSENSOR_SET_STATE_MAGNE   _IO(SENSOR_DEV_MAJOR_NUM, 203)

#endif  /* __TCC_SENSOR_DEV_H__ */