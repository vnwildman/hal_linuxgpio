/********************************************************************
* Description:  hal_linuxgpio.c
*               Driver for GPIO pins using sysfs interface
*
* Author: Trần Ngọc Quân
* License: GPL Version 2
* Copyright (c) 2017.
*
/********************************************************************/


#include "rtapi.h"		/* RTAPI realtime OS API */
#include "rtapi_bitops.h"
#include "rtapi_app.h"		/* RTAPI realtime module decls */
#include "hal.h"		/* HAL public API decls */

#define MAX_PIN 26
#define BUFFER_MAX 3
#define DIRECTION_MAX 35

#define HIGH 0x1
#define LOW  0x0

#define TRUE  0x1
#define FALSE 0x0

#if !defined(BUILD_SYS_USER_DSO)
#error "This driver is for usermode threads only"
#endif

#include <fcntl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <unistd.h>

static signed char   gpios[MAX_PIN] = {[0 ... MAX_PIN-1] = -1};
static signed char     dir[MAX_PIN] = {[0 ... MAX_PIN-1] =  1}; // all output
static         int gpiosfd[MAX_PIN] = {[0 ... MAX_PIN-1] = -1};

static int npins = 0;

MODULE_AUTHOR("Trần Ngọc Quân");
MODULE_DESCRIPTION("Driver GPIO pins using sysfs interface");
MODULE_LICENSE("GPL");

//Example: loadrt hal_linuxgpio input_pins="3,5,7" output_pins="20,31,26,24,21,19,23,32,33,8,10,36"
static int input_pins[] = { [0 ... MAX_PIN-1] = -1 } ;
RTAPI_MP_ARRAY_INT(input_pins, MAX_PIN,"input pin up to 26 pins");

static int output_pins[] = { [0 ... MAX_PIN-1] = -1 } ;
RTAPI_MP_ARRAY_INT(output_pins, MAX_PIN, "output pin up to 26 pins");

static int comp_id;  /* component ID */
hal_bit_t **gpio_data;

static int parse_conf(void);
static void rw_gpio(void *arg, long period);

int rtapi_app_main(void)
{
	int retval = 0;
	int n = 0 ;
	signed char pin;

	comp_id = hal_init("hal_linuxgpio");
	if (comp_id < 0)
		{
			rtapi_print_msg(RTAPI_MSG_ERR,"HAL_LINUXGPIO: ERROR: hal_init() failed\n");
			return -1;
		}

	parse_conf();
	gpio_data = hal_malloc(npins * sizeof(void *));
	if (gpio_data == 0)
		{
			rtapi_print_msg(RTAPI_MSG_ERR,
			                "HAL_LINUXGPIO: ERROR: hal_malloc() failed\n");
			hal_exit(comp_id);
			return -1;
		}

	/* Setup hal pin in/out */
	for (n = 0; n < npins; n++)
		{
			pin = gpios[n];
			if (dir[n])
				{
					if ((retval = hal_pin_bit_newf(HAL_IN, &gpio_data[n],
					                               comp_id, "hal_linuxgpio.pin-%02d-out", pin)) < 0)
						{
							break;
						}
				}
			else
				{
					if ((retval = hal_pin_bit_newf(HAL_OUT, &gpio_data[n],
					                               comp_id, "hal_linuxgpio.pin-%02d-in", pin)) < 0)
						{
							break;
						}
				}
			if (retval < 0)
				{
					rtapi_print_msg(RTAPI_MSG_ERR,
					                "HAL_LINUXGPIO: ERROR: pin %d export failed with err=%d\n",
					                n,retval);
					hal_exit(comp_id);
					return -1;
				}
		}

	/*Export functions*/
	retval = hal_export_funct("hal_linuxgpio.readwrite", rw_port, 0,
	                          0, 0, comp_id);
	if (retval < 0)
		{
			rtapi_print_msg(RTAPI_MSG_ERR,
			                "HAL_LINUXGPIO: ERROR: readwrite function export failed\n");
			hal_exit(comp_id);
			return -1;
		}

	rtapi_print_msg(RTAPI_MSG_INFO, "HAL_LINUXGPIO: installed driver\n");
	hal_ready(comp_id);
	return 0;
}

void rtapi_app_exit(void)
{
	int i;
	for (i = 0; i < MAX_PIN -1; i++)
		{
			if (gpiofd[i] != -1)
				close(gpiofd[i]);
		}
	hal_exit(comp_id);
}

