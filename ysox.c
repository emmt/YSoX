/*
 * ysox.c --
 *
 * Implements Yorick interface to SoX (an audio file-format and effect
 * library).
 *
 *-----------------------------------------------------------------------------
 *
 * Copyright (C) 2015 Éric Thiébaut <eric.thiebaut@univ-lyon1.fr>
 *
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <float.h>

#include <sox.h>

#include <pstdlib.h>
#include <play.h>
#include <yapi.h>

#define TRUE  1
#define FALSE 0

#define ON    1
#define OFF   0

/* Define some macros to get rid of some GNU extensions when not compiling
   with GCC. */
#if ! (defined(__GNUC__) && __GNUC__ > 1)
#   define __attribute__(x)
#   define __inline__
#   define __FUNCTION__        ""
#   define __PRETTY_FUNCTION__ ""
#endif

PLUG_API void y_error(const char *) __attribute__ ((noreturn));
static void push_string(const char* str);
static char* fetch_path(int iarg);
static void critical(void);

/* Structure to store a SoX stream. */
typedef struct _ysox ysox_t;

/* Push array for samples on top of the stack. */
static void* push_samples(long channels, long samples);

/* Define a Yorick global symbol with an int/long/double value. */
static void define_int_const(const char* name, int value);
static void define_long_const(const char* name, long value);
static void define_double_const(const char* name, double value);

/* Read a given number of samples and left the result on top of the stack. */
static void read_samples(ysox_t* obj, long samples);

/* Write samples, IARG is the stack position of the data to write.  If
   conversion occurs, this stack element is replaced by the converted data. */
static void write_samples(ysox_t* obj, int iarg);

/* Seek to given offset. */
static void seek_to(ysox_t* obj, long offset);

/* Format an id=value metadata. */
static char* format_metadata(char* buffer, const char* id, size_t id_len,
                             const char* value, size_t value_len);

/* Switch SIGFPE on/off. */
static void switch_fpemask(int on);

/*---------------------------------------------------------------------------*/
/* PSEUDO-OBJECTS FOR AUDIO STREAM */

static void ysox_free(void*);
static void ysox_print(void*);
static void ysox_eval(void*, int);
static void ysox_extract(void*, char*);

struct _ysox {
  sox_format_t* format;
  long offset;
};

static y_userobj_t ysox_type = {
  "SoX instance", ysox_free, ysox_print, ysox_eval, ysox_extract
};

static void
ysox_free(void* addr)
{
  ysox_t* obj = (ysox_t*)addr;
  if (obj->format != NULL) {
    sox_close(obj->format);
  }
}

static void
ysox_print(void* addr)
{
  ysox_t* obj = (ysox_t*)addr;
  sox_format_t* ft = obj->format;

  if (ft == NULL) {
    y_print("SoX instance with no input/output stream", TRUE);
  } else {
    char buf[1024];
    const char* text;
    double hours, minutes, seconds;
    y_print("SoX instance", TRUE);
    y_print("  Encoding: ", FALSE);
    y_print(sox_encodings_info[ft->encoding.encoding].name, TRUE);
    sprintf(buf, "  Channels: %u @ %u-bit", ft->signal.channels,
            ft->signal.precision);
    y_print(buf, TRUE);
    sprintf(buf, "  Samplerate: %gHz", ft->signal.rate);
    y_print(buf, TRUE);

    seconds = ft->signal.length/ft->signal.channels/ft->signal.rate;
    if (seconds >= 60.0) {
      minutes = floor(seconds/60.0);
      seconds -= 60.0*minutes;
    } else {
      minutes = 0.0;
    }
    if (minutes >= 60.0) {
      hours = floor(minutes/60.0);
      minutes -= 60.0*hours;
      sprintf(buf, "  Duration: %.0f:%02.0f:%06.3f",
              hours, minutes, seconds);
    } else {
      sprintf(buf, "  Duration: %02.0f:%06.3f", minutes, seconds);
    }
    y_print(buf, TRUE);

#define INFO(s)                                         \
    text = sox_find_comment(ft->oob.comments, s);       \
    if (text != NULL) {                                 \
      y_print("  " s ": ", FALSE);                      \
      y_print(text, TRUE);                              \
    }
    INFO("Comment");
    INFO("Description");

    INFO("Artist");
    INFO("Album");
    INFO("Year");
    text = sox_find_comment(ft->oob.comments, "Tracknumber");
    if (text != NULL) {
      y_print("  Track: ", FALSE);
      y_print(text, FALSE);
      text = sox_find_comment(ft->oob.comments, "Tracktotal");
      if (text != NULL) {
        y_print(" of ", FALSE);
        y_print(text, TRUE);
      } else {
        y_print("", TRUE);
      }
    }
    INFO("Title");
#undef INFO
  }
}

