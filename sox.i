/*
 * sox.i --
 *
 * Yorick interface to SoX (an audio file-format and effect library).
 *
 *-----------------------------------------------------------------------------
 *
 * Copyright (C) 2015 Éric Thiébaut <eric.thiebaut@univ-lyon1.fr>
 *
 */

if (is_func(plug_in)) plug_in, "ysox";

extern sox_open_read;
/* DOCUMENT s = sox_open_read(path);

     Open sound file PATH for reading and  return a handle for it.  The handle
     can be indexed to read some audio samples:

        s(i)         yields i-th sample;
        s(i1:i2)     yields samples in the range i1 to i2;
        s() or s(:)  yield all remaining samples;

     the same indexing rules as in Yorick apply (i.e., indices less or equal 0
     refer to the end  of the stream).  If i1 or i2  are omitted, they default
     to the  current stream position and  the end of the  stream respectively.
     Fewer samples, or even no data, may  be returned if the end of the stream
     is encountered.  See sox_read for more details.

     The handle can also be used as a structure to retrieve some informations:

        s.bits_per_sample = number of bits per sample;
        s.compression = compression factor (where applicable);
        s.filename    = name of file;
        s.filetype    = type of file, as determined by header inspection or
                        libmagic;
        s.seekable    = can seek on this file;
        s.offset      = current offset with respect to start of data;
        s.clips       = incremented if clipping occurs;
        s.errno       = error code;
        s.errstr      = error message;
        s.rate        = samples per second, 0 if unknown;
        s.channels    = number of sound channels, 0 if unknown;
        s.samples     = number of sound samples, 0 if unknown;
        s.precision   = bits per sample, 0 if unknown;
        s.length      = samples*channels in file, 0 if unknown, -1 if
                        unspecified;
        s.mode        = read or write mode ('r' or 'w');
        s.duration    = duration in seconds;
        s.encoding    = encoding (integer code);

     For instance, the duration (in seconds) is given by:

        s.length/s.channels/s.rate

     or by:

        s.samples/s.rate

     The stream is automatically closed when S is no longer in use.


   SEE ALSO: sox_read, sox_open_write, sox_close. */

extern sox_close;
/* DOCUMENT sox_close, s;

     Close the audio stream S.  No input/output can be done with S after that.
     This function is probably not  needed as streams get automatically closed
     when no longer in use.

   SEE ALSO: sox_open_read, sox_open_write.
 */

extern sox_read;
/* DOCUMENT b = sox_read(s, n);

     Read N  samples from sound  stream S.  The result  is an array  of 32-bit
     integers of dimension  NC-by-NP where NC is the number  of audio channels
     and NP <= N, i.e. the result may be shorter than what is requested if the
     end of the audio stream is reached.  If there is no more samples to read,
     a void result is returned.

     Note that, compared  to the behavior of SoX library,  the total number of
     samples is not N but N times the number of channels.

   SEE ALSO: sox_seek. */

extern sox_seek;
/* DOCUMENT sox_seek, s, off;
         or sox_seek(s, off);

     Sets the location  at which next samples will be  decoded in sound stream
     S.  OFF  times the number  of channels is the  sample offset at  which to
     position reader.  When called as a function, S is returned.

     Note  that, compared  to  the  behavior of  SoX  library,  the offset  is
     multiplied by the number of channels.

   SEE ALSO: sox_read. */

extern sox_open_write;
/* DOCUMENT s = sox_open_write(path);

     Open sound file PATH for writing and  return a handle for it.  The handle
     can be used as a function or as a subroutine to write some audio samples:

        s, buf;      append audio samples to S;
        s(buf);      idem, plus return, possibly converted, audio data;

     where BUF  is an array  of audio  samples (see `sox_write`  for details).
     The handle can also be used  as a structure to retrieve some informations
     (see `sox_open_read`).


   KEYWORDS

     bits_per_sample - The number of bits per sample.

     channels - The number of audio channels.  Default is 2 (stereo) unless
             a template with a different number of channels is used.

     compression - The compression level.

     encoding - The identifier of the encoding to use.

     filetype - The name of the file type.

     overwrite -  Indicates whether overwriting  an existing file  is allowed.
             By default, it is forbidden.

     precision - The number of bits per sample.

     rate - The rate (in Hz) of the audio stream.

     template - An audio stream to serve  as a template to define the settings
             of the created output stream.  The comments are not copied.

   SEE ALSO: sox_open_read, sox_write. */

