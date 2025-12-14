#ifndef QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_TRACING_HOOKS_H_
#define QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_TRACING_HOOKS_H_

#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

// Interface for performance tracing hooks.
class QUICHE_EXPORT BlindSignTracingHooks {
 public:
  virtual ~BlindSignTracingHooks() = default;

  // Called before (resp. after) the GetInitialData RPC.
  virtual void OnGetInitialDataStart() = 0;
  virtual void OnGetInitialDataEnd() = 0;

  // Called before (resp. after) initializing a batch of Privacy Pass clients
  // and generating blinded token requests.
  virtual void OnGenerateBlindedTokenRequestsStart() = 0;
  virtual void OnGenerateBlindedTokenRequestsEnd() = 0;

  // Called before (resp. after) the AuthAndSign RPC.
  virtual void OnAuthAndSignStart() = 0;
  virtual void OnAuthAndSignEnd() = 0;

  // Called before (resp. after) finalizing a batch of tokens.
  virtual void OnUnblindTokensStart() = 0;
  virtual void OnUnblindTokensEnd() = 0;
};

}  // namespace quiche

#endif  // QUICHE_BLIND_SIGN_AUTH_BLIND_SIGN_TRACING_HOOKS_H_