static void
ysox_eval(void* addr, int argc)
{
  ysox_t* obj = (ysox_t*)addr;
  if (argc != 1) {
    y_error("missing or bad argument");
    return;
  }
  if (obj->format == NULL) {
    y_error("input/output of audio stream has been closed");
  }
  if (obj->format->mode == 'r') {
    /* Input audio stream. */
    long offset, samples;
    long ntot = obj->format->signal.length/obj->format->signal.channels;
    int type = yarg_typeid(0);
    int rank = yarg_rank(0);
    if (rank == 0 && (type == Y_CHAR || type == Y_SHORT || type == Y_INT
                      || type == Y_LONG)) {
      long i = ygets_l(0);
      if (i <= 0) i += ntot;
      offset = i - 1; /* Yorick indices start at 1 */
      samples = 1;
    } else if (type == Y_VOID) {
      /* Read all remaing data. */
      offset = obj->offset;
      samples = ntot - offset;
    } else if (type == Y_RANGE) {
      long mms[3];
      int flags = yget_range(0, mms);
      if (flags == Y_MMMARK) y_error("unexpected matrix multiply marker");
      if (flags == Y_PSEUDO) y_error("unexpected '-' marker");
      if (flags == Y_RUBBER) y_error("unexpected rubber band marker");
      if (flags == Y_NULLER) {
        ypush_nil();
        return;
      }
      if (flags == Y_RUBBER1) {
        offset = 0;
        samples = obj->format->signal.length/obj->format->signal.channels;
      } else {
        long imin = ((flags & Y_MIN_DFLT) != 0 ? obj->offset + 1 : mms[0]);
        long imax = ((flags & Y_MAX_DFLT) != 0 ? ntot : mms[1]);
        if (mms[2] != 1) y_error("subsampling or reversing not "
                                 "yet implemented");
        if (imin <= 0) imin += ntot;
        if (imax <= 0) imax += ntot;
        if (imin > imax || imin <= 0 || imax > ntot) y_error("invalid range");
        offset = imin - 1;
        samples = imax - imin + 1;
      }
    } else {
      y_error("unexpected type of argument");
      return;
    }
    if (offset != obj->offset) {
      seek_to(obj, offset);
    }
    read_samples(obj, samples);
  } else if (obj->format->mode == 'w') {
    /* Ouput audio stream. */
    write_samples(obj, 0);
  } else {
    y_error("unexpected input/output mode");
  }
}

static void
ysox_extract(void* addr, char* member)
{
  ysox_t* obj = (ysox_t*)addr;
  sox_format_t* ft = obj->format;
  if (ft == NULL) {
    y_error("sound stream has been closed");
  }
  switch (member != NULL ? member[0] : '\0') {
  case 'b':
    if (strcmp(member, "bits_per_sample") == 0) {
      ypush_long(ft->encoding.bits_per_sample);
      return;
    }
    break;
  case 'c':
    if (strcmp(member, "channels") == 0) {
      ypush_long(ft->signal.channels);
      return;
    }
    if (strcmp(member, "clips") == 0) {
      ypush_long(ft->clips);
      return;
    }
    if (strcmp(member, "compression") == 0) {
      ypush_double(ft->encoding.compression);
      return;
    }
    break;
  case 'd':
    if (strcmp(member, "duration") == 0) {
      ypush_double(ft->signal.length/ft->signal.channels/ft->signal.rate);
      return;
    }
    break;
  case 'e':
    if (strcmp(member, "encoding") == 0) {
      ypush_int(ft->encoding.encoding);
      return;
    }
    if (strcmp(member, "errno") == 0) {
      ypush_int(ft->sox_errno);
      return;
    }
    if (strcmp(member, "errstr") == 0) {
      const size_t N = sizeof(ft->sox_errstr);
      char tmp[N+1];
      memcpy(tmp, ft->sox_errstr, N);
      tmp[N] = '\0';
      push_string(tmp);
      return;
    }
    break;
  case 'f':
    if (strcmp(member, "filename") == 0) {
      push_string(ft->filename);
      return;
    }
    if (strcmp(member, "filetype") == 0) {
      push_string(ft->filetype);
      return;
    }
    break;
  case 'l':
     if (strcmp(member, "length") == 0) {
       ypush_long(ft->signal.length);
       return;
     }
     break;
  case 'm':
     if (strcmp(member, "mode") == 0) {
       ypush_int(ft->mode);
       return;
     }
     break;
  case 'o':
    if (strcmp(member, "offset") == 0) {
      ypush_long(obj->offset);
      return;
    }
    break;
  case 'p':
    if (strcmp(member, "precision") == 0) {
      ypush_long(ft->signal.precision);
      return;
    }
    break;
  case 'r':
    if (strcmp(member, "rate") == 0) {
      ypush_double(ft->signal.rate);
      return;
    }
    if (strcmp(member, "readable") == 0) {
      ypush_int(ft != NULL && ft->mode == 'r');
      return;
    }
    break;
  case 's':
    if (strcmp(member, "samples") == 0) {
      ypush_long(ft->signal.length/ft->signal.channels);
      return;
    }
    if (strcmp(member, "seekable") == 0) {
      ypush_int(ft->seekable ? TRUE : FALSE);
      return;
    }
    break;
  case 'w':
    if (strcmp(member, "writable") == 0) {
      ypush_int(ft != NULL && ft->mode == 'w');
      return;
    }
    break;
  }
  y_error("bad member name");
}

