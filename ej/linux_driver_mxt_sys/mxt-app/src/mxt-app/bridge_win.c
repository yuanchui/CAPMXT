//------------------------------------------------------------------------------
/// \file   bridge_win.c
/// \brief  Windows build: stub implementations for TCP bridge (not supported).
///         Used when MXT_OS_WINDOWS is defined.
//------------------------------------------------------------------------------

#ifdef MXT_OS_WINDOWS

#include "mxt_app.h"
#include "libmaxtouch/libmaxtouch.h"
#include "libmaxtouch/log.h"

int mxt_socket_server(struct mxt_device *mxt, uint16_t port)
{
  (void)mxt;
  (void)port;
  mxt_err(mxt->ctx, "TCP bridge server is not supported on Windows");
  return MXT_ERROR_NOT_SUPPORTED;
}

int mxt_socket_client(struct mxt_device *mxt, char *ip_address, uint16_t port)
{
  (void)mxt;
  (void)ip_address;
  (void)port;
  mxt_err(mxt->ctx, "TCP bridge client is not supported on Windows");
  return MXT_ERROR_NOT_SUPPORTED;
}

#endif /* MXT_OS_WINDOWS */
