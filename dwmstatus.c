/*

  dwmstatus.c

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <X11/Xlib.h>
#include <alsa/asoundlib.h>
#include "config.h"

static int     rx_bytes    = 0;
static int     tx_bytes    = 0;
static float   rx_speed    = 0.0;
static float   tx_speed    = 0.0;
static char    *rx_str     = NULL;
static char    *tx_str     = NULL;
static long    cpu_work    = 0;
static long    cpu_total   = 0;
static Display *dpy        = NULL;
static char    *status_str = NULL;
static char    *buffer_str = NULL;

static void set_status(const char *str)
{
  XStoreName(dpy, DefaultRootWindow(dpy), str);
  XSync(dpy, False);
}

static void netspeed()
{
  FILE *rx_fd, *tx_fd;
  int rx, tx;
  char buffer[64];
  memset(buffer, '\0', 64);

  rx_fd = fopen("/sys/class/net/" IF_LINK "/statistics/rx_bytes", "r");
  fgets(buffer, 64, rx_fd);
  fclose(rx_fd);
  rx = atoi(buffer);
  memset(buffer, '\0', 64);
  tx_fd = fopen("/sys/class/net/" IF_LINK "/statistics/tx_bytes", "r");
  fgets(buffer, 64, tx_fd);
  fclose(tx_fd);
  tx = atoi(buffer);
  memset(buffer, '\0', 64);

  rx_speed = (rx - rx_bytes) / (1024.0 * UPDATE_INTERVAL);
  tx_speed = (tx - tx_bytes) / (1024.0 * UPDATE_INTERVAL);

  rx_bytes = rx;
  tx_bytes = tx;

  if (rx_speed < 10)
    sprintf(rx_str, "%.2f", rx_speed);
  else if (rx_speed < 100)
    sprintf(rx_str, "%.1f", rx_speed);
  else
    sprintf(rx_str, "%04d", (int) rx_speed);

  if (tx_speed < 10)
    sprintf(tx_str, "%.2f", tx_speed);
  else if (tx_speed < 100)
    sprintf(tx_str, "%.1f", tx_speed);
  else
    sprintf(tx_str, "%04d", (int) tx_speed);

  sprintf(buffer_str,
    FG_GREEN"\uE061"FG_NORM"%s K/s"FG_RED"\uE060"FG_NORM"%s K/s",
    rx_str, tx_str);
}

static void mem_usage()
{
  int total, free, buffers, cached;
  FILE *f;
  f = fopen("/proc/meminfo", "r");
  fscanf(f, "%*s %d %*s %*s %d %*s %*s %d %*s %*s %d",
    &total, &free, &buffers, &cached);
  fclose(f);
  int used = total - free - buffers - cached;
  float used_mib = used / 1024.0;
  float used_mib_p = (float)(total - free - buffers - cached) / total * 100;
  sprintf(buffer_str, FG_PURPLE"\uE020"FG_NORM"%02.0f%% (%.0fM)",
    used_mib_p, used_mib);
}

static void cpu_info()
{
  FILE *fd;
  long j1, j2, j3, j4, j5, j6, j7, work, total, load, freq, temp;
  float freq_f, temp_f;
  fd = fopen("/proc/stat", "r");
  fscanf(fd, "cpu %ld %ld %ld %ld %ld %ld %ld",
         &j1, &j2, &j3, &j4, &j5, &j6, &j7);
  fclose(fd);
  work = j1 + j2 + j3 + j6 + j7;
  total = work + j4 + j5;
  load = 100 * (work - cpu_work) / (total - cpu_total);
  cpu_work = work;
  cpu_total = total;

  fd = fopen("/sys/devices/system/cpu/cpu1/cpufreq/scaling_cur_freq", "r");
  if (fd == NULL) {
    fprintf(stderr, "Unable to get frequency.\n");
    exit(-1);
  }
  fscanf(fd, "%ld", &freq);
  fclose(fd);
  freq_f = (float) freq / 1000000;

  fd = fopen("/sys/class/hwmon/hwmon0/device/temp2_input", "r");
  if (fd == NULL)
    fd = fopen("/sys/class/hwmon/hwmon1/device/temp2_input", "r");
  if (fd == NULL) {
    fprintf(stderr, "Unable to get temp.\n");
    exit(-1);
  }
  fscanf(fd, "%ld", &temp);
  fclose(fd);

  temp_f = (float) temp / 1000;
  sprintf(buffer_str,
    FG_BLUE"\uE026"FG_NORM"%02ld%% (%.1fGHz)"FG_ORANGE"\uE01B"FG_NORM"%.1fC",
    load, freq_f, temp_f);
}

static void volume()
{
  long vol, vol_min, vol_max;
  float vol_f;
  snd_mixer_t *h_mixer;
  snd_mixer_selem_id_t *sid;
  snd_mixer_elem_t *elem;
  snd_mixer_selem_channel_id_t CHANNEL = SND_MIXER_SCHN_FRONT_LEFT;
  snd_mixer_open(&h_mixer, 1);
  snd_mixer_attach(h_mixer, "default");
  snd_mixer_selem_register(h_mixer, NULL, NULL);
  snd_mixer_load(h_mixer);
  snd_mixer_selem_id_malloc(&sid);
  snd_mixer_selem_id_set_index(sid, 0);
  snd_mixer_selem_id_set_name(sid, "Master");
  elem = snd_mixer_find_selem(h_mixer, sid);
  snd_mixer_selem_get_playback_volume_range(elem, &vol_min, &vol_max);
  snd_mixer_selem_get_playback_volume(elem, CHANNEL, &vol);
  snd_mixer_close(h_mixer);
  vol_f = (float) (vol * 100) / vol_max;
  sprintf(buffer_str, FG_MAGNETA"\uE05D"FG_NORM"%.0f%%", vol_f);
}

static void uptime()
{
  char buffer[32];
  FILE *fd = fopen("/proc/uptime", "r");
  fgets(buffer, 32, fd);
  fclose(fd);
  long uptime = atoi(buffer);
  int hours, minutes, seconds;
  hours = minutes = seconds = 0;
  minutes = uptime / 60;
  seconds = uptime % 60;
  if (minutes >= 60) {
    hours = minutes / 60;
    minutes = minutes % 60;
  }
  sprintf(buffer_str, "%02d:%02d:%02d", hours, minutes, seconds);
}

static void current_time()
{
  time_t t;
  struct tm *ti;
  char buffer[64];
  time(&t);
  ti = localtime(&t);
  strftime(buffer, 64, FG_CYAN"\uE015"FG_NORM"%b %d %a %H:%M:%S", ti);
  sprintf(buffer_str, "%s", buffer);
}

int main(int argc, char **argv)
{
  status_str = malloc(512);
  buffer_str = malloc(128);
  rx_str = malloc(16);
  tx_str = malloc(16);
  if (status_str == NULL || buffer_str == NULL || rx_str == NULL ||
      tx_str == NULL) {
    fprintf(stderr, "Cannot allocate memory.\n");
    exit(-1);
  }
  memset(status_str, '\0', 512);
  memset(buffer_str, '\0', 128);
  dpy = XOpenDisplay(NULL);
  if (dpy == NULL) {
    fprintf(stderr, "Cannot open display.\n");
    exit(-1);
  }
  while (1) {
    uptime();
    strcat(status_str, buffer_str);

    cpu_info();
    strcat(status_str, buffer_str);

    mem_usage();
    strcat(status_str, buffer_str);

    netspeed();
    strcat(status_str, buffer_str);

    volume();
    strcat(status_str, buffer_str);

    current_time();
    strcat(status_str, buffer_str);

    set_status(status_str);
    status_str[0] = '\0';
    sleep(UPDATE_INTERVAL);
  }
  free(status_str);
  free(buffer_str);
  XCloseDisplay(dpy);
  return 0;
}