static ysox_t*
ysox_push(void)
{
  return (ysox_t*)ypush_obj(&ysox_type, sizeof(ysox_t));
}

static ysox_t*
ysox_fetch(int iarg)
{
  return (ysox_t*)yget_obj(iarg, &ysox_type);
}

/*---------------------------------------------------------------------------*/
/* INITIALIZATION */

void
Y_sox_init(int argc)
{
  static int init = 0;

  /* Initialize libSoX. */
  if ((init & 1) == 0) {
    critical();
    if (sox_init() != SOX_SUCCESS) {
      y_error("failed to initialize SoX effects library");
    }
    init |= 1;
  }
  if ((init & 2) == 0) {
    critical();
    if (sox_format_init() != SOX_SUCCESS) {
      y_error("failed to load SoX format handler plugins");
    }
    init |= 2;
  }

  /* Audio stream objects can be used as a function. */
  if (ysox_type.uo_ops == NULL) {
    yfunc_obj(&ysox_type);
  }

  /* Define constants. */
#define DEFINE_INT_CONST(c)    define_int_const(#c, c)
#define DEFINE_LONG_CONST(c)   define_long_const(#c, c)
#define DEFINE_DOUBLE_CONST(c) define_double_const(#c, c)
  DEFINE_INT_CONST(SOX_ENCODING_UNKNOWN);
  DEFINE_INT_CONST(SOX_ENCODING_SIGN2);
  DEFINE_INT_CONST(SOX_ENCODING_UNSIGNED);
  DEFINE_INT_CONST(SOX_ENCODING_FLOAT);
  DEFINE_INT_CONST(SOX_ENCODING_FLOAT_TEXT);
  DEFINE_INT_CONST(SOX_ENCODING_FLAC);
  DEFINE_INT_CONST(SOX_ENCODING_HCOM);
  DEFINE_INT_CONST(SOX_ENCODING_WAVPACK);
  DEFINE_INT_CONST(SOX_ENCODING_WAVPACKF);
  DEFINE_INT_CONST(SOX_ENCODING_ULAW);
  DEFINE_INT_CONST(SOX_ENCODING_ALAW);
  DEFINE_INT_CONST(SOX_ENCODING_G721);
  DEFINE_INT_CONST(SOX_ENCODING_G723);
  DEFINE_INT_CONST(SOX_ENCODING_CL_ADPCM);
  DEFINE_INT_CONST(SOX_ENCODING_CL_ADPCM16);
  DEFINE_INT_CONST(SOX_ENCODING_MS_ADPCM);
  DEFINE_INT_CONST(SOX_ENCODING_IMA_ADPCM);
  DEFINE_INT_CONST(SOX_ENCODING_OKI_ADPCM);
  DEFINE_INT_CONST(SOX_ENCODING_DPCM);
  DEFINE_INT_CONST(SOX_ENCODING_DWVW);
  DEFINE_INT_CONST(SOX_ENCODING_DWVWN);
  DEFINE_INT_CONST(SOX_ENCODING_GSM);
  DEFINE_INT_CONST(SOX_ENCODING_MP3);
  DEFINE_INT_CONST(SOX_ENCODING_VORBIS);
  DEFINE_INT_CONST(SOX_ENCODING_AMR_WB);
  DEFINE_INT_CONST(SOX_ENCODING_AMR_NB);
  DEFINE_INT_CONST(SOX_ENCODING_CVSD);
  DEFINE_INT_CONST(SOX_ENCODING_LPC10);
#if SOX_LIB_VERSION_CODE >= SOX_LIB_VERSION(14,4,2)
  DEFINE_INT_CONST(SOX_ENCODING_OPUS);
#endif
#if 0
  DEFINE_INT_CONST(SOX_SEEK_SET);
#endif
  DEFINE_INT_CONST(SOX_UNSPEC);
  DEFINE_LONG_CONST(SOX_UNKNOWN_LEN);
  DEFINE_LONG_CONST(SOX_IGNORE_LENGTH);
  DEFINE_INT_CONST(SOX_DEFAULT_CHANNELS);
  DEFINE_DOUBLE_CONST(SOX_DEFAULT_RATE);
  DEFINE_INT_CONST(SOX_DEFAULT_PRECISION);
  DEFINE_INT_CONST(SOX_DEFAULT_ENCODING);
  DEFINE_INT_CONST(SOX_SAMPLE_PRECISION);
  DEFINE_INT_CONST(SOX_SAMPLE_MIN);
  DEFINE_INT_CONST(SOX_SAMPLE_MAX);
#undef DEFINE_INT_CONST
#undef DEFINE_LONG_CONST
#undef DEFINE_DOUBLE_CONST

  ypush_nil();
}