static void rw_gpio(void *arg, long period)
{
	int n;
	char value_str[BUFFER_MAX];

	for (n = 0; n < npins; n++)
		{
			if (dir[n])
				{
					if (1 != write(gpiofd[n], *(gpio_data[n]) ? "1" : "0", 1))
						{
							rtapi_print_msg(RTAPI_MSG_ERR, "HAL_LINUXGPIO: ERROR: Failed to write value!\n");
							hal_exit(comp_id);
							return(-1);
						}
				}
			else
				{
					if (-1 == read(fd, value_str, BUFFER_MAX))
						{
							rtapi_print_msg(RTAPI_MSG_ERR, "HAL_LINUXGPIO: ERROR: Failed to read value!\n");
							hal_exit(comp_id);
							return(-1);
						}
					*gpio_data[n] = atoi(value_str);
				}
		}
}

static int parse_conf()
{
	int pin, i;
	char buffer[BUFFER_MAX];
	char path[DIRECTION_MAX];
	ssize_t bytes_written;
	int fd;

	if(input_pins[0] == -1 && output_pins[0] == -1)
		{
			rtapi_print_msg(RTAPI_MSG_ERR, "HAL_LINUXGPIO: ERROR: missing both input_pins and ouput_pins configure string\n");
			hal_exit(comp_id);
			return -1;
		}

	/* parse configure input pins */
	for (i = 0; i < MAX_PIN -1; i++)
		{
			pin = input_pins[i];
			if (pin == -1) break;

			gpio[npins] = pin;
			dir[npins]  = 0;   // set pin as input

			snprintf(path, DIRECTION_MAX, "/sys/class/gpio/gpio%d/direction", pin);
			if(!access(path, F_OK ))
				{
					rtapi_print_msg(RTAPI_MSG_INFO, "Pin %d is ready exported by other application or duplicated\n", pin);
					continue;
				}

			/*Export*/
			fd = open("/sys/class/gpio/export", O_WRONLY);
			if (-1 == fd])
				{
					rtapi_print_msg(RTAPI_MSG_ERR, "HAL_LINUXGPIO: ERROR: Failed to open export for writing!\n");
					hal_exit(comp_id);
					return(-1);
				}
			bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
			write(fd, buffer, bytes_written);
			close(fd);

			/* Set it as input */
			fd = open(path, O_WRONLY);
			if (-1 == fd)
			{
				rtapi_print_msg(RTAPI_MSG_ERR, "HAL_LINUXGPIO: ERROR: Failed to open gpio direction for writing!\n");
					hal_exit(comp_id);
					return(-1);
				}
			if (-1 == write(fd, "in", 2))
				{
					rtapi_print_msg(RTAPI_MSG_ERR, "HAL_LINUXGPIO: ERROR: Failed to set direction!\n");
					hal_exit(comp_id);
					return(-1);
				}
			close(fd);

			snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
			gpiofd[npins] = open(path, O_WONLY);
			if (-1 == gpiofd[npins])
				{
					rtapi_print_msg(RTAPI_MSG_ERR, "HAL_LINUXGPIO: ERROR: Failed to open gpio value for reading!\n");
					hal_exit(comp_id);
					return(-1);
				}
			npins++;
		}

	/* parse configure output pins */
	for (i = 0; i < MAX_PIN -1; i++)
		{
			pin = input_pins[i];
			if (pin == -1) break;

			gpio[npins] = pin;
			dir[npins]  = 1;   // set pin as output

			snprintf(path, DIRECTION_MAX, "/sys/class/gpio/gpio%d/direction", pin);
			if(!access(path, F_OK ))
				{
					rtapi_print_msg(RTAPI_MSG_INFO, "Pin %d is ready exported by other application or duplicated\n", pin);
					continue;
				}

			/*Export*/
			fd = open("/sys/class/gpio/export", O_WRONLY);
			if (-1 == fd])
				{
					rtapi_print_msg(RTAPI_MSG_ERR, "HAL_LINUXGPIO: ERROR: Failed to open export for writing!\n");
					hal_exit(comp_id);
					return(-1);
				}
			bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
			write(fd, buffer, bytes_written);
			close(fd);

			/* Set it as out */
			fd = open(path, O_WRONLY);
			if (-1 == fd)
			{
				rtapi_print_msg(RTAPI_MSG_ERR, "HAL_LINUXGPIO: ERROR: Failed to open gpio direction for writing!\n");
					hal_exit(comp_id);
					return(-1);
				}
			if (-1 == write(fd, "out", 2))
				{
					rtapi_print_msg(RTAPI_MSG_ERR, "HAL_LINUXGPIO: ERROR: Failed to set direction!\n");
					hal_exit(comp_id);
					return(-1);
				}
			close(fd);

			snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
			gpiofd[npins] = open(path, O_RDONLY);
			if (-1 == gpiofd[npins])
				{
					rtapi_print_msg(RTAPI_MSG_ERR, "HAL_LINUXGPIO: ERROR: Failed to open gpio value for reading!\n");
					hal_exit(comp_id);
					return(-1);
				}
			npins++;
		}

	npins++;
}
