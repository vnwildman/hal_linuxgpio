/**********************************************************
* Description:  hal_linuxgpio.c
*               Driver for GPIO pins using sysfs interface
*
* Author: Trần Ngọc Quân
* License: GPL Version 2
* Copyright (c) 2017.
*
/*********************************************************/

#include "rtapi.h"		/* RTAPI realtime OS API */
#include "rtapi_bitops.h"
#include "rtapi_app.h"		/* RTAPI realtime module decls */
#include "hal.h"		/* HAL public API decls */

#define MAX_PIN       26
#define BUFFER_MAX     3
#define DIRECTION_MAX 35
#define VALUE_MAX     35

#if !defined(BUILD_SYS_USER_DSO)
#error "This driver is for usermode threads only"
#endif

#include <fcntl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>

static int pinname[MAX_PIN] = {[0 ... MAX_PIN-1] = -1};
static int     dir[MAX_PIN] = {[0 ... MAX_PIN-1] =  1}; // all output
static int  gpiofd[MAX_PIN] = {[0 ... MAX_PIN-1] = -1};

static int npins = 0;

MODULE_AUTHOR("Trần Ngọc Quân");
MODULE_DESCRIPTION("Driver GPIO pins using sysfs interface");
MODULE_LICENSE("GPL");

//Example: loadrt hal_linuxgpio input_pins="17,27,22" output_pins="14,15,18,23,24,25,8,7,1,12,16,20"
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
  int pin;

  comp_id = hal_init("hal_linuxgpio");
  if (comp_id < 0)
    {
      rtapi_print_msg(RTAPI_MSG_ERR,"HAL_LINUXGPIO: ERROR: hal_init() failed\n");
      return -1;
    }

  if (parse_conf() == -1)
    {
      rtapi_print_msg(RTAPI_MSG_ERR,"HAL_LINUXGPIO: ERROR: parse_conf() failed\n");
      hal_exit(comp_id);
      return -1;
    }

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
      pin = pinname[n];
      rtapi_print_msg(RTAPI_MSG_INFO, "Pin %d is used\n", pin);
      if (dir[n])
        {
          retval = hal_pin_bit_newf(HAL_IN, &gpio_data[n],
                                    comp_id, "hal_linuxgpio.pin-%02d-out", pin);
          rtapi_print_msg(RTAPI_MSG_INFO,
                          "Exported hal_linuxgpio.pin-%02d-out", pin);
        }
      else
        {
          retval = hal_pin_bit_newf(HAL_OUT, &gpio_data[n],
                                    comp_id, "hal_linuxgpio.pin-%02d-in", pin);
          rtapi_print_msg(RTAPI_MSG_INFO,
                          "Exported hal_linuxgpio.pin-%02d-in", pin);
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
  retval = hal_export_funct("hal_linuxgpio.update", rw_gpio, 0, 0, 0, comp_id);
  if (retval < 0)
    {
      rtapi_print_msg(RTAPI_MSG_ERR,
                      "HAL_LINUXGPIO: ERROR: update function export failed\n");
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
  for (i = 0; i < npins; i++)
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
              return;
            }
        }
      else
        {
          if (-1 == read(gpiofd[n], value_str, BUFFER_MAX))
            {
              rtapi_print_msg(RTAPI_MSG_ERR, "HAL_LINUXGPIO: ERROR: Failed to read value!\n");
              hal_exit(comp_id);
              return;
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

      pinname[npins] = pin;
      dir[npins]     = 0;   // set pin as input

      rtapi_print_msg(RTAPI_MSG_INFO, "Pin %d is configured as input\n", pin);

      snprintf(path, DIRECTION_MAX, "/sys/class/gpio/gpio%d/direction", pin);
      if(access(path, F_OK))
        {
          rtapi_print_msg(RTAPI_MSG_INFO, "Pin %d is ready exported by other application or duplicated\n", pin);
          continue;
        }

      /*Export*/
      fd = open("/sys/class/gpio/export", O_WRONLY);
      if (-1 == fd)
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
      gpiofd[npins] = open(path, O_RDONLY);
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

      pinname[npins] = pin;
      dir[npins]  = 1;   // set pin as output

      snprintf(path, DIRECTION_MAX, "/sys/class/gpio/gpio%d/direction", pin);
      if(access(path, F_OK ))
        {
          rtapi_print_msg(RTAPI_MSG_INFO, "Pin %d is ready exported by other application or duplicated\n", pin);
          continue;
        }

      /*Export*/
      fd = open("/sys/class/gpio/export", O_WRONLY);
      if (-1 == fd)
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
      gpiofd[npins] = open(path, O_WRONLY);
      if (-1 == gpiofd[npins])
        {
          rtapi_print_msg(RTAPI_MSG_ERR, "HAL_LINUXGPIO: ERROR: Failed to open gpio value for reading!\n");
          hal_exit(comp_id);
          return(-1);
        }
      npins++;
    }

  npins++;
  rtapi_print_msg(RTAPI_MSG_ERR, "HAL_LINUXGPIO: ERROR: npins %d!\n", npins);
  return 0;
}