/*---------------------------------------------------------------------------*/
/* READING AUDIO */

void
Y_sox_close(int argc)
{
  ysox_t* obj;

  if (argc != 1) y_error("expecting exactly one argument");
  obj = ysox_fetch(0);
  if (obj->format != NULL) {
    critical();
    sox_close(obj->format);
    obj->format = NULL;
    obj->offset = 0;
  }
}

void
Y_sox_open_read(int argc)
{
  if (argc != 1) {
    y_error("expecting exactly one argument");
  } else {
    const char *path = fetch_path(0);
    ysox_t* obj = ysox_push();
    critical();
    obj->format = sox_open_read(path, NULL, NULL, NULL);
    if (obj->format == NULL) y_error("failed to open audio file");
    obj->offset = 0;
  }
}

void
Y_sox_read(int argc)
{
  if (argc != 2) {
    y_error("expecting exactly two arguments");
  } else {
    ysox_t* obj = ysox_fetch(1);
    long samples = ygets_l(0);
    read_samples(obj, samples);
  }
}

static void
read_samples(ysox_t* obj, long samples)
{
  void *buf, *tmp;
  long channels, n, np;
  if (obj->format == NULL || obj->format->mode != 'r') {
    y_error("sound stream not open for reading");
  }
  channels = obj->format->signal.channels;
  if (samples <= 0) {
    if (samples < 0) {
      y_error("invalid number of samples");
    }
    ypush_nil();
    return;
  }
  buf = push_samples(channels, samples);
  critical();
  n = sox_read(obj->format, buf, channels*samples);
  np = (n > 0 ? n/channels : 0);
  obj->offset += np;
  if (n < 0) y_errorn("unexpected negative count (%ld)", n);
  if (n%channels != 0) y_warnn("number of samples (%ld) is not a "
                               "multiple of the number of channels", n);
  if (np < samples) {
    if (np == 0) {
      /* Probably end of stream. */
      yarg_drop(1);
      ypush_nil();
    } else {
      /* Short stream. */
      tmp = push_samples(channels, np);
      memcpy(tmp, buf, channels*np*sizeof(sox_sample_t));
      yarg_swap(1, 0);
      yarg_drop(1);
    }
  }
}

void
Y_sox_seek(int argc)
{
  if (argc != 2) {
    y_error("expecting exactly two arguments");
  } else {
    ysox_t* obj = ysox_fetch(1);
    long offset = ygets_l(0);
    seek_to(obj, offset);
    yarg_drop(1); /* left the sound stream on top of the stack */
  }
}

static void
seek_to(ysox_t* obj, long offset)
{
  long channels, length;
  if (obj->format == NULL || obj->format->mode != 'r') {
    y_error("sound stream not open for reading");
  }
  channels = obj->format->signal.channels;
  length = obj->format->signal.length;
  if (offset < 0) y_error("offset must be nonnegative");
  if (offset*channels < 0) y_error("integer overflow");
  if (offset*channels > length) offset = length/channels;
  if (obj->offset != offset) {
    critical();
    if (sox_seek(obj->format, offset*channels, SOX_SEEK_SET) != SOX_SUCCESS) {
      y_errorq("sox_seek failed (%s)", obj->format->sox_errstr);
    }
    obj->offset = offset;
  }
}

/*---------------------------------------------------------------------------*/
/* WRITING AUDIO */

static sox_bool
overwrite_permitted(char const * filename)
{
  return sox_true;
}

static sox_bool
overwrite_forbidden(char const * filename)
{
  return sox_false;
}

