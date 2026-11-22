
#include <stdio.h>
#include <stdlib.h>  
#include <ctype.h>
#include <string.h>
#include <stdarg.h>

#include "libretro.h"

#include "compiler.h"
#include "strres.h"
#include "xmil.h"
#include "dosio.h"
#include "fontmng.h"
#include "scrnmng.h"
#include "soundmng.h"
#include "mousemng.h"
#include "sysmng.h"
#include "taskmng.h"
#include "joymng.h"
#include "ini.h"
#include "pccore.h"
#include "iocore.h"
#include "scrndraw.h"
#include "x1f.h"
#include "diskdrv.h"
#include "timing.h"
#include "keystat.h"
#include "vramhdl.h"
#include "statsave.h"
#include "joymng.h"
#include "sdlkbd.h"
#include "fddfile.h"
#include "z80core.h"

#ifdef _WIN32
char slash = '\\';
#else
char slash = '/';
#endif

#define SOUNDRATE 44100.0
#define SNDSZ 735

char RPATH[512];
char RETRO_DIR[512];
const char *retro_save_directory;
const char *retro_system_directory;
const char *retro_content_directory;
char retro_system_conf[512];

char Core_Key_Sate[512];
char Core_old_Key_Sate[512];

int retrow=FULLSCREEN_WIDTH;
int retroh=FULLSCREEN_HEIGHT;
int CHANGEAV=0;

int pauseg=0;

signed short soundbuf[1024*2];

uint16_t videoBuffer[(SCREEN_PITCH / 2) * FULLSCREEN_HEIGHT];  //emu  surf

#define MAX_DISK_IMAGES 100
static char *images[MAX_DISK_IMAGES];
static int cur_disk_num;

#define NUM_SWAP_DRIVES 2
static int drive_idx[NUM_SWAP_DRIVES] = { -1, -1 };

static retro_video_refresh_t video_cb;
static retro_environment_t environ_cb;

static  retro_input_poll_t input_poll_cb;
static retro_log_printf_t log_cb;

