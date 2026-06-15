//------------------------------------------------------------------------------
/// \file   log.c
/// \brief  Provides a macro for logging messages.
/// \author Tim Culmer
//------------------------------------------------------------------------------

#include "stdio.h"
#include "stdint.h"
#include "malloc.h"

#include "libmaxtouch.h"
#include "libmaxtouch/utilfuncs.h"



//******************************************************************************
/// \brief  Returns the input log level as a human-readable string.
/// \return Log level string
static const char get_log_level_string(enum mxt_log_level level)
{
  switch (level) {
  case LOG_SILENT:
    return 'S';
  case LOG_FATAL:
    return 'F';
  case LOG_ERROR:
    return 'E';
  case LOG_WARN:
    return 'W';
  case LOG_INFO:
    return 'I';
  case LOG_DEBUG:
    return 'D';
  case LOG_VERBOSE:
  case LOG_DEFAULT:
  case LOG_UNKNOWN:
  default:
    return 'V';
  }
}

//******************************************************************************
/// \brief  Get log verbosity level
enum mxt_log_level mxt_get_log_level(struct libmaxtouch_ctx *ctx)
{
  return ctx->log_level;
}

//******************************************************************************
/// \brief  Set log verbosity level
void mxt_set_log_level(struct libmaxtouch_ctx *ctx, uint8_t verbose)
{
  switch (verbose) {
  case 0:
    ctx->log_level = LOG_SILENT;
    break;
  case 1:
    ctx->log_level = LOG_WARN;
    break;
  case 2:
    ctx->log_level = LOG_INFO;
    break;
  case 3:
    ctx->log_level = LOG_DEBUG;
    break;
  default:
    ctx->log_level = LOG_VERBOSE;
    break;
  }
}

//*****************************************************************************
/// \brief Output buffer to debug as hex
void mxt_log_buffer(struct libmaxtouch_ctx *ctx, enum mxt_log_level level,
                    const char *prefix,
                    const unsigned char *data, size_t count)
{
#if ENABLE_DEBUG
  unsigned int i;
  char *hexbuf;
  size_t strsize = count*3 + 1;

  if (mxt_get_log_level(ctx) > level)
    return;

  hexbuf = (char *)calloc(strsize, sizeof(char));
  if (hexbuf == NULL) {
    mxt_err(ctx, "%s: calloc failure", __func__);
    return;
  }

  for (i = 0; i < count; i++)
    sprintf(&hexbuf[3 * i], "%02X ", data[i]);

  mxt_log(ctx, LOG_VERBOSE, "%s %s", prefix, hexbuf);

  free(hexbuf);
#endif
}

//******************************************************************************
/// \brief Log function
void mxt_log(struct libmaxtouch_ctx *ctx, enum mxt_log_level level, const char *format, ...)
{
  va_list args;

  va_start(args, format);
  ctx->log_fn(ctx, level, format, args);
  va_end(args);
}

//******************************************************************************
/// \brief Output log message to stdout
void mxt_log_stdout(struct libmaxtouch_ctx *ctx, enum mxt_log_level level,
                    const char *format, va_list va_args)
{
  vprintf(format, va_args);
  printf("\n");
}

//******************************************************************************
/// \brief Output log message to stderr, with optional timestamp
void mxt_log_stderr(struct libmaxtouch_ctx *ctx, enum mxt_log_level level,
                    const char *format, va_list va_args)
{
  if (mxt_get_log_level(ctx) < LOG_INFO) {
    mxt_print_timestamp(stderr, false);
    fprintf(stderr, " %c: ", get_log_level_string(level));
  }

  vfprintf(stderr, format, va_args);
  fprintf(stderr, "\n");
}