void
Y_sox_open_write(int argc)
{
  sox_signalinfo_t signal;
  sox_encodinginfo_t encodinginfo;
  sox_format_t* ft = NULL;
  /*sox_oob_t oob;*/
  double rate = SOX_DEFAULT_RATE;
  double compression = HUGE_VAL;
  ysox_t* obj;
  char* path = NULL;
  char* filetype = NULL;
  unsigned int bits_per_sample = SOX_UNSPEC;
  unsigned int precision = SOX_DEFAULT_PRECISION;
  unsigned int channels = SOX_DEFAULT_CHANNELS;
  int encoding = SOX_DEFAULT_ENCODING;
  int overwrite = FALSE;
  int iarg;
  static long bits_per_sample_index = -1L;
  static long channels_index = -1L;
  static long compression_index = -1L;
  static long encoding_index = -1L;
  static long filetype_index = -1L;
  static long overwrite_index = -1L;
  static long precision_index = -1L;
  static long rate_index = -1L;
  static long template_index = -1L;

  /* Initialize all keyword indexes. */
#define INIT(s) if (s##_index == -1L) s##_index = yget_global(#s, 0)
  INIT(bits_per_sample);
  INIT(channels);
  INIT(compression);
  INIT(encoding);
  INIT(filetype);
  INIT(overwrite);
  INIT(precision);
  INIT(rate);
  INIT(template);
#undef INIT

  /* Initialize encoding information. */
  sox_init_encodinginfo(&encodinginfo);
  encodinginfo.encoding = encoding;
  encodinginfo.bits_per_sample = bits_per_sample;
  encodinginfo.compression = compression;
  encodinginfo.compression = 1.0;

  /* Initialize signal information. */
  signal.rate = rate;
  signal.channels = channels;
  signal.precision = precision;
  /*precision = sox_precision(encoding, bits_per_sample);*/
  signal.length = SOX_UNKNOWN_LEN;
  signal.mult = NULL;

  /* First parse template keyword. */
  for (iarg = argc - 1; iarg >= 0; --iarg) {
    long index = yarg_key(iarg);
    if (index == template_index) {
      --iarg;
      ft = ysox_fetch(iarg)->format;
      if (ft == NULL) {
        y_error("input/output of template stream has been closed");
      }
      memcpy(&signal, &ft->signal, sizeof(signal));
      signal.length = SOX_UNKNOWN_LEN;
      if (signal.mult != NULL) {
        fprintf(stderr, "WARNING non-NULL signal.mult = 0x%lx\n",
                (unsigned long)signal.mult);
      }
      memcpy(&encodinginfo, &ft->encoding, sizeof(encodinginfo));
      filetype = ft->filetype;
    }
  }

  /* Parse positional arguments and other keywords. */
  for (iarg = argc - 1; iarg >= 0; --iarg) {
    long index = yarg_key(iarg);
    if (index < 0) {
      /* Positional argument. */
      if (path == NULL) {
        path = fetch_path(iarg);
      } else {
        y_error("too many arguments");
      }
    } else {
      /* Keyword argument. */
      --iarg;
      if (index == bits_per_sample_index) {
        long value = ygets_l(iarg);
        bits_per_sample_index = (unsigned int)value;
        if (value <= 0 || bits_per_sample_index != value) {
          y_error("illegal bits per sample");
        }
      } else if (index == channels_index) {
        long value = ygets_l(iarg);
        channels = (unsigned int)value;
        if (value <= 0 || channels != value) {
          y_error("illegal number of channels");
        }
      } else if (index == compression_index) {
        compression = ygets_d(iarg);
        if (compression <= 0.0) {
          y_error("illegal compression");
        }
      } else if (index == encoding_index) {
        long value = ygets_l(iarg);
        encoding = (unsigned int)value;
        if (value <= 0 || encoding != value) {
          y_error("illegal encoding");
        }
      } else if (index == filetype_index) {
        filetype = ygets_q(iarg);
      } else if (index == overwrite_index) {
        overwrite = yarg_true(iarg);
      } else if (index == precision_index) {
        long value = ygets_l(iarg);
        precision = (unsigned int)value;
        if (value <= 0 || precision != value) {
          y_error("illegal precision");
        }
      } else if (index == rate_index) {
        rate = ygets_d(iarg);
        if (rate <= 0.0) {
          y_error("illegal rate");
        }
      } else if (index != template_index) {
        y_error("unsupported keyword");
      }
    }
  }
  if (path == NULL) y_error("path argument is missing");


  obj = ysox_push();
  critical();
  switch_fpemask(OFF);
  obj->format = sox_open_write(path, &signal, &encodinginfo,
                               filetype, NULL,
                               (overwrite ? overwrite_permitted :
                                overwrite_forbidden));
  switch_fpemask(ON);
  if (obj->format == NULL) y_error("failed to open audio file");
  obj->offset = 0;
}

void
Y_sox_write(int argc)
{
  if (argc != 2) y_error("expecting exactly two arguments");
  write_samples(ysox_fetch(1), 0);
}