retro_input_state_t input_state_cb;
retro_audio_sample_t audio_cb;
retro_audio_sample_batch_t audio_batch_cb;

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { audio_cb  =cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

static long framecount = 0;
int allow_scanlines = 0;

long GetTicks(void)
{
  return (framecount * 100) / 6;
}

int pre_main(const char *floppy)
{
   xmil_main(floppy); 

   return 0;
}

void texture_init(void)
{
   memset(videoBuffer, 0,retrow*retroh*2);
}

void log_printf(const char *format, ...)
{
	va_list va;
	char formatted[1024];

	va_start(va, format);
	vsnprintf(formatted, sizeof(formatted) - 1, format, va);
	if (log_cb)
		log_cb(RETRO_LOG_INFO, "%s\n", formatted);
	else
		fprintf(stderr, "%s\n", formatted);
	va_end(va);
}

#define MAX_CORE_OPTIONS 32
static struct retro_core_option_definition option_defs[MAX_CORE_OPTIONS + 1];
static char option_text[MAX_CORE_OPTIONS][512];

static const char *option_info(const char *key)
{
   if (!strcmp(key, "X1_MULTIDRIVE"))
      return "ON: the first two disks of an .m3u playlist are inserted in "
             "drives FDD0 and FDD1 at start. OFF: only drive FDD0 is used and "
             "'Swap Disks on Drive' has no effect. Changes apply when content "
             "is loaded or reset.";
   if (!strcmp(key, "X1_SWAPDRV"))
      return "By default using the native Disk Swap interface within "
             "RetroArch's menu will swap the disk in drive FDD0. Change this "
             "option to swap disks in drive FDD1. With Multi-Drive ON, the "
             "first two disks of an .m3u playlist are inserted in FDD0 and "
             "FDD1 at start.";
   return NULL;
}

static void register_core_options(retro_environment_t cb,
      const struct retro_variable *vars)
{
   unsigned version = 0;
   int n;

   if (!cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version) || version < 1)
   {
      cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void *)vars);
      return;
   }

   memset(option_defs, 0, sizeof(option_defs));

   for (n = 0; vars[n].key && n < MAX_CORE_OPTIONS; n++)
   {
      struct retro_core_option_definition *def = &option_defs[n];
      char *text = option_text[n];
      char *sep;
      char *tok;
      int v = 0;

      snprintf(text, sizeof(option_text[n]), "%s", vars[n].value);
      sep = strstr(text, "; ");
      if (!sep)
      {
         cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void *)vars);
         return;
      }
      *sep = '\0';

      def->key  = vars[n].key;
      def->desc = text;
      def->info = option_info(vars[n].key);

      tok = sep + 2;
      while (tok && *tok && v < RETRO_NUM_CORE_OPTION_VALUES_MAX - 1)
      {
         def->values[v].value = tok;
         def->values[v].label = NULL;
         v++;
         tok = strchr(tok, '|');
         if (tok)
            *tok++ = '\0';
      }
      def->default_value = def->values[0].value;
   }

   cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS, option_defs);
}

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;

   struct retro_variable variables[] = {
      { "X1_RESOLUTE" , "Resolution; LOW|HIGH" },
      { "X1_BOOTMEDIA" , "Boot media; 2HD|2D" },
      { "X1_ROMTYPE", "ROM type; X1|TURBO|TURBOZ" },
      { "X1_FPS", "FPS; AUTO|60|30|20|15" },
      { "X1_DISPSYNC", "Disp Vsync; OFF|ON" },
      { "X1_RASTER", "Raster; OFF|ON" },
      { "X1_NOWAIT", "No wait; OFF|ON" },
      { "X1_BTN_MODE", "Joy Reverse; OFF|ON" },
      { "X1_BTN_RAPID", "Joy Rapid; OFF|ON" },
      { "X1_AUDIO_RATE", "Audio sampling rate; 44100|22050|11025"},
      {	"X1_SEEKSND", "Seek Sound; OFF|ON" },
      {	"X1_SCANLINES", "Scanlines in low-res; OFF|ON" },
      { "X1_KEY_MODE", "Key mode; Keyboard|JoyKey-1|JoyKey-2|Mouse-Key" },
      { "X1_FMBOARD", "FM Board; ON|OFF"
#if defined(SUPPORT_OPMx2)
	"|DOUBLE"
#endif
      },
      { "X1_AUDIO_DELAYMS", "Audio buffer (ms); 250|100|150|200|300|350|500|750|1000" },
      { "X1_CPU_CLOCK", "Cpu clock (MHz); 4|1|2|3|4|5|6|7|8|9|10|11|12|13|14|15|16|17|18|19|20" },
      { "X1_MULTIDRIVE", "Multi-Drive; ON|OFF" },
      { "X1_SWAPDRV", "Swap Disks on Drive; FDD0|FDD1" },

      { NULL, NULL },
   };

   register_core_options(cb, variables);

   struct retro_vfs_interface_info vfs_interface_info;
   vfs_interface_info.required_interface_version = 1;
   vfs_interface_info.iface = NULL;
   if (cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vfs_interface_info))
     vfs_interface = vfs_interface_info.iface;

   struct retro_log_callback logging;

   if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
	   log_cb = logging.log;
}

static int get_booleanvar(const char *name, const char *true_string) {
  struct retro_variable var = {0};

  var.key = name;
  var.value = NULL;

  return environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && strcmp(var.value, true_string) == 0;
}