extern sox_write;
/* DOCUMENT sox_write, s, buf;
         or samp = sox_write(s, buf);

     Write  the audio  samples in  BUF to  audio stream  S.  BUF  should be  a
     NC-by-NS array with NC the number of  audio channels and NS the number of
     samples (however, if NS=1, the last dimension can be missing and if NC=1,
     the first dimension, can  be missing).  If BUF is not  an array of 32-bit
     integers, conversion  to SoX audio  samples is performed as  follows (MIN
     and  MAX  are the  minimum  and  maximum  possible  values for  a  32-bit
     integer):

      - Floating  point  samples are  converted  from  range [-1,1)  to  range
        [MIN,MAX] using rounding to nearest integer, clipping may occurs.

      - Integer values are converted from  their respective [MIN,MAX] range to
        the [MIN,MAX] range of a 32-bit integer.

     When  called as  a function  the, possibly  converted, audio  samples are
     returned.

   SEE ALSO: sox_open_write. */

extern sox_get_metadata;
extern sox_set_metadata;
extern sox_append_comment;
extern sox_copy_comments;
extern sox_delete_comments;
/* DOCUMENT value = sox_get_metadata(s, id);
         or sox_set_metadata, s, id, value;
         or old_value = sox_set_metadata(s, id, value);
         or sox_append_comment, s, comment;
         or str = sox_copy_comments(s);
         or sox_delete_comments, s;

     The function  `sox_get_metadata()` retrieves the value  associated to the
     identifier ID in audio stream S  or string(0) if not found.  Metadata are
     comments in  the form ID=VALUE.   Note that the  case of letters  does no
     matter in the identifier.  For instance:

         artist = sox_get_metadata(s, "Artist");
         title = sox_get_metadata(s, "Title");

     The function `sox_append_comment()` appends a new comment.

     The  function   `sox_set_metadata()`  set   the  value   associated  with
     identifier ID and returns the former value if any.  For instance:

         former = sox_set_metadata(s, "Artist", usurper);

     The  function   `sox_copy_comments()`  returns  all  the   comments  (and
     metadata) of S as  an array of string(s) or an empty  result if there are
     no comments.

     The function `sox_delete_comments()` deletes all comments (and metadata).


   SEE ALSO: sox_open_read, sox_open_write, sox_delete_comment,
             sox_append_comment.
 */


extern sox_formats;