static void
write_samples(ysox_t* obj, int iarg)
{
  void* buf;
  long samples, channels, ntot, n;
  long dims[Y_DIMSIZE];
  int type, integer;
  size_t nbits;

  if (obj->format == NULL || obj->format->mode != 'w') {
    y_error("sound stream not open for writing");
  }
  channels = obj->format->signal.channels;
  buf = ygeta_any(iarg, &ntot, dims, &type);
  switch (type) {
  case Y_CHAR:
    nbits = 8*sizeof(char);
    integer = TRUE;
    break;
  case Y_SHORT:
    nbits = 8*sizeof(short);
    integer = TRUE;
    break;
  case Y_INT:
    nbits = 8*sizeof(int);
    integer = TRUE;
    break;
  case Y_LONG:
    nbits = 8*sizeof(long);
    integer = TRUE;
    break;
  case Y_FLOAT:
    nbits = 8*sizeof(float);
    integer = FALSE;
    break;
  case Y_DOUBLE:
    nbits = 8*sizeof(double);
    integer = FALSE;
    break;
  default:
    y_error("invalid audio data type");
    return;
  }
  if ((dims[0] == 1 || dims[0] == 2) && dims[1] == channels) {
    samples = ntot/channels;
  } else if (channels == 1 && dims[0] <= 1) {
    samples = ntot;
  } else {
    y_error("expecting CHANNELS-by-SAMPLES audio data");
  }
  if (SOX_SAMPLE_PRECISION != 32 || sizeof(sox_sample_t) != 4) {
    y_error("expecting 32-bit integers for SoX audio samples");
  }
  if (! integer || nbits != SOX_SAMPLE_PRECISION) {
    /* Convert to SoX audio samples (signed 32-bit integers).  According to
     * libSoX documentation:
     *
     *  - Conversions should be as accurate as possible (with rounding).
     *
     *  - Unsigned integers are converted to and from signed integers by
     *    flipping the upper-most bit then treating them as signed integers.
     */
    sox_sample_t* tmp = push_samples(channels, samples);
    long i, clips = 0;
    if (integer) {
      /* FIXME: not really rounding to nearest value? */
      if (nbits == 8) {
        /* We assume unsigned bytes. */
        const uint8_t* inp = buf;
        for (i = 0; i < ntot; ++i) {
          tmp[i] = SOX_UNSIGNED_TO_SAMPLE(8, inp[i]);
        }
      } else if (nbits == 16) {
        const int16_t* inp = buf;
        for (i = 0; i < ntot; ++i) {
          tmp[i] = SOX_UNSIGNED_TO_SAMPLE(16, inp[i]);
        }
      } else if (nbits == 64) {
        const int64_t* inp = buf;
        for (i = 0; i < ntot; ++i) {
          tmp[i] = (sox_sample_t)(inp[i] >> 32);
        }
      } else {
        y_error("unsupported integer type for conversion to SoX audio samples");
      }
    } else {
      /* For floating-point values, we convert input samples from range [-1,1)
       * to range [MIN,MAX] using rounding to nearest integer.  Here, for
       * short, we denote MIN=SOX_SAMPLE_MIN and MAX=SOX_SAMPLE_MAX are
       * integers.
       *
       * Hence:  sample = floor(MULT*value + BIAS)
       * with:   MULT = 1 + MAX
       * and:    BIAS = 0.5
       *
       * Note that MIN = -1 - MAX = -MULT.
       *
       * Clipping at the lower bound occurs when:
       *       floor(MULT*value + BIAS) < MIN
       *  <=>  MULT*value + BIAS < MIN = -MULT
       *  <=>  value < cmin = (MIN - BIAS)/MULT
       *                    = -1 - BIAS/MULT
       *
       * Clipping at the upper bound occurs when:
       *       floor(MULT*value + BIAS) > MAX
       *  <=>  MULT*value + BIAS >= MAX + 1 = MULT
       *  <=>  value >= cmax = (MAX + 1 - BIAS)/MULT
       *                     = 1 - BIAS/MULT
       */
      const double mult =  1.0 + (double)SOX_SAMPLE_MAX;
      const double bias =  0.5;
      const double cmin = -1.0 - bias/mult;
      const double cmax =  1.0 - bias/mult;
      if (type == Y_FLOAT) {
        const float* inp = buf;
        for (i = 0; i < ntot; ++i) {
          double value = inp[i];
          if (value < cmin) {
            ++clips;
            tmp[i] = SOX_SAMPLE_MIN;
          } else if (value >= cmax) {
            ++clips;
            tmp[i] = SOX_SAMPLE_MAX;
          } else {
            tmp[i] = (sox_sample_t)floor(mult*value + bias);
          }
        }
      } else {
        const double* inp = buf;
        for (i = 0; i < ntot; ++i) {
          double value = inp[i];
          if (value < cmin) {
            ++clips;
            tmp[i] = SOX_SAMPLE_MIN;
          } else if (value >= cmax) {
            ++clips;
            tmp[i] = SOX_SAMPLE_MAX;
          } else {
            tmp[i] = (sox_sample_t)floor(mult*value + bias);
          }
        }
      }
    }

    /* Update the number of clippings and replace stack items. */
    obj->format->clips += clips;
    yarg_swap(iarg + 1, 0);
    yarg_drop(1);
    buf = tmp;
  }

  critical();
  n = sox_write(obj->format, buf, ntot);
  obj->offset += (n > 0 ? n/channels : 0);
  if (n != ntot) y_errorn("write error (%ld samples written)", n);
}