static void update_variables(void)
{
   struct retro_variable var = {0};

   int audiorate = 44100;

   var.key = "X1_AUDIO_RATE";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      audiorate = atoi(var.value);      
   }

   if (audiorate != 44100 && audiorate != 22050 && audiorate != 11025)
     audiorate = 44100;

   if (xmilcfg.samplingrate != (UINT16)audiorate) {
     xmilcfg.samplingrate = (UINT16)audiorate;
     corestat.soundrenewal = 1;
   }

   int resolute = get_booleanvar("X1_RESOLUTE", "LOW");
   int bootmedia = get_booleanvar("X1_BOOTMEDIA", "2D");

   xmilcfg.DIP_SW = (resolute ? DIPSW_RESOLUTE : 0) | (bootmedia ? DIPSW_BOOTMEDIA : 0);

   int romtype = 1;

   var.key = "X1_ROMTYPE";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "X1") == 0)
         romtype = 1;
      else if (strcmp(var.value, "TURBO") == 0)
         romtype = 2;
      else if (strcmp(var.value, "TURBOZ") == 0)
         romtype = 3;
   }

   xmilcfg.ROM_TYPE = romtype;

   int fps = 0;

   var.key = "X1_FPS";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "AUTO") == 0)
         fps = 0;
      else if (strcmp(var.value, "60") == 0)
         fps = 1;
      else if (strcmp(var.value, "30") == 0)
         fps = 2;
      else if (strcmp(var.value, "20") == 0)
         fps = 3;
      else if (strcmp(var.value, "15") == 0)
         fps = 4;
   }

   xmiloscfg.DRAW_SKIP = fps;

   xmilcfg.DISPSYNC = get_booleanvar("X1_DISPSYNC", "ON");
   xmilcfg.RASTER = get_booleanvar("X1_RASTER", "ON");
   xmiloscfg.NOWAIT = get_booleanvar("X1_NOWAIT", "ON");
   xmilcfg.BTN_MODE = get_booleanvar("X1_BTN_MODE", "ON");
   xmilcfg.BTN_RAPID = get_booleanvar("X1_BTN_RAPID", "ON");
   xmilcfg.MOTOR = get_booleanvar("X1_SEEKSND", "ON");
   allow_scanlines = get_booleanvar("X1_SCANLINES", "ON");

   int keymode = 0;
   var.key = "X1_KEY_MODE";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "Keyboard") == 0)
         keymode = 0;
      else if (strcmp(var.value, "JoyKey-1") == 0)
         keymode = 1;
      else if (strcmp(var.value, "JoyKey-2") == 0)
         keymode = 2;
      else if (strcmp(var.value, "Mouse-Key") == 0)
         keymode = 3;
   }

   if (xmilcfg.KEY_MODE != keymode) {
     xmilcfg.KEY_MODE = keymode;
     keystat_resetjoykey();
   }

   int fmboard = 1;

   var.key = "X1_FMBOARD";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "OFF") == 0)
         fmboard = 0;
      else if (strcmp(var.value, "ON") == 0)
         fmboard = 1;
      else if (strcmp(var.value, "DOUBLE") == 0)
         fmboard = 2;
   }

   xmilcfg.SOUND_SW = fmboard;

   int delayms = 250;

   var.key = "X1_AUDIO_DELAYMS";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      delayms = atoi(var.value);      
   }

   if (delayms < 100 || delayms > 1000)
     delayms = 250;

   xmilcfg.delayms = delayms;

   xmilcfg.skipline = 0;
   xmilcfg.skiplight = 0;

   int cpuclock = 4;

   var.key = "X1_CPU_CLOCK";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      cpuclock = atoi(var.value);
   }

   if (cpuclock < 1 || cpuclock > 20)
     cpuclock = 4;

   xmilcfg.X1_CPU_CLOCK = 1000000 * cpuclock;
}

#define KEYP(a,b) {\
	if(Core_Key_Sate[a] && Core_Key_Sate[a]!=Core_old_Key_Sate[a]  )\
		sdlkbd_keydown(a);\
	else if ( !Core_Key_Sate[a] && Core_Key_Sate[a]!=Core_old_Key_Sate[a]  )\
		sdlkbd_keyup(a);\
}	