local SOX_ENCODING_UNKNOWN, SOX_ENCODING_SIGN2, SOX_ENCODING_UNSIGNED, SOX_ENCODING_FLOAT, SOX_ENCODING_FLOAT, SOX_ENCODING_FLAC, SOX_ENCODING_HCOM, SOX_ENCODING_WAVPACK, SOX_ENCODING_WAVPACKF, SOX_ENCODING_ULAW, SOX_ENCODING_ALAW, SOX_ENCODING_G721, SOX_ENCODING_G723, SOX_ENCODING_CL, SOX_ENCODING_CL, SOX_ENCODING_MS, SOX_ENCODING_IMA, SOX_ENCODING_OKI, SOX_ENCODING_DPCM, SOX_ENCODING_DWVW, SOX_ENCODING_DWVWN, SOX_ENCODING_GSM, SOX_ENCODING_MP3, SOX_ENCODING_VORBIS, SOX_ENCODING_AMR, SOX_ENCODING_AMR, SOX_ENCODING_CVSD, SOX_ENCODING_LPC10, SOX_ENCODING_OPUS;
extern sox_encodings;
extern sox_encoding_flags;
extern sox_encoding_name;
extern sox_encoding_desc;
/* DOCUMENT num = sox_encodings();
         or flags = sox_encoding_flags(enc);
         or name = sox_encoding_name(enc);
         or desc = sox_encoding_desc(enc);

      These functions retrieve informations about supported encodings.  NUM is
      the  number  of implemented  encodings.   FLAGS  is  an integer  (0  for
      lossless, 1 for  lossy once, or 2  for lossy twice).  NAME  and DESC are
      strings with the name and description of the encoding.

      The following constants may be used for ENC (depending on the version of
      the SoX library, they may not all be implemented):

      SOX_ENCODING_UNKNOWN      encoding has not yet been determined
      SOX_ENCODING_SIGN2        signed linear 2's comp: Mac
      SOX_ENCODING_UNSIGNED     unsigned linear: Sound Blaster
      SOX_ENCODING_FLOAT        floating point (binary format)
      SOX_ENCODING_FLOAT_TEXT   floating point (text format)
      SOX_ENCODING_FLAC         FLAC compression
      SOX_ENCODING_HCOM         Mac FSSD files with Huffman compression
      SOX_ENCODING_WAVPACK      WavPack with integer samples
      SOX_ENCODING_WAVPACKF     WavPack with float samples
      SOX_ENCODING_ULAW         u-law signed logs: US telephony, SPARC
      SOX_ENCODING_ALAW         A-law signed logs: non-US telephony, Psion
      SOX_ENCODING_G721         G.721 4-bit ADPCM
      SOX_ENCODING_G723         G.723 3 or 5 bit ADPCM
      SOX_ENCODING_CL_ADPCM     Creative Labs 8 --> 2,3,4 bit Compressed PCM
      SOX_ENCODING_CL_ADPCM16   Creative Labs 16 --> 4 bit Compressed PCM
      SOX_ENCODING_MS_ADPCM     Microsoft Compressed PCM
      SOX_ENCODING_IMA_ADPCM    IMA Compressed PCM
      SOX_ENCODING_OKI_ADPCM    Dialogic/OKI Compressed PCM
      SOX_ENCODING_DPCM         Differential PCM: Fasttracker 2 (xi)
      SOX_ENCODING_DWVW         Delta Width Variable Word
      SOX_ENCODING_DWVWN        Delta Width Variable Word N-bit
      SOX_ENCODING_GSM          GSM 6.10 33byte frame lossy compression
      SOX_ENCODING_MP3          MP3 compression
      SOX_ENCODING_VORBIS       Vorbis compression
      SOX_ENCODING_AMR_WB       AMR-WB compression
      SOX_ENCODING_AMR_NB       AMR-NB compression
      SOX_ENCODING_CVSD         Continuously Variable Slope Delta modulation
      SOX_ENCODING_LPC10        Linear Predictive Coding
      SOX_ENCODING_OPUS         Opus compression

      We  recall  that the  encoding  of  an audio  stream  S  created by  the
      `sox_open_read()` or  `sox_open_write()` functions can be  obtained with
      S.encoding.  For instance:

          sox_encoding_desc(s.encoding)

      yields a description of the encoding used for S.


   SEE ALSO: sox_open_read, sox_open_write.
 */

extern sox_fpemask;
/* DOCUMENT sox_fpemask, -1;
     Resets  the SIGFPE  mask  to yorick's  normal  state, permitting  SIGFPE.
     Before  calling some  SoX  functions, the  SIGFPE mask  is  reset to  the
     default setting for  the platform, usually disabling  SIGFPE signals.  If
     the SoX function  is asynchronously interrupted, the SIGFPE  mask may not
     be  reset.   This function  enables  you  to  restore the  normal  yorick
     floating point  environment should  this occur.  You  never need  to call
     this in a functioning program.
 */

local SOX_UNSPEC, SOX_UNKNOWN_LEN, SOX_IGNORE_LENGTH, SOX_DEFAULT_CHANNELS,
  SOX_DEFAULT_RATE, SOX_DEFAULT_PRECISION, SOX_DEFAULT_ENCODING
extern sox_init;
/* DOCUMENT sox_init;
     Initialize  the SoX  library and  global variables.   This subroutine  is
     automatically called when the plugin is  loaded, but can be safely called
     again (e.g., to restore global variables).

     Global constants include the various encodings (see `sox_encodings`) and:

     SOX_UNSPEC            Unspecified value or actual value is not yet known.
     SOX_UNKNOWN_LEN       Unknown length or actual length is not known.
     SOX_IGNORE_LENGTH     Indicate that a format handler should ignore length
                           information in file headers.
     SOX_DEFAULT_CHANNELS  Default channel count is 2 (stereo).
     SOX_DEFAULT_RATE      Default rate is 48000Hz.
     SOX_DEFAULT_PRECISION Default precision is 16 bits per sample.
     SOX_DEFAULT_ENCODING  Default encoding is SIGN2 (linear 2's complement
                           PCM).

   SEE ALSO: sox_open_read, sox_open_write, sox_encodings. */

/* Initialize the internals. */
sox_init;

/*
 * Local Variables:
 * mode: Yorick
 * tab-width: 8
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * fill-column: 78
 * coding: utf-8
 * End:
 */