/*---------------------------------------------------------------------------*/
/* ENCODINGS AND FORMATS */

void
Y_sox_encodings(int argc)
{
  if (argc != 1 || ! yarg_nil(0)) y_error("must be called with a "
                                          "single void argument");
  ypush_long(SOX_ENCODINGS - 1);
}

#define FUNCTION(member, pusher) void                                   \
Y_sox_encoding_##member(int argc)                                       \
{                                                                       \
  int i;                                                                \
  if (argc != 1) y_error("expecting a single argument");                \
  i = ygets_i(0);                                                       \
  if (i < 1 || i >= SOX_ENCODINGS) y_error("invalid encoding number");  \
  pusher(sox_get_encodings_info()[i].member);                           \
}
FUNCTION(flags, ypush_int)
FUNCTION(name, push_string)
FUNCTION(desc, push_string)
#undef FUNCTION

void
Y_sox_formats(int argc)
{
  long dims[2];
  const sox_format_tab_t* fmt;
  char **arr;
  int i, n;

  if (yarg_subroutine()) y_error("must be called as a function");
  if (argc != 1 || ! yarg_nil(0)) y_error("must be called with a "
                                          "single void argument");

  /* Get list of formats and count them. */
  fmt = sox_get_format_fns();
  for (n = 0; fmt[n].name != NULL; ++n) ;

  /* Save names into an array of strings. */
  if (n > 0) {
    dims[0] = 1;
    dims[1] = n;
    arr = ypush_q(dims);
    for (i = 0; i < n; ++i) {
      arr[i] = p_strcpy(fmt[i].name);
    }
  } else {
    ypush_nil();
  }
}

/*---------------------------------------------------------------------------*/
/* COMMENTS AND METADATA */

void
Y_sox_append_comment(int argc)
{
  if (argc != 2) {
    y_error("expecting at exactly two arguments");
  } else {
    sox_format_t* ft = ysox_fetch(1)->format;
    const char* comment = ygets_q(0);
    if (ft == NULL) y_error("audio input/output has been closed");
    if (comment != NULL) {
      sox_append_comment(&ft->oob.comments, comment);
    }
  }
}

void
Y_sox_set_metadata(int argc)
{
  if (argc != 3) {
    y_error("expecting exactly three arguments");
  } else {
    sox_format_t* ft = ysox_fetch(2)->format;
    const char* id = ygets_q(1);
    const char* value = ygets_q(0);
    char* buffer;
    char** comments;
    size_t id_len, value_len;
    if (ft == NULL) y_error("audio input/output has been closed");
    id_len = (id == NULL ? 0 : strlen(id));
    if (value == NULL) {
      value = "";
      value_len = 0;
    } else {
      value_len = strlen(value);
    }
    comments = ft->oob.comments;
    if (comments != NULL) {
      /* Replace existing id=value comment if found. */
      for (; *comments != NULL; ++comments) {
        char* comment = *comments;
        if (strncasecmp(comment, id, id_len) == 0 && comment[id_len] == '=') {
          if (! yarg_subroutine()) {
            /* Save old value onto the stack. */
            push_string(comment + id_len + 1);
          }
          critical();
          buffer = malloc(id_len + value_len + 2);
          if (buffer == NULL) {
            y_error("insufficient memory");
          }
          *comments = format_metadata(buffer, id, id_len, value, value_len);
          free(comment);
          return;
        }
      }
    }
    buffer = ypush_scratch(id_len + value_len + 2, NULL);
    sox_append_comment(&ft->oob.comments,
                       format_metadata(buffer, id, id_len, value, value_len));
    yarg_drop(1); /* discard scratch buffer */
  }
}

static char*
format_metadata(char* buffer, const char* id, size_t id_len,
                const char* value, size_t value_len)
{
  size_t i;
  char* result = buffer;
  if (id_len < 1) y_error("bad metadata identifier");
  for (i = 0; i < id_len; ++i) {
    if (isspace(id[i])) {
      y_error("metadata identifier must not contain newlines nor spaces");
    }
    buffer[i] = id[i];
  }
  buffer[id_len] = '=';
  buffer += id_len + 1;
  for (i = 0; i < value_len; ++i) {
    if (value[i] == '\n') {
      y_error("metadata value must not contain newlines");
    }
    buffer[i] = value[i];
  }
  buffer[value_len] = '\0';
  return result;
}