void update_input(void)
{
 	static int mposx=640/2,mposy=400/2;
	int i;

  	input_poll_cb();

  	joymng_sync();




   		for(i=0;i<320;i++)
      			Core_Key_Sate[i]=input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0,i) ? 0x80: 0;

   		if(memcmp( Core_Key_Sate,Core_old_Key_Sate , sizeof(Core_Key_Sate) ) )
			for(i=0;i<320;i++){
				KEYP(i,i);
			}

   		memcpy(Core_old_Key_Sate,Core_Key_Sate , sizeof(Core_Key_Sate) );
}


#if 0
static void keyboard_cb(bool down, unsigned keycode, uint32_t character, uint16_t mod)
{
}
#endif

/************************************
 * libretro implementation
 ************************************/

//static struct retro_system_av_info g_av_info;

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name = "x1";
   info->library_version = "0.60";
   info->need_fullpath = true;
   info->valid_extensions = "dx1|zip|2d|2hd|tfd|d88|88d|hdm|xdf|dup|cmd|m3u";
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   /* FIXME handle PAL/NTSC */
   struct retro_game_geometry geom = { retrow, retroh,
				       FULLSCREEN_WIDTH,
				       FULLSCREEN_HEIGHT,
				       4.0 / 3.0 };
   struct retro_system_timing timing = { 60.0, SOUNDRATE };

   info->geometry = geom;
   info->timing   = timing;
}

void update_geometry(void)
{
   struct retro_system_av_info system_av_info;
   system_av_info.geometry.base_width = retrow;
   system_av_info.geometry.base_height = retroh;
   system_av_info.geometry.aspect_ratio = (float)4/3;// retro_aspect;
   environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &system_av_info);
}


void retro_set_controller_port_device(unsigned port, unsigned device)
{
    (void)port;
    (void)device;
}

#define SERIAL_SIZE 265000

size_t retro_serialize_size(void)
{
	return SERIAL_SIZE;
}

bool retro_serialize(void *data, size_t size)
{
  FILEH fh = make_writemem_file();
  int res= statsave_save_fh(fh);
  if (res<0)
    return false;
  if (fh->memsize + 8 > size)
    return false;
  memset(data, 0, size);
  memcpy((char *)data, &framecount, 8);
  memcpy((char *)data + 8, fh->mem, fh->memsize);
  return true;
}

static void sync_drive_selection(void)
{
  int d, i;

  for (d = 0; d < NUM_SWAP_DRIVES; d++) {
    if (!fddfile_diskready(d))
      continue;
    for (i = 0; i < cur_disk_num; i++) {
      if (images[i] && !strcmp(images[i], fddfile_diskname(d))) {
        drive_idx[d] = i;
        break;
      }
    }
  }
}

bool retro_unserialize(const void *data, size_t size)
{
  memcpy(&framecount, (char *)data, 8);
  FILEH fh = make_readmem_file(data+8, size-8);
  if (statsave_load_fh(fh) < 0)
    return false;
  sync_drive_selection();
  return true;
}

void retro_cheat_reset(void)
{}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
    (void)index;
    (void)enabled;
    (void)code;
}

static void disk_list_clear(void)
{
   int i;

   for (i = 0; i < MAX_DISK_IMAGES; i++)
   {
      if (images[i])
      {
         free(images[i]);
         images[i] = NULL;
      }
   }
   drive_idx[0] = -1;
   drive_idx[1] = -1;
   cur_disk_num = 0;
}

static int path_has_extension(const char *path, const char *ext)
{
   size_t plen = strlen(path);
   size_t elen = strlen(ext);
   size_t i;

   if (plen <= elen || path[plen - elen - 1] != '.')
      return 0;

   for (i = 0; i < elen; i++)
   {
      if (tolower((unsigned char)path[plen - elen + i]) !=
          tolower((unsigned char)ext[i]))
         return 0;
   }
   return 1;
}

static int path_is_absolute(const char *path)
{
   if (path[0] == '/' || path[0] == '\\')
      return 1;
   if (isalpha((unsigned char)path[0]) && path[1] == ':')
      return 1;
   return 0;
}

static int disk_list_load_m3u(const char *m3u_path)
{
   char line[1024];
   char base_dir[1024];
   char full[2048];
   FILE *fp;
   const char *sep1;
   const char *sep2;
   const char *last_sep;
   size_t base_len;
   int first_line = 1;

   fp = fopen(m3u_path, "r");
   if (!fp)
   {
      log_printf("Could not open playlist: %s\n", m3u_path);
      return 0;
   }

   sep1 = strrchr(m3u_path, '/');
   sep2 = strrchr(m3u_path, '\\');
   if (!sep1)
      last_sep = sep2;
   else if (!sep2)
      last_sep = sep1;
   else
      last_sep = (sep1 > sep2) ? sep1 : sep2;
   base_len = last_sep ? (size_t)(last_sep - m3u_path) + 1 : 0;
   if (base_len >= sizeof(base_dir))
      base_len = 0;
   memcpy(base_dir, m3u_path, base_len);
   base_dir[base_len] = '\0';

   while (cur_disk_num < MAX_DISK_IMAGES &&
          fgets(line, sizeof(line), fp))
   {
      char *p = line;
      char *end;
      size_t len = strlen(line);

      if (len > 0 && line[len - 1] != '\n' && !feof(fp))
      {
         int c;
         while ((c = fgetc(fp)) != EOF && c != '\n')
            ;
      }

      if (first_line)
      {
         first_line = 0;
         if ((unsigned char)p[0] == 0xEF &&
             (unsigned char)p[1] == 0xBB &&
             (unsigned char)p[2] == 0xBF)
            p += 3;
      }

      while (*p == ' ' || *p == '\t')
         p++;

      end = p + strlen(p);
      while (end > p && (end[-1] == '\r' || end[-1] == '\n' ||
                         end[-1] == ' '  || end[-1] == '\t'))
         *--end = '\0';

      if (*p == '\0' || *p == '#')
         continue;

      if (path_is_absolute(p))
         snprintf(full, sizeof(full), "%s", p);
      else
         snprintf(full, sizeof(full), "%s%s", base_dir, p);

      images[cur_disk_num++] = strdup(full);
   }

   fclose(fp);
   return cur_disk_num;
}

static int multidrive_enabled(void)
{
   struct retro_variable var = {0};

   var.key = "X1_MULTIDRIVE";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value &&
       !strcmp(var.value, "OFF"))
      return 0;
   return 1;
}

bool retro_load_game(const struct retro_game_info *info)
{
   const char *full_path;

   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;

   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
	   log_printf("RGB565 is not supported.\n");
	   return false;
   }


   if (!info || !info->path)
   {
      log_printf("No content given.\n");
      return false;
   }

   full_path = info->path;

   disk_list_clear();

   if (path_has_extension(full_path, "m3u"))
   {
      if (disk_list_load_m3u(full_path) == 0)
      {
         log_printf("Playlist contains no disks: %s\n", full_path);
         return false;
      }
   }
   else
   {
      images[0] = strdup(full_path);
      cur_disk_num = 1;
   }

   drive_idx[0] = 0;
   drive_idx[1] = (multidrive_enabled() && cur_disk_num >= 2) ? 1 : -1;

   strncpy(RPATH, images[0], sizeof(RPATH) - 1);
   RPATH[sizeof(RPATH) - 1] = '\0';

   log_printf("LOAD EMU\n");

   return true;
}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info)
{
    (void)game_type;
    (void)info;
    (void)num_info;
    return false;
}

void retro_unload_game(void)
{
     pauseg=0;
     disk_list_clear();
}

unsigned retro_get_region(void)
{
    return RETRO_REGION_NTSC;
}

unsigned retro_api_version(void)
{
    return RETRO_API_VERSION;
}

void *retro_get_memory_data(unsigned id)
{
    switch (id)
    {
    case RETRO_MEMORY_SYSTEM_RAM:
	return mainmem;
    default:
	break;
    }

    return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
    switch (id)
    {
    case RETRO_MEMORY_SYSTEM_RAM:
	return sizeof(mainmem);
    default:
	break;
    }

    return 0;
}