void
Y_sox_get_metadata(int argc)
{
  if (argc != 2) {
    y_error("expecting exactly two arguments");
  } else {
    sox_format_t* ft = ysox_fetch(1)->format;
    const char* id = ygets_q(0);
    const char* value = (ft == NULL || id == NULL ? NULL
                         : sox_find_comment(ft->oob.comments, id));
    push_string(value);
  }
}

void
Y_sox_delete_comments(int argc)
{
  if (argc != 1) {
    y_error("expecting exactly one argument");
  } else {
    sox_format_t* ft = ysox_fetch(0)->format;
    if (ft != NULL) {
      sox_delete_comments(&ft->oob.comments);
    }
  }
}

void
Y_sox_copy_comments(int argc)
{
  if (argc != 1) {
    y_error("expecting exactly one argument");
  } else {
    sox_format_t* ft = ysox_fetch(0)->format;
    size_t i, n = (ft == NULL ? 0 : sox_num_comments(ft->oob.comments));
    if (n <= 0) {
      ypush_nil();
    } else {
      char **arr;
      if (n == 1) {
        arr = ypush_q(NULL);
      } else {
        long dims[2];
        dims[0] = 1;
        dims[1] = n;
        arr = ypush_q(dims);
      }
      for (i = 0; i < n; ++i) {
        arr[i] = p_strcpy(ft->oob.comments[i]);
      }
    }
  }
}

/*---------------------------------------------------------------------------*/
/* UTILITIES */

static void
critical(void)
{
  if (p_signalling) {
    p_abort();
  }
}

static void*
push_samples(long channels, long samples)
{
  long dims[3];

  dims[0] = 2;
  dims[1] = channels;
  dims[2] = samples;
  if (sizeof(sox_sample_t) == sizeof(short)) {
    return ypush_s(dims);
  } else if (sizeof(sox_sample_t) == sizeof(int)) {
    return ypush_i(dims);
  } else if (sizeof(sox_sample_t) == sizeof(long)) {
    return ypush_l(dims);
  } else {
    y_error("no corresponding integer type");
  }
  return NULL;
}

static char*
fetch_path(int iarg)
{
  char** arr = ypush_q(NULL);
  char* arg = ygets_q(iarg + 1);
  if (arg != NULL) arr[0] = p_native(arg);
  yarg_swap(iarg + 1, 0);
  yarg_drop(1);
  return arr[0];
}

static void
push_string(const char* str)
{
  ypush_q(NULL)[0] = p_strcpy(str);
}

static void
define_int_const(const char* name, int value)
{
  ypush_int(value);
  yput_global(yget_global(name, 0), 0);
  yarg_drop(1);
}

static void
define_long_const(const char* name, long value)
{
  ypush_long(value);
  yput_global(yget_global(name, 0), 0);
  yarg_drop(1);
}

static void
define_double_const(const char* name, double value)
{
  ypush_double(value);
  yput_global(yget_global(name, 0), 0);
  yarg_drop(1);
}

/*---------------------------------------------------------------------------*/
/*
 * The following function is from yorick-gl (glfpu.c).
 *
 * Functions to turn on/off FPE interrupt masks before and after OpenGL
 * calls.  The OpenGL standard does not permit FPE trapping required by
 * yorick (yet doesn't bother turning it on and off itself like libm).
 * This hasn't been a problem on any platform until Mac OSX 10.7, which
 * somehow manages to generate SIGFPE while attempting to clear the newly
 * created GLX window.  It also apparently has installed its own SIGFPE
 * handler, since yorick's is never invoked, and drops into an infinite
 * loop.  This code uses fenv.h to restore the default FPU mode on entry
 * to any routine which calls an OpenGL function, and put back yorick's
 * FPU mode upon return.  The code is fragile, since it may not restore
 * the original mode if the code is interrupted (for example by C-c).
 * A separate interpreted API has been added to restore the yorick FPU
 * environment in case this happens.
 *
 * Copyright (c) 2012, David H. Munro.
 */

#ifndef MISSING_FENV_H

#include <fenv.h>

static void
switch_fpemask(int on)
{
  static fenv_t fenv;
  static int valid_fenv = 0;
  static int depth_fenv = 0;  /* zero means at state saved in fenv */

  valid_fenv = (valid_fenv || fegetenv(&fenv) == 0);
  if (valid_fenv) {
    if (on) {
      if (on != 1) depth_fenv = 1;
      if (depth_fenv && !--depth_fenv)
        fesetenv(&fenv);
    } else {
      if (!depth_fenv++)
        fesetenv(FE_DFL_ENV);
    }
  }
}

#else

staic void
switch_fpemask(int on)
{
}

#endif

void
Y_sox_fpemask(int argc)
{
  if (argc != 1) y_error("expecting exactly one argument");
  switch_fpemask(ygets_i(0));
}