static int swap_drive(void)
{
  struct retro_variable var = {0};

  if (!multidrive_enabled())
    return 0;
  var.key = "X1_SWAPDRV";
  if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value &&
      !strcmp(var.value, "FDD1"))
    return 1;
  return 0;
}

bool set_eject_state(bool ejected) {
  int drv = swap_drive();
  int idx = drive_idx[drv];

  if (ejected || idx < 0 || idx >= cur_disk_num || !images[idx]) {
    fddfile_eject(drv);
  } else {
    diskdrv_setfdd(drv, images[idx], 0);
  }
  return 1;
}

bool get_eject_state (void) {
  return !fddfile_diskready(swap_drive());
}

unsigned get_image_index (void) {
  int idx = drive_idx[swap_drive()];

  return (idx < 0) ? (unsigned)cur_disk_num : (unsigned)idx;
}

bool set_image_index(unsigned index) {
  drive_idx[swap_drive()] = ((int)index >= cur_disk_num) ? -1 : (int)index;
  return 1;
}

unsigned get_num_images(void) {
    return cur_disk_num;
}

bool replace_image_index(unsigned index,
			 const struct retro_game_info *info) {
  int i;

  if ((int)index >= cur_disk_num)
    return 0;

  if (info && info->path) {
    if (images[index])
      free(images[index]);
    images[index] = strdup(info->path);
    return 1;
  }

  if (images[index])
    free(images[index]);
  for (i = (int)index; i < cur_disk_num - 1; i++)
    images[i] = images[i + 1];
  images[cur_disk_num - 1] = NULL;
  cur_disk_num--;

  for (i = 0; i < NUM_SWAP_DRIVES; i++) {
    if (drive_idx[i] == (int)index)
      drive_idx[i] = -1;
    else if (drive_idx[i] > (int)index)
      drive_idx[i]--;
  }
  return 1;
}

bool add_image_index(void) {
  if (cur_disk_num >= MAX_DISK_IMAGES)
    return 0;
  images[cur_disk_num] = NULL;
  cur_disk_num++;
  return 1;
}

static bool get_image_path(unsigned index, char *path, size_t len) {
  if (!path || len == 0 || (int)index >= cur_disk_num || !images[index])
    return 0;
  snprintf(path, len, "%s", images[index]);
  return 1;
}

static bool get_image_label(unsigned index, char *label, size_t len) {
  const char *name;
  const char *sep1;
  const char *sep2;
  const char *dot;
  size_t n;

  if (!label || len == 0 || (int)index >= cur_disk_num || !images[index])
    return 0;

  name = images[index];
  sep1 = strrchr(name, '/');
  sep2 = strrchr(name, '\\');
  if (sep1 && (!sep2 || sep1 > sep2))
    name = sep1 + 1;
  else if (sep2)
    name = sep2 + 1;

  n = strlen(name);
  dot = strrchr(name, '.');
  if (dot && dot != name)
    n = (size_t)(dot - name);

  if (n >= len)
    n = len - 1;
  memcpy(label, name, n);
  label[n] = '\0';
  return 1;
}

struct retro_disk_control_callback disk_controller =
  {
   .set_eject_state = set_eject_state,
   .get_eject_state = get_eject_state,
   .get_image_index = get_image_index,
   .set_image_index = set_image_index,
   .get_num_images = get_num_images,
   .replace_image_index = replace_image_index,
   .add_image_index = add_image_index
};

struct retro_disk_control_ext_callback disk_controller_ext =
  {
   .set_eject_state = set_eject_state,
   .get_eject_state = get_eject_state,
   .get_image_index = get_image_index,
   .set_image_index = set_image_index,
   .get_num_images = get_num_images,
   .replace_image_index = replace_image_index,
   .add_image_index = add_image_index,
   .set_initial_image = NULL,
   .get_image_path = get_image_path,
   .get_image_label = get_image_label
};

static int has_font(const char *dir)
{
   static const char *names[] = { "FNT0808.X1", "FNT0816.X1", "FNT1616.X1" };
   char path[512];
   FILEH fh;
   int i;

   for (i = 0; i < 3; i++)
   {
      snprintf(path, sizeof(path), "%s%s", dir, names[i]);
      fh = file_open_rb(path);
      if (fh != FILEH_INVALID)
      {
         file_close(fh);
         return 1;
      }
   }
   return 0;
}

void retro_init(void)
{
   const char *system_dir = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir) && system_dir)
   {
      // if defined, use the system directory			
      retro_system_directory=system_dir;		
   }		   

   const char *content_dir = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY, &content_dir) && content_dir)
   {
      // if defined, use the system directory			
      retro_content_directory=content_dir;		
   }			

   const char *save_dir = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_dir) && save_dir)
   {
      // If save directory is defined use it, otherwise use system directory
      retro_save_directory = *save_dir ? save_dir : retro_system_directory;      
   }
   else
   {
      // make retro_save_directory the same in case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY is not implemented by the frontend
      retro_save_directory=retro_system_directory;
   }

   if(retro_system_directory==NULL)sprintf(RETRO_DIR, "%s\0",".");
   else sprintf(RETRO_DIR, "%s\0", retro_system_directory);

   sprintf(retro_system_conf, "%s%cxmil\0",RETRO_DIR,slash);

   {
      char font_dir[512];
      char *p;

      snprintf(font_dir, sizeof(font_dir), "%s%c", retro_system_conf, slash);
      if (slash == '\\')
      {
         for (p = font_dir; *p; p++)
         {
            if (*p == '\\')
               *p = '/';
         }
      }
      if (has_font(font_dir) || !has_font("./"))
         file_setcd(font_dir);
   }

   static struct retro_input_descriptor inputDescriptors[] = {
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "A" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "B" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "X" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Y" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Left" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Up" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Down" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "R" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "L" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, "R2" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, "L2" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3, "R3" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3, "L3" },

		{ 0 },
	};
	environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, &inputDescriptors);

/*
    struct retro_keyboard_callback cbk = { keyboard_cb };
    environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &cbk);
*/
	{
		unsigned dci_version = 0;
		if (environ_cb(RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION, &dci_version) && dci_version >= 1)
			environ_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE, &disk_controller_ext);
		else
			environ_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE, &disk_controller);
	}
  	update_variables();

    memset(Core_Key_Sate,0,512);
    memset(Core_old_Key_Sate ,0, sizeof(Core_old_Key_Sate));
}

void initload() {
  update_variables();
}

void retro_deinit(void)
{
   xmil_end();
   log_printf("Retro DeInit\n");
}

static int firstcall=1;

void retro_reset(void)
{
   if (firstcall)
      return;

   update_variables();

   if (!multidrive_enabled())
   {
      fddfile_eject(1);
      drive_idx[1] = -1;
   }

   pccore_reset();
}

void retro_run(void)
{
   framecount++;
   if(firstcall)
   {
      pre_main(RPATH);
      if (drive_idx[1] >= 0 && drive_idx[1] < cur_disk_num && images[drive_idx[1]])
         diskdrv_setfdd(1, images[drive_idx[1]], 0);
      update_variables();
      mousemng_enable(MOUSEPROC_SYSTEM);
      firstcall=0;
      log_printf("INIT done\n");
      return;
   }

   if (CHANGEAV == 1)
   {
      update_geometry();
      log_printf("w:%d h:%d a:%f\n",retrow,retroh,(float)(4/3));
      CHANGEAV=0;
   }

   bool updated = false;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
   {
      update_variables();
   }


   update_input();

   pccore_exec(TRUE);
   sound_play_cb(NULL, NULL,SNDSZ*4);

   video_cb(videoBuffer, retrow, retroh, SCREEN_PITCH);
}

